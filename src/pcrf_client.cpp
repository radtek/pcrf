#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <list>

extern CLog *g_pcoLog;

/* длительность интервала опроса БД по умолчанию */
#define DB_REQ_INTERVAL 1

static session_handler *g_psoSessionHandler;
static pthread_mutex_t g_tDBReqMutex;
static pthread_t g_tThreadId = -1;
static int g_iStop;

struct sess_state {
	char *m_pszSessionId; /* Session-Id */
  SRARResult *m_psoRARResult;
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque);

static pthread_mutex_t g_tLocalQueueMutex;
static std::list<SSessionInfo> g_listLocalRefQueue;

/* функция для обработки просроченного запроса */
void pcrf_client_req_expire (void *p_pvData, DiamId_t p_pDiamId, size_t p_stDiamIdLen, msg **p_ppMsg)
{
  std::string strDiamId;
  std::string strSessionId;
  sess_state *psoSessState;
  session *psoSess;
  os0_t pszSessId;
  size_t stSessIdLen;

  /* получаем сохраненное состояние сессии */
  {
    int iIsNew;

    /* получаем дескриптор о сессии */
    CHECK_FCT_DO(fd_msg_sess_get(fd_g_config->cnf_dict, *p_ppMsg, &psoSess, &iIsNew), return);
    /* сессия уже должна существовать, новая сессия нам не нужна. проверим это... */
    ASSERT(iIsNew == 0);

    /* получаем идентификатор сессии */
    CHECK_FCT_DO(fd_sess_getsid(psoSess, &pszSessId, &stSessIdLen), return);

    /* получаем данные, ассоциированные с сессией */
    CHECK_FCT_DO(fd_sess_state_retrieve(g_psoSessionHandler, psoSess, &psoSessState), return);
  }

  if (NULL != psoSessState && NULL != psoSessState->m_psoRARResult) {
    psoSessState->m_psoRARResult->m_iResultCode = ETIMEDOUT;
    pthread_mutex_unlock(&psoSessState->m_psoRARResult->m_mutexWait);
  }

  /* готовимся к формированию корректной c-style строки */
  strDiamId.insert(0, reinterpret_cast<char*>(p_pDiamId), p_stDiamIdLen);
  strSessionId.insert(0, reinterpret_cast<char*>(pszSessId), stSessIdLen);
  /* сообщаем об истечении таймаута */
  UTL_LOG_E(*g_pcoLog, "request expired: peer: '%s'", strDiamId.c_str());

  /* Free the message */
  CHECK_FCT_DO(fd_msg_free(*p_ppMsg), /*continue*/);
  *p_ppMsg = NULL;
}

/* получение ответа на Re-Auth/Abort-Session сообщение */
static void pcrf_client_XXA (void * data, struct msg ** msg)
{
	int iFnRes;
	struct sess_state * psoMsgState = NULL;
	struct session * psoSess;
	struct avp * avp;
	struct avp_hdr * hdr;
	int iRC;

	/* Search the session, retrieve its data */
	{
		int iIsNew;
		CHECK_FCT_DO (fd_msg_sess_get (fd_g_config->cnf_dict, *msg, &psoSess, &iIsNew), return);
		ASSERT (iIsNew == 0);

		CHECK_FCT_DO (fd_sess_state_retrieve (g_psoSessionHandler, psoSess, &psoMsgState), return);
    LOG_D("session info: '%p'; data pointer: '%p'", psoSess, data);
	}

  if (NULL != psoMsgState) {
    LOG_D("message state: Session-Id: '%s'; RAR-Result: '%p'", psoMsgState->m_pszSessionId, psoMsgState->m_psoRARResult);
  } else {
    LOG_D("message state: <empty pointer>");
  }
  LOG_D("message state: session handler: '%p'; session info: '%p'", g_psoSessionHandler, psoSess);

	/* Value of Result Code */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictRC, &avp), return);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), return);
		iRC = hdr->avp_value->i32;
	} else {
		iRC = -1;
	}

  if (psoMsgState && psoMsgState->m_psoRARResult) {
    LOG_D("Result-Code: '%d'", iRC);
    psoMsgState->m_psoRARResult->m_iResultCode = iRC;
    CHECK_FCT_DO(pthread_mutex_unlock(&psoMsgState->m_psoRARResult->m_mutexWait), /* continue */);
  }

	/* обрабатываем Result-Code */
	switch (iRC) {
	case 5002: /* DIAMETER_UNKNOWN_SESSION_ID */
		CHECK_FCT_DO((iFnRes = pcrf_client_db_fix_staled_sess (psoMsgState->m_pszSessionId)), /*continue*/ );
		break;
	}

	/* Free the message */
	CHECK_FCT_DO (fd_msg_free (*msg), /*continue*/);
	*msg = NULL;

	return;
}

/* отправка Re-Auth сообщения */
int pcrf_client_RAR (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB p_soReqInfo,
	std::vector<SDBAbonRule> *p_pvectActiveRules,
	std::vector<SDBAbonRule> &p_vectAbonRules,
  SRARResult *p_psoRARRes,
  uint32_t p_uiUsec)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;
	SStat *psoStat = stat_get_branch ("gx client");
	struct msg * psoReq = NULL;
	struct avp * psoAVP;
	union avp_value soAVPValue;
	struct sess_state * psoMsgState;
	struct session *psoSess = NULL;

	/* Create the request */
	CHECK_FCT_DO (iRetVal = fd_msg_new (g_psoDictRAR, MSGFL_ALLOC_ETEID, &psoReq), goto out);

	{
		struct msg_hdr * psoData;
		CHECK_FCT_DO ((iRetVal = fd_msg_hdr (psoReq, &psoData)), goto out);
		psoData->msg_appl = 16777238;
	}

	/* задаем номер сессии */
	{
		int iIsNew;
		CHECK_FCT_DO (iRetVal = fd_sess_fromsid_msg ((uint8_t *)p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str (), p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length (), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(p_soReqInfo.m_psoSessInfo->m_coSessionId.v.data ()));
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_sess_set (psoReq, psoSess), goto out);
	}

	/* выделяем память для структуры, хранящей состояние сессии (запроса) */
	psoMsgState = reinterpret_cast<sess_state *>(malloc (sizeof (struct sess_state)));
	if (psoMsgState == NULL) {
		LOG_D ("malloc failed: %s", strerror (errno));
		goto out;
	}

	/* Now set all AVPs values */

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (iRetVal = fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t*>(const_cast<char*>(p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.data()));
		soAVPValue.os.len  = p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t *>(const_cast<char*>(p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.data()));
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

  /* Set the Auth-Application-Id */
  {
    CHECK_FCT_DO(iRetVal = fd_msg_avp_new(g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
    soAVPValue.u32 = 16777238;
    CHECK_FCT_DO(iRetVal = fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto out);
    CHECK_FCT_DO(iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
  }

	/* Set Re-Auth-Request-Type AVP */
	{
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* AUTHORIZE_ONLY */
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Event-Trigger */
	/* RAT_CHANGE */
	CHECK_FCT_DO(set_RAT_CHANGE_event_trigger(*(p_soReqInfo.m_psoSessInfo), psoReq), /* continue */);
	/* USER_LOCATION_CHANGE */
	CHECK_FCT_DO(set_ULCh_event_trigger(*(p_soReqInfo.m_psoSessInfo), psoReq), /* continue */);

	/* Usage-Monitoring-Information */
	CHECK_POSIX_DO(pcrf_make_UMI(psoReq, *(p_soReqInfo.m_psoSessInfo), false), /* continue */);

	/* Charging-Rule-Remove */
  if (NULL != p_pvectActiveRules) {
    psoAVP = pcrf_make_CRR(p_pcoDBConn, *(p_soReqInfo.m_psoSessInfo), *p_pvectActiveRules, psoStat);
    if (psoAVP) {
      /* put 'Charging-Rule-Remove' into request */
      CHECK_FCT_DO(iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP), );
    }
  }

	/* Charging-Rule-Install */
	psoAVP = pcrf_make_CRI (p_pcoDBConn, &p_soReqInfo, p_vectAbonRules, psoReq, psoStat);
	if (psoAVP) {
		/* put 'Charging-Rule-Install' into request */
		CHECK_FCT_DO ((iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP)), /* continue */ );
	}

  LOG_D("Session-Id: '%s'; Origin-Host: '%s'; Origin-Realm: '%s'",
    p_soReqInfo.m_psoSessInfo->m_coSessionId.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str(),
    p_soReqInfo.m_psoSessInfo->m_coOriginHost.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.c_str(),
    p_soReqInfo.m_psoSessInfo->m_coOriginRealm.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.c_str());

  psoMsgState->m_pszSessionId = strdup (p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str ());
  psoMsgState->m_psoRARResult = p_psoRARRes;

  LOG_D("message state: Session-Id: '%s'; RAR-Result: '%p'", psoMsgState->m_pszSessionId, psoMsgState->m_psoRARResult);
  LOG_D("message state: session handler: '%p'; session info: '%p'", g_psoSessionHandler, psoSess);

	/* Store this value in the session */
	CHECK_FCT_DO ((iRetVal = fd_sess_state_store (g_psoSessionHandler, psoSess, &psoMsgState)), goto out);

  LOG_D("message state: '%p'", psoMsgState);

	/* Send the request */
  if (0 == p_uiUsec) {
    CHECK_FCT_DO((iRetVal = fd_msg_send(&psoReq, pcrf_client_XXA, psoMsgState)), goto out);
  } else {
    timespec soTimeSpec;

    CHECK_FCT_DO((iRetVal = pcrf_make_timespec_timeout(soTimeSpec, p_uiUsec)), goto out);
    CHECK_FCT_DO((iRetVal = fd_msg_send_timeout(&psoReq, pcrf_client_XXA, psoMsgState, pcrf_client_req_expire, &soTimeSpec)), goto out);
  }

out:
	stat_measure (psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

/* отправка RAR сообщения, содержащего Session-Release-Cause */
int pcrf_client_RAR_W_SRCause (SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;
  CTimeMeasurer coTM;
  SStat *psoStat = stat_get_branch("gx client");
  struct msg * psoReq = NULL;
	struct avp * psoAVP;
	union avp_value soAVPValue;
	struct sess_state * psoMsgState;
	struct session *psoSess = NULL;

	/* Create the request */
	CHECK_FCT_DO (fd_msg_new (g_psoDictRAR, MSGFL_ALLOC_ETEID, &psoReq), goto out);

	/* задаем идентификатор приложения */
	{
		struct msg_hdr * psoData;
		CHECK_FCT_DO (fd_msg_hdr (psoReq, &psoData), goto out);
		psoData->msg_appl = 16777238;
	}

	/* задаем номер сессии */
	{
		int iIsNew;
		CHECK_FCT_DO(fd_sess_fromsid_msg((uint8_t *)p_soSessInfo.m_coSessionId.v.data(), p_soSessInfo.m_coSessionId.v.length(), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *)p_soSessInfo.m_coSessionId.v.data();
		soAVPValue.os.len = p_soSessInfo.m_coSessionId.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO(fd_msg_avp_add(psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO(fd_msg_sess_set(psoReq, psoSess), goto out);
	}

	/* выделяем память для хранения инфомрации о состоянии сессии (запроса) */
	psoMsgState = reinterpret_cast<sess_state *>(malloc (sizeof (struct sess_state)));
	if (psoMsgState == NULL) {
		fd_log_debug ("malloc failed: %s", strerror (errno));
		goto out;
	}

	/* Now set all AVPs values */

	/* Set the Auth-Application-Id */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
		soAVPValue.u32 = 16777238;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue ), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginHost.v.c_str ();
		soAVPValue.os.len  = p_soSessInfo.m_coOriginHost.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginRealm.v.c_str ();
		soAVPValue.os.len = p_soSessInfo.m_coOriginRealm.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Re-Auth-Request-Type AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Session-Release-Cause AVP */
	{
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionReleaseCause, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* UNSPECIFIED_REASON */
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	psoMsgState->m_pszSessionId = strdup (p_soSessInfo.m_coSessionId.v.c_str ());

	/* Store this value in the session */
	CHECK_FCT_DO (fd_sess_state_store (g_psoSessionHandler, psoSess, &psoMsgState), goto out);

	/* Send the request */
	CHECK_FCT_DO (fd_msg_send (&psoReq, pcrf_client_XXA, psoMsgState), goto out);

out:
  stat_measure(psoStat, __FUNCTION__, &coTM);

  return iRetVal;
}

/* проверка наличия изменений в политиках */
int pcrf_client_is_any_changes(std::vector<SDBAbonRule> &p_vectActive, std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* проверяем наличие активных неактуальных правил */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectActive.begin(); iter != p_vectActive.end(); ++iter) {
		if (!iter->m_bIsRelevant && iter->m_bIsActivated) {
			iRetVal = 1;
			break;
		}
	}

	if (iRetVal)
		return iRetVal;

	/* проверяем наличие актуальных неактивных правил */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectAbonRules.begin(); iter != p_vectAbonRules.end(); ++iter) {
		if (!iter->m_bIsActivated && iter->m_bIsRelevant) {
			iRetVal = 1;
			break;
		}
	}

	return iRetVal;
}

/* функция обработки записи очереди обновления политик */
int pcrf_client_operate_refqueue_record (otl_connect *p_pcoDBConn, SRefQueue &p_soRefQueue)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;
	SStat *psoStat = stat_get_branch(__FUNCTION__);
	std::vector<std::string> vectSessionList;

	/* загружаем из БД список сессий абонента */
	CHECK_POSIX(pcrf_client_db_load_session_list(*p_pcoDBConn, p_soRefQueue, vectSessionList));

	/* обходим все сессии абонента */
	for (std::vector<std::string>::iterator iterSess = vectSessionList.begin (); iterSess != vectSessionList.end (); ++iterSess) {
		{
			/* сведения о сессии */
			SMsgDataForDB soSessInfo;
			/* список правил профиля абонента */
			std::vector<SDBAbonRule> vectAbonRules;
			/* список активных правил абонента */
			std::vector<SDBAbonRule> vectActive;

			/* инициализация структуры хранения данных сообщения */
			CHECK_FCT_DO(pcrf_server_DBstruct_init (&soSessInfo), goto clear_and_continue);
			/* задаем идентификтор сессии */
			soSessInfo.m_psoSessInfo->m_coSessionId = *iterSess;
			/* загружаем из БД информацию о сессии абонента */
			{
        /* ищем информацию о базовой сессии в кеше */
        if (0 != pcrf_session_cache_get(soSessInfo.m_psoSessInfo->m_coSessionId.v, *soSessInfo.m_psoSessInfo, *soSessInfo.m_psoReqInfo)) {
          /* если не находим в кеше - ищем в БД */
          if (0 != pcrf_server_db_load_session_info(*p_pcoDBConn, soSessInfo, soSessInfo.m_psoSessInfo->m_coSessionId.v, psoStat)) {
            goto clear_and_continue;
          }
        }
				/* необходимо определить диалект хоста */
				CHECK_POSIX_DO (pcrf_peer_dialect(*soSessInfo.m_psoSessInfo), goto clear_and_continue);
        /* для Procera нам понадобится дополнительная информация */
        if (GX_PROCERA == soSessInfo.m_psoSessInfo->m_uiPeerDialect) {
          SSessionInfo soUGWSessInfo;
          std::string strUGWSessionId;
          if (0 == pcrf_server_find_ugw_session_byframedip (*p_pcoDBConn, soSessInfo.m_psoSessInfo->m_coFramedIPAddress.v, soUGWSessInfo, psoStat) && 0 == soUGWSessInfo.m_coSessionId.is_null()) {
            strUGWSessionId = soUGWSessInfo.m_coSessionId.v;
            /* ищем информацию о базовой сессии в кеше */
            if (0 != pcrf_session_cache_get(strUGWSessionId, *soSessInfo.m_psoSessInfo, *soSessInfo.m_psoReqInfo)) {
              /* если не находим в кеше - ищем в БД */
              pcrf_server_db_load_session_info(*p_pcoDBConn, soSessInfo, strUGWSessionId, psoStat);
            }
          }
        }
      }
			/* проверяем, подключен ли пир к freeDiameterd */
			if (!pcrf_peer_is_connected (*soSessInfo.m_psoSessInfo)) {
				iRetVal = ENOTCONN;
				UTL_LOG_E (*g_pcoLog, "peer is not connected: host: '%s'; realm: '%s'", soSessInfo.m_psoSessInfo->m_coOriginHost.v.c_str (), soSessInfo.m_psoSessInfo->m_coOriginRealm.v.c_str());
				goto clear_and_continue;
			}
			/* если в поле action задано значение abort_session */
			if (!p_soRefQueue.m_coAction.is_null() && 0 == p_soRefQueue.m_coAction.v.compare("abort_session")) {
				CHECK_POSIX_DO(pcrf_client_RAR_W_SRCause(*(soSessInfo.m_psoSessInfo)), );
				goto clear_and_continue;
			}
			/* загружаем из БД правила абонента */
			CHECK_POSIX_DO(pcrf_server_create_abon_rule_list(*p_pcoDBConn, soSessInfo, vectAbonRules, psoStat), );
			/* если у абонента нет активных политик завершаем его сессию */
			if (0 == vectAbonRules.size()) {
				CHECK_POSIX_DO(pcrf_client_RAR_W_SRCause(*(soSessInfo.m_psoSessInfo)), );
				goto clear_and_continue;
			}
			/* загружаем список активных правил */
			CHECK_POSIX_DO(pcrf_server_db_load_active_rules(p_pcoDBConn, soSessInfo, vectActive, psoStat), );
			/* формируем список неактуальных правил */
			CHECK_POSIX_DO(pcrf_server_select_notrelevant_active(vectAbonRules, vectActive), );
			/* загружаем информацию о мониторинге */
			CHECK_POSIX_DO(pcrf_server_db_monit_key(*p_pcoDBConn, *(soSessInfo.m_psoSessInfo), psoStat), /* continue */);
			/* проверяем наличие изменений в политиках */
			if (!pcrf_client_is_any_changes(vectActive, vectAbonRules)) {
				UTL_LOG_N (*g_pcoLog, "subscriber_id: '%s'; session_id: '%s': no any changes", soSessInfo.m_psoSessInfo->m_strSubscriberId.c_str (), soSessInfo.m_psoSessInfo->m_coSessionId.v.c_str ());
				goto clear_and_continue;
			}
			/* посылаем RAR-запрос */
			CHECK_POSIX_DO(pcrf_client_RAR(p_pcoDBConn, soSessInfo, &vectActive, vectAbonRules, NULL, 0), /* continue */ );
			/* освобождаем ресуры*/
		clear_and_continue:
			pcrf_server_DBStruct_cleanup(&soSessInfo);
			if (iRetVal)
				break;
		}
	}

	stat_measure(psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

/* функция сканирования очереди обновлений в БД */
static void * pcrf_client_operate_refreshqueue (void *p_pvArg)
{
	int iFnRes;
	struct timeval soCurTime;
	struct timespec soWaitTime;
	otl_connect *pcoDBConn = NULL;

  /* suppress compiler warning */
  p_pvArg = p_pvArg;

	/* запрашиваем текущее время */
	CHECK_POSIX_DO (gettimeofday (&soCurTime, NULL), return NULL);
	/* задаем время завершения ожидания семафора */
	soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL);
	soWaitTime.tv_nsec = 0;
	/* очередь сессий на обновление */
	std::vector<SRefQueue> vectQueue;
	std::vector<SRefQueue>::iterator iter;
  std::list<SSessionInfo>::iterator iterLocalQueue;
  std::list<SSessionInfo>::iterator iterLocalQueueLast;
  size_t stLocalQueueCnt;
  size_t stCnt;

	while (! g_iStop) {
		/* в рабочем режиме мьютекс всегда будет находиться в заблокированном состоянии и обработка будет запускаться по истечению таймаута */
		/* для завершения работы потока мьютекс принудительно разблокируется чтобы не дожидаться истечения таймаута */
		iFnRes = pthread_mutex_timedlock (&g_tDBReqMutex, &soWaitTime);
		/* если пора завершать работу выходим из цикла */
		if (g_iStop) {
			break;
		}

		/* если ошибка не связана с таймаутом завершаем цикл */
		if (ETIMEDOUT != iFnRes) {
			break;
		}

		/* задаем время следующего запуска */
		/* запрашиваем текущее время */
		gettimeofday (&soCurTime, NULL);
		/* задаем время завершения ожидания семафора */
		soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL);
		soWaitTime.tv_nsec = 0;

		/* запрашиваем подключение к БД */
		if (pcrf_db_pool_get((void**)&(pcoDBConn), __FUNCTION__))
			continue;
		/* создаем список обновления политик */
		CHECK_POSIX_DO(pcrf_client_db_refqueue((*pcoDBConn), vectQueue), goto clear_and_continue);

		for (iter = vectQueue.begin(); iter != vectQueue.end(); ++iter) {
			/* обрабатыаем запись очереди обновлений политик */
			CHECK_POSIX_DO (pcrf_client_operate_refqueue_record (pcoDBConn, *iter), continue);
			CHECK_POSIX_DO (pcrf_client_db_delete_refqueue (*(pcoDBConn), *iter), continue);
		}

    /* обрабатываем локальную очередь на завершение сессий */
    /* получаем какое количество элементов находится в очереди на момент запуска обработки */
    CHECK_POSIX_DO(pthread_mutex_lock(&g_tLocalQueueMutex), goto clear_and_continue);
    stLocalQueueCnt = g_listLocalRefQueue.size();
    CHECK_POSIX_DO(pthread_mutex_unlock(&g_tLocalQueueMutex), /* void */);
    /* обходим все элементы очереди до заданного на момент исполнения количества элементов */
    iterLocalQueue = g_listLocalRefQueue.begin();
    iterLocalQueueLast = iterLocalQueue;
    for (stCnt = 0; stCnt < stLocalQueueCnt &&iterLocalQueueLast != g_listLocalRefQueue.end(); ++stCnt, ++iterLocalQueueLast) {
      pcrf_client_RAR_W_SRCause(*iterLocalQueueLast);
    }
    /* очищаем локальную очередь */
    CHECK_POSIX_DO(pthread_mutex_lock(&g_tLocalQueueMutex), goto clear_and_continue);
    g_listLocalRefQueue.erase(iterLocalQueue, iterLocalQueueLast);
    CHECK_POSIX_DO(pthread_mutex_unlock(&g_tLocalQueueMutex), /* void */ );

		clear_and_continue:
		vectQueue.clear();
		/* если мы получили в распоряжение подключение к БД его надо освободить */
		if (pcoDBConn) {
			pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
			pcoDBConn = NULL;
		}
	}

	pthread_exit (0);
}

void pcrf_local_refresh_queue_add(SSessionInfo &p_soSessionInfo)
{
  CTimeMeasurer coTM;
  SStat *psoStat = stat_get_branch("gx client");
  CHECK_POSIX_DO(pthread_mutex_lock(&g_tLocalQueueMutex), return);
  g_listLocalRefQueue.push_back(p_soSessionInfo);
  CHECK_POSIX_DO(pthread_mutex_unlock(&g_tLocalQueueMutex), /* void */);
  stat_measure(psoStat, __FUNCTION__, &coTM);
}

/* инициализация клиента */
int pcrf_cli_init (void)
{
	/* создания списка сессий */
	CHECK_FCT (fd_sess_handler_create (&g_psoSessionHandler, sess_state_cleanup, NULL, NULL));

  /* если очередь обновления политик не обрабатывается */
  if (0 == g_psoConf->m_iOperateRefreshQueue) {
    return 0;
  }

  /* инициализация мьютекса обращения к БД */
	CHECK_POSIX (pthread_mutex_init (&g_tDBReqMutex, NULL));
	/* блокируем мьютекс чтобы перевести создаваемый ниже поток в состояние ожидания */
	CHECK_POSIX (pthread_mutex_lock (&g_tDBReqMutex));

  /* мьютекс локальной очереди обновленя политик */
	CHECK_POSIX (pthread_mutex_init (&g_tLocalQueueMutex, NULL));

	/* запуск потока для выполнения запросов к БД */
	CHECK_POSIX (pthread_create (&g_tThreadId, NULL, pcrf_client_operate_refreshqueue, NULL));

	return 0;
}

/* деинициализация клиента */
void pcrf_cli_fini (void)
{
	/* освобождение ресурсов, занятых списком созаднных сессий */
	if (g_psoSessionHandler) {
		CHECK_FCT_DO (fd_sess_handler_destroy (&g_psoSessionHandler, NULL), /* continue */ );
	}

  /* если очередь обновления политик не обрабатывается */
  if (0 == g_psoConf->m_iOperateRefreshQueue) {
    return;
  }

	/* останавливаем поток обработки запрсов к БД */
	/* устанавливаем флаг завершения работы потока*/
	g_iStop = 1;
	/* отпускаем мьютекс */
	CHECK_POSIX_DO (pthread_mutex_unlock (&g_tDBReqMutex), /* void */ );
	/* ждем окончания работы потока */
	CHECK_POSIX_DO (pthread_join (g_tThreadId, NULL), /* void */ );
  /* освобождаем мьютекс локальной очереди обновления политик */
	CHECK_POSIX_DO (pthread_mutex_destroy (&g_tLocalQueueMutex), /* void */ );
	/* освобождение ресурсов, занятых мьютексом */
	CHECK_POSIX_DO (pthread_mutex_destroy (&g_tDBReqMutex), /* void */ );
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque)
{
  UTL_LOG_D(*g_pcoLog, "%p:%p:%p", state, sid, opaque);

  /* suppress compiler warning */
  sid = sid; opaque = opaque;

	if (state->m_pszSessionId) {
		free (state->m_pszSessionId);
		state->m_pszSessionId = NULL;
	}
	if (state) {
		free (state);
	}
}
