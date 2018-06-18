#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <list>
#include <unordered_map>

extern CLog *g_pcoLog;

static SStat *g_psoGxClientStat;

/* длительность интервала опроса БД по умолчанию */
#define DB_REQ_INTERVAL 1

static session_handler *g_psoSessionHandler;
static pthread_mutex_t g_tDBReqMutex;
static pthread_t g_tThreadId;
static int g_iStop;

struct sess_state {
  SRARResult *m_psoRARResult;
  sess_state() : m_psoRARResult(NULL) {}
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque);

static pthread_mutex_t g_tLocalQueueMutex;
static std::unordered_map<std::string, uint32_t> g_mapLocalRefreshQueue;

/* функция для обработки просроченного запроса */
static void pcrf_client_req_expire (void *p_pvData, DiamId_t p_pDiamId, size_t p_stDiamIdLen, msg **p_ppMsg)
{
  std::string strDiamId;
  std::string strSessionId;
  sess_state  *psoSessState = NULL;
  session     *psoSess      = NULL;
  os0_t       pszSessId     = NULL;
  size_t      stSessIdLen   = 0;

  /* получаем сохраненное состояние сессии */
  if (NULL != *p_ppMsg) {
    int iIsNew;

    /* получаем дескриптор о сессии */
    CHECK_FCT_DO(fd_msg_sess_get(fd_g_config->cnf_dict, *p_ppMsg, &psoSess, &iIsNew), goto clean_and_exit );

    /* получаем идентификатор сессии */
    CHECK_FCT_DO(fd_sess_getsid(psoSess, &pszSessId, &stSessIdLen), goto clean_and_exit);
    strSessionId.insert(0, reinterpret_cast<char*>(pszSessId), stSessIdLen);

    /* получаем данные, ассоциированные с сессией */
    psoSessState = reinterpret_cast<sess_state*>(p_pvData);
  }

  if (NULL != psoSessState && NULL != psoSessState->m_psoRARResult && psoSessState->m_psoRARResult->m_bInit) {
    psoSessState->m_psoRARResult->m_iResultCode = ETIMEDOUT;
    pthread_mutex_unlock(&psoSessState->m_psoRARResult->m_mutexWait);
  }

  /* готовимся к формированию корректной c-style строки */
  if (NULL != p_pDiamId && 0 != p_stDiamIdLen) {
    strDiamId.insert(0, reinterpret_cast<char*>(p_pDiamId), p_stDiamIdLen);
  }
  /* сообщаем об истечении таймаута */
  LOG_E("request expired: peer: '%s'; session-id: '%s'", strDiamId.c_str(), strSessionId.c_str());

  clean_and_exit:
  /* Free the message */
  if (NULL != *p_ppMsg) {
    CHECK_FCT_DO(fd_msg_free(*p_ppMsg), /*continue*/);
    *p_ppMsg = NULL;
  }

  return;
}

/* получение ответа на Re-Auth/Abort-Session сообщение */
static void pcrf_client_raa (void *p_pData, struct msg **p_ppMsg)
{
  CTimeMeasurer     coTM;
	struct sess_state *psoMsgState = NULL;
	struct session    *psoSess = NULL;
  std::string       strSessionId;
  os0_t             pszSessId = NULL;
  size_t            stSessIdLen = 0;
	int               iRC;

	/* Search the session, retrieve its data */
	if (NULL != *p_ppMsg) {
		int iIsNew;
		CHECK_FCT_DO (fd_msg_sess_get (fd_g_config->cnf_dict, *p_ppMsg, &psoSess, &iIsNew), goto clean_and_exit );

    /* получаем идентификатор сессии */
    CHECK_FCT_DO(fd_sess_getsid(psoSess, &pszSessId, &stSessIdLen), goto clean_and_exit);
    strSessionId.insert(0, reinterpret_cast<char*>(pszSessId), stSessIdLen);

    psoMsgState = reinterpret_cast<sess_state*>(p_pData);
	}

  if (NULL != psoMsgState) {
    LOG_D("message state: Session-Id: '%s'; RAR-Result: '%p'", strSessionId.c_str(), psoMsgState->m_psoRARResult);
  } else {
    LOG_D("message state: <empty pointer>");
  }
  LOG_D("message state: session handler: '%p'; session info: '%p'", g_psoSessionHandler, psoSess);

	/* Value of Result Code */
  {
    struct avp      *psoAVP;
    struct avp_hdr  *psoAVPHdr;

    CHECK_FCT_DO(fd_msg_search_avp(*p_ppMsg, g_psoDictRC, &psoAVP), goto clean_and_exit);
    CHECK_FCT_DO(fd_msg_avp_hdr(psoAVP, &psoAVPHdr), goto clean_and_exit);
    iRC = psoAVPHdr->avp_value->i32;
    LOG_D("Result-Code: '%d'", iRC);
  }

  if (NULL != psoMsgState && NULL != psoMsgState->m_psoRARResult && psoMsgState->m_psoRARResult->m_bInit) {
    psoMsgState->m_psoRARResult->m_iResultCode = iRC;
    CHECK_FCT_DO(pthread_mutex_unlock(&psoMsgState->m_psoRARResult->m_mutexWait), /* continue */);
  }

	/* обрабатываем Result-Code */
	switch (iRC) {
	case 5002: /* DIAMETER_UNKNOWN_SESSION_ID */
    otl_value<std::string> coSessionId( strSessionId );
    pcrf_client_db_fix_staled_sess( coSessionId );
    pcrf_session_cache_remove(strSessionId);
    break;
	}

  clean_and_exit:
	/* Free the message */
	CHECK_FCT_DO(fd_msg_free (*p_ppMsg), return);
	*p_ppMsg = NULL;

	return;
}

/* отправка Re-Auth сообщения */
int pcrf_client_rar (
	SMsgDataForDB p_soReqInfo,
	std::vector<SDBAbonRule> *p_pvectActiveRules,
	std::vector<SDBAbonRule> &p_vectAbonRules,
  std::list<int32_t> *p_plistTrigger,
  SRARResult *p_psoRARRes,
  uint32_t p_uiUsec )
{
	int           iRetVal       = 0;
	CTimeMeasurer coTM;
	msg           *psoReq       = NULL;
	sess_state    *psoMsgState  = NULL, *psoMsgStFnParam = NULL;
	session       *psoSess      = NULL;

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
    avp_value soAVPValue;
    avp *psoAVP;

    CHECK_FCT_DO (iRetVal = fd_sess_fromsid_msg ((uint8_t *)p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str (), p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length (), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(p_soReqInfo.m_psoSessInfo->m_coSessionId.v.data ()));
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_sess_set (psoReq, psoSess), goto out);
	}

	/* Now set all AVPs values */

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (iRetVal = fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
    avp_value soAVPValue;
    avp *psoAVP;

    CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t*>(const_cast<char*>(p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.data()));
		soAVPValue.os.len  = p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
    avp_value soAVPValue;
    avp *psoAVP;

		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t *>(const_cast<char*>(p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.data()));
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

  /* Set the Auth-Application-Id */
  {
    avp_value soAVPValue;
    avp *psoAVP;

    CHECK_FCT_DO(iRetVal = fd_msg_avp_new(g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
    soAVPValue.u32 = 16777238;
    CHECK_FCT_DO(iRetVal = fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto out);
    CHECK_FCT_DO(iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
  }

	/* Set Re-Auth-Request-Type AVP */
	{
    avp_value soAVPValue;
    avp *psoAVP;

		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* AUTHORIZE_ONLY */
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Event-Trigger */
  if ( NULL != p_plistTrigger ) {
    for ( std::list<int32_t>::iterator iter = p_plistTrigger->begin(); iter != p_plistTrigger->end(); ++iter ) {
      CHECK_FCT_DO( set_event_trigger( psoReq, *iter ), /* continue */ );
    }
  }
	/* Charging-Rule-Remove */
  if (NULL != p_pvectActiveRules) {
    avp *psoAVP;

    psoAVP = pcrf_make_CRR( *( p_soReqInfo.m_psoSessInfo ), *p_pvectActiveRules );
    if (psoAVP) {
      /* put 'Charging-Rule-Remove' into request */
      CHECK_FCT_DO(iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP), );
    }
  }

  /* Usage-Monitoring-Information */
  CHECK_POSIX_DO( pcrf_make_UMI( psoReq, *( p_soReqInfo.m_psoSessInfo ) ), /* continue */ );

  /* Charging-Rule-Install */
  {
    avp *psoAVP;

    psoAVP = pcrf_make_CRI( &p_soReqInfo, p_vectAbonRules, psoReq );
    if (psoAVP) {
      /* put 'Charging-Rule-Install' into request */
      CHECK_FCT_DO((iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP)), /* continue */);
    }
  }

  LOG_D("Session-Id: '%s'; Origin-Host: '%s'; Origin-Realm: '%s'",
    p_soReqInfo.m_psoSessInfo->m_coSessionId.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str(),
    p_soReqInfo.m_psoSessInfo->m_coOriginHost.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.c_str(),
    p_soReqInfo.m_psoSessInfo->m_coOriginRealm.is_null() ? "<null>" : p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.c_str());


  /* выделяем память для структуры, хранящей состояние сессии (запроса) */
  if (NULL != p_psoRARRes) {
    psoMsgState = new sess_state;
    psoMsgState->m_psoRARResult = p_psoRARRes;

    LOG_D("message state: Session-Id: '%s'; RAR-Result: '%p'", p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str(), psoMsgState->m_psoRARResult);
    LOG_D("message state: session handler: '%p'; session info: '%p'", g_psoSessionHandler, psoSess);

    /* Store this value in the session */
    CHECK_FCT_DO((iRetVal = fd_sess_state_store(g_psoSessionHandler, psoSess, &psoMsgState)), delete psoMsgState; psoMsgState = NULL; goto out);

    LOG_D("message state: '%p'", psoMsgState);
    psoMsgStFnParam = psoMsgState;
  }

	/* Send the request */
  if (0 == p_uiUsec) {
    CHECK_FCT_DO((iRetVal = fd_msg_send(&psoReq, pcrf_client_raa, psoMsgStFnParam)), goto out);
  } else {
    timespec soTimeSpec;

    CHECK_FCT_DO((iRetVal = pcrf_make_timespec_timeout(soTimeSpec, 0, p_uiUsec)), goto out);
    CHECK_FCT_DO((iRetVal = fd_msg_send_timeout(&psoReq, pcrf_client_raa, psoMsgStFnParam, pcrf_client_req_expire, &soTimeSpec)), goto out);
  }

out:
	stat_measure (g_psoGxClientStat, __FUNCTION__, &coTM);

	return iRetVal;
}

/* отправка RAR сообщения, содержащего Session-Release-Cause */
int pcrf_client_rar_w_SRCause (SSessionInfo &p_soSessInfo)
{
  LOG_D( "enter: %s", __FUNCTION__ );

	int           iRetVal     = 0;
  CTimeMeasurer coTM;
  msg           *psoReq     = NULL;
  session       *psoSess    = NULL;

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
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO(fd_sess_fromsid_msg((uint8_t *)p_soSessInfo.m_coSessionId.v.data(), p_soSessInfo.m_coSessionId.v.length(), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *)p_soSessInfo.m_coSessionId.v.data();
		soAVPValue.os.len = p_soSessInfo.m_coSessionId.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO(fd_msg_avp_add(psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO(fd_msg_sess_set(psoReq, psoSess), goto out);
	}

	/* Now set all AVPs values */

	/* Set the Auth-Application-Id */
	{
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
		soAVPValue.u32 = 16777238;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue ), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginHost.v.c_str ();
		soAVPValue.os.len  = p_soSessInfo.m_coOriginHost.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginRealm.v.c_str ();
		soAVPValue.os.len = p_soSessInfo.m_coOriginRealm.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Re-Auth-Request-Type AVP */
	{
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Session-Release-Cause AVP */
	{
    avp           *psoAVP;
    avp_value     soAVPValue;

    CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionReleaseCause, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* UNSPECIFIED_REASON */
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Send the request */
	CHECK_FCT_DO (fd_msg_send (&psoReq, pcrf_client_raa, NULL), goto out);

out:
  stat_measure(g_psoGxClientStat, __FUNCTION__, &coTM);

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}

/* проверка наличия изменений в политиках */
int pcrf_client_is_any_changes(std::vector<SDBAbonRule> &p_vectActive, std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* проверяем наличие активных неактуальных правил */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectActive.begin(); iter != p_vectActive.end(); ++iter) {
		if (!iter->m_bIsRelevant && iter->m_bIsActive) {
			iRetVal = 1;
			break;
		}
	}

	if (iRetVal)
		return iRetVal;

	/* проверяем наличие актуальных неактивных правил */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectAbonRules.begin(); iter != p_vectAbonRules.end(); ++iter) {
		if (!iter->m_bIsActive && iter->m_bIsRelevant) {
			iRetVal = 1;
			break;
		}
	}

	return iRetVal;
}

/* функция обработки записи очереди обновления политик */
static int pcrf_client_operate_refqueue_record( otl_connect *p_pcoDBConn, SRefQueue &p_soRefQueue )
{
  int iRetVal = 0;
  std::vector<std::string> vectSessionList;
  std::list<int32_t> listEventTrigger;

  bool bMKInstalled = false;         /* признак того, что ключи мониторинга были инсталлированы */
  std::vector<SDBAbonRule>::iterator iterAbonRule;

  /* загружаем из БД список сессий абонента */
  CHECK_POSIX( pcrf_client_db_load_session_list( p_pcoDBConn, p_soRefQueue, vectSessionList ) );

  /* обходим все сессии абонента */
  for ( std::vector<std::string>::iterator iterSess = vectSessionList.begin(); iterSess != vectSessionList.end(); ++iterSess ) {
    listEventTrigger.clear();
    /* сведения о сессии */
    SMsgDataForDB soSessInfo;
    /* список правил профиля абонента */
    std::vector<SDBAbonRule> vectAbonRules;
    /* список активных правил абонента */
    std::vector<SDBAbonRule> vectActive;

    /* инициализация структуры хранения данных сообщения */
    CHECK_FCT_DO( pcrf_server_DBstruct_init( &soSessInfo ), goto clear_and_continue );
    /* задаем идентификтор сессии */
    soSessInfo.m_psoSessInfo->m_coSessionId = *iterSess;
    /* загружаем из БД информацию о сессии абонента */
    {
      /* ищем информацию о базовой сессии в кеше */
      if ( 0 != pcrf_session_cache_get( soSessInfo.m_psoSessInfo->m_coSessionId.v, *soSessInfo.m_psoSessInfo, *soSessInfo.m_psoReqInfo ) ) {
        goto clear_and_continue;
      }
      /* необходимо определить диалект хоста */
      CHECK_POSIX_DO( pcrf_peer_dialect( *soSessInfo.m_psoSessInfo ), goto clear_and_continue );
      /* для Procera нам понадобится дополнительная информация */
      if ( GX_PROCERA == soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
        SSessionInfo soUGWSessInfo;
        std::string strUGWSessionId;
        if ( 0 == pcrf_server_find_core_sess_byframedip( soSessInfo.m_psoSessInfo->m_coFramedIPAddress.v, soUGWSessInfo ) && 0 == soUGWSessInfo.m_coSessionId.is_null() ) {
          strUGWSessionId = soUGWSessInfo.m_coSessionId.v;
          /* ищем информацию о базовой сессии в кеше */
          pcrf_session_cache_get( strUGWSessionId, *soSessInfo.m_psoSessInfo, *soSessInfo.m_psoReqInfo );
        }
      }
    }
    /* проверяем, подключен ли пир к freeDiameterd */
    if ( !pcrf_peer_is_connected( *soSessInfo.m_psoSessInfo ) ) {
      iRetVal = ENOTCONN;
      goto clear_and_continue;
    }
    /* если в поле action задано значение abort_session */
    if ( !p_soRefQueue.m_coAction.is_null() && 0 == p_soRefQueue.m_coAction.v.compare( "abort_session" ) ) {
      CHECK_POSIX_DO( pcrf_client_rar_w_SRCause( *( soSessInfo.m_psoSessInfo ) ), );
      goto clear_and_continue;
    }
    /* загружаем из БД правила абонента */
    CHECK_POSIX_DO( pcrf_server_create_abon_rule_list( p_pcoDBConn, soSessInfo, vectAbonRules ), );
    /* если у абонента нет активных политик завершаем его сессию */
    if ( 0 == vectAbonRules.size() ) {
      CHECK_POSIX_DO( pcrf_client_rar_w_SRCause( *( soSessInfo.m_psoSessInfo ) ), );
      goto clear_and_continue;
    }
    /* загружаем список активных правил */
    CHECK_POSIX_DO( pcrf_session_rule_cache_get( soSessInfo.m_psoSessInfo->m_coSessionId.v, vectActive ), /* continue */ );
    /* формируем список неактуальных правил */
    pcrf_server_select_notrelevant_active( vectAbonRules, vectActive );
    /* формируем список ключей мониторинга */
    pcrf_make_mk_list( vectAbonRules, soSessInfo.m_psoSessInfo );
    /* загружаем информацию о мониторинге */
    CHECK_POSIX_DO( pcrf_server_db_monit_key( p_pcoDBConn, *( soSessInfo.m_psoSessInfo ) ), /* continue */ );
    /* проверяем наличие изменений в политиках */
    if ( !pcrf_client_is_any_changes( vectActive, vectAbonRules ) ) {
      UTL_LOG_N( *g_pcoLog, "subscriber_id: '%s'; session_id: '%s': no any changes", soSessInfo.m_psoSessInfo->m_strSubscriberId.c_str(), soSessInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
      goto clear_and_continue;
    }

    /* обходим все правила */
    for ( iterAbonRule = vectAbonRules.begin(); iterAbonRule != vectAbonRules.end(); ++iterAbonRule ) {
      /* проверяем наличие ключей мониторинга */
      if ( ! bMKInstalled && 0 != iterAbonRule->m_vectMonitKey.size() ) {
        bMKInstalled = true;
        break;
      }
    }

    /* готовим список триггеров */
    /* RAT_CHANGE */
    switch ( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
      case GX_HW_UGW:
      case GX_ERICSSN:
        listEventTrigger.push_back( 2 );
        break;
    }
    /* TETHERING_REPORT */
    switch ( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
      case GX_HW_UGW:
      case GX_ERICSSN:
        listEventTrigger.push_back( 101 );
        break;
    }
#if 1
    /* USER_LOCATION_CHANGE */
    switch ( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
      case GX_HW_UGW:
      case GX_ERICSSN:
        listEventTrigger.push_back( 13 );
        break;
    }
#endif
    /* USAGE_REPORT */
    if ( bMKInstalled ) {
      switch ( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
        case GX_HW_UGW:
        case GX_PROCERA:
        case GX_ERICSSN:
          listEventTrigger.push_back( 33 );
          break;
        case GX_CISCO_SCE:
          listEventTrigger.push_back( 26 );
          break;
      }
    }

    /* посылаем RAR-запрос */
    CHECK_POSIX_DO( pcrf_client_rar( soSessInfo, &vectActive, vectAbonRules, &listEventTrigger, NULL, 0 ), /* continue */ );

    /* освобождаем ресуры*/
    clear_and_continue:
    pcrf_server_DBStruct_cleanup( &soSessInfo );
    if ( iRetVal )
      break;
  }

  return iRetVal;
}

/* функция сканирования очереди обновлений в БД */
static void * pcrf_client_operate_refreshqueue( void *p_pvArg )
{
  int iFnRes;
  struct timespec soWaitTime;
  otl_connect *pcoDBConn = NULL;

  /* suppress compiler warning */
  p_pvArg = p_pvArg;

  /* задаем время завершения ожидания семафора */
  pcrf_make_timespec_timeout( soWaitTime, ( g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL ), 0 );

  /* очередь сессий на обновление */
  std::vector<SRefQueue> vectQueue;
  std::vector<SRefQueue>::iterator iter;
  /* локальная очередь сессий на завершение */
  std::unordered_map<std::string, uint32_t>::iterator iterLocalQueue;
  std::list<std::string> listSessionIdList;
  std::list<std::string>::iterator iterSessionIdList;

  while ( ! g_iStop ) {
    /* в рабочем режиме мьютекс всегда будет находиться в заблокированном состоянии и обработка будет запускаться по истечению таймаута */
    /* для завершения работы потока мьютекс принудительно разблокируется чтобы не дожидаться истечения таймаута */
    iFnRes = pthread_mutex_timedlock( &g_tDBReqMutex, &soWaitTime );
    /* если пора завершать работу выходим из цикла */
    if ( g_iStop ) {
      break;
    }

    /* если ошибка не связана с таймаутом завершаем цикл */
    if ( ETIMEDOUT != iFnRes ) {
      break;
    }

    /* задаем время следующего запуска */
    pcrf_make_timespec_timeout( soWaitTime, ( g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL ), 0 );

    /* запрашиваем подключение к БД */
    if ( 0 == pcrf_db_pool_get( &( pcoDBConn ), __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
    } else {
      continue;
    }
    /* создаем список обновления политик */
    CHECK_POSIX_DO( pcrf_client_db_refqueue( pcoDBConn, vectQueue ), goto clear_and_continue );

    for ( iter = vectQueue.begin(); iter != vectQueue.end(); ++iter ) {
      /* обрабатываем запись очереди обновлений политик */
      if ( 0 == pcrf_client_operate_refqueue_record( pcoDBConn, *iter ) ) {
      } else {
        continue;
      }
      if ( 0 == pcrf_client_db_delete_refqueue( pcoDBConn, *iter ) ) {
      } else {
        continue;
      }
    }

    /* обрабатываем локальную очередь на завершение сессий */
    /* блокируем мьютекс */
    CHECK_POSIX_DO( pthread_mutex_lock( &g_tLocalQueueMutex ), goto clear_and_continue );
    /* быстренько копируем все идентификаторы сессий, чтобы не затягивать с блокировкой мьтекса */
    iterLocalQueue = g_mapLocalRefreshQueue.begin();
    while ( iterLocalQueue != g_mapLocalRefreshQueue.end() ) {
      listSessionIdList.push_back( iterLocalQueue->first );
      ++iterLocalQueue;
    }
    /* очищаем локальную очередь */
    g_mapLocalRefreshQueue.clear();
    /* снимаем блокировку мьютекса */
    CHECK_POSIX_DO( pthread_mutex_unlock( &g_tLocalQueueMutex ), /* void */ );

    /* обрабатываем список сессий */
    iterSessionIdList = listSessionIdList.begin();
    while ( iterSessionIdList != listSessionIdList.end() ) {
      {
        SSessionInfo soSessInfo;
        SRequestInfo soReqInfo;

        if ( 0 == pcrf_session_cache_get( *iterSessionIdList, soSessInfo, soReqInfo ) ) {
          pcrf_client_rar_w_SRCause( soSessInfo );
        }
      }
      ++iterSessionIdList;
    }

    clear_and_continue:
    vectQueue.clear();
    listSessionIdList.clear();

    /* если мы получили в распоряжение подключение к БД его надо освободить */
    if ( NULL != pcoDBConn ) {
      pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
      pcoDBConn = NULL;
    }
  }

  pthread_exit( 0 );
}

void pcrf_local_refresh_queue_add( std::string &p_strSessionId )
{
  CHECK_POSIX_DO(pthread_mutex_lock(&g_tLocalQueueMutex), return);
  g_mapLocalRefreshQueue.insert( std::pair<std::string, uint32_t>( p_strSessionId, 0 ) );
  CHECK_POSIX_DO(pthread_mutex_unlock(&g_tLocalQueueMutex), /* void */);
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

  g_psoGxClientStat = stat_get_branch("gx client");

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
  if (0 != g_tThreadId) {
    CHECK_POSIX_DO(pthread_join(g_tThreadId, NULL), /* void */);
  }
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

  if (NULL != state) {
		delete state;
	}
}
