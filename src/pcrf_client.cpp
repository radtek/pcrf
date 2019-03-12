#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>
#include <list>
#include <unordered_set>

#include "procera/pcrf_procera.h"
#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "pcrf_session_cache.h"

extern CLog *g_pcoLog;
extern uint32_t g_ui32OriginStateId;

static SStat *g_psoGxClientStat;

/* длительность интервала опроса БД по умолчанию */
#define DB_REQ_INTERVAL 1

static session_handler *g_psoSessionHandler;
static pthread_mutex_t g_tDBReqMutex;
static pthread_t g_tThreadId;
static int g_iStop;

struct sess_state {
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque);

static pthread_mutex_t g_tLocalQueueMutex;
static std::multimap<time_t, SRefQueue> g_mmapLocalRefreshQueue;

/* функция для загрузки записей из локальной очереди */
static int pcrf_client_load_localQueue_data( std::list<SRefQueue> &p_listQueue );

/* функция для обработки просроченного запроса */
static void pcrf_client_gx_raa_expire (void *p_pvData, DiamId_t p_pDiamId, size_t p_stDiamIdLen, msg **p_ppMsg)
{
  std::string strDiamId;
  std::string strSessionId;
  SRARResult  *psoRARResult = reinterpret_cast< SRARResult* >( p_pvData );
  session     *psoSess      = NULL;
  os0_t       pszSessId     = NULL;
  size_t      stSessIdLen   = 0;

  /* получаем сохраненное состояние сессии */
  if (NULL != *p_ppMsg) {
    int iIsNew;

    /* получаем дескриптор о сессии */
    CHECK_FCT_DO(fd_msg_sess_get(fd_g_config->cnf_dict, *p_ppMsg, &psoSess, &iIsNew), goto clean_and_exit );

    /* получаем идентификатор сессии */
    if ( NULL != psoSess && 0 == iIsNew ) {
      CHECK_FCT_DO( fd_sess_getsid( psoSess, &pszSessId, &stSessIdLen ), goto clean_and_exit );
      strSessionId.insert( 0, reinterpret_cast< char* >( pszSessId ), stSessIdLen );
    }
  }

  if ( psoRARResult ) {
    psoRARResult->m_iResultCode = ETIMEDOUT;
    pthread_mutex_unlock( &psoRARResult->m_mutexWait );
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
    CHECK_FCT_DO( fd_msg_free( *p_ppMsg ), return );
    *p_ppMsg = NULL;
  }

  return;
}

/* получение ответа на Re-Auth сообщение */
static void pcrf_client_gx_raa (void *p_pvData, struct msg **p_ppMsg)
{
  SRARResult *psoRARResult = reinterpret_cast< SRARResult* >( p_pvData );
	struct session    *psoSess = NULL;
  std::string       strSessionId;
  os0_t             pszSessId = NULL;
  size_t            stSessIdLen = 0;
	int               iRC = 0;

	/* Search the session, retrieve its data */
	if (NULL != *p_ppMsg) {
		int iIsNew;
		CHECK_FCT_DO (fd_msg_sess_get (fd_g_config->cnf_dict, *p_ppMsg, &psoSess, &iIsNew), goto clean_and_exit );

    /* получаем идентификатор сессии */
    if ( NULL != psoSess && 0 == iIsNew ) {
      /* запрашиваем идентификатор сессии */
      CHECK_FCT_DO(fd_sess_getsid(psoSess, &pszSessId, &stSessIdLen), goto clean_and_exit);
      strSessionId.insert(0, reinterpret_cast<char*>(pszSessId), stSessIdLen);
    }
	}

	/* Value of Result Code */
  {
    struct avp      *psoAVP;
    struct avp_hdr  *psoAVPHdr;

    CHECK_FCT_DO(fd_msg_search_avp(*p_ppMsg, g_psoDictRC, &psoAVP), goto clean_and_exit);
    CHECK_FCT_DO(fd_msg_avp_hdr(psoAVP, &psoAVPHdr), goto clean_and_exit);
    iRC = psoAVPHdr->avp_value->i32;
    LOG_D("Result-Code: '%d'", iRC);
  }

	/* обрабатываем Result-Code */
	switch (iRC) {
	case 5002: /* DIAMETER_UNKNOWN_SESSION_ID */
    pcrf_client_db_fix_staled_sess( strSessionId );
    pcrf_session_cache_remove(strSessionId);
    break;
	}

  clean_and_exit:
  if ( psoRARResult ) {
    psoRARResult->m_iResultCode = iRC;
    CHECK_FCT_DO( pthread_mutex_unlock( &psoRARResult->m_mutexWait ), /* continue */ );
  }
	/* Free the message */
  if ( NULL != *p_ppMsg ) {
    CHECK_FCT_DO( fd_msg_free( *p_ppMsg ), return );
    *p_ppMsg = NULL;
  }
}

/* отправка Re-Auth сообщения */
int pcrf_client_gx_rar (
  const SSessionInfo *p_psoSessInfo,
  const SRequestInfo *p_psoReqInfo,
	const std::vector<SDBAbonRule> *p_pvectActiveRules,
	const std::list<SDBAbonRule> *p_plistAbonRules,
  const std::list<int32_t> *p_plistTrigger,
  SRARResult *p_psoRARRes,
  const uint32_t p_uiUsec )
{
  if ( NULL != p_psoSessInfo ) {
  } else {
    return EINVAL;
  }

	int           iRetVal       = 0;
	CTimeMeasurer coTM;
	msg           *psoReq       = NULL;
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

    CHECK_FCT_DO (iRetVal = fd_sess_fromsid_msg ((uint8_t *)p_psoSessInfo->m_strSessionId.c_str (), p_psoSessInfo->m_strSessionId.length (), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = const_cast<uint8_t*>(reinterpret_cast<const uint8_t*>(p_psoSessInfo->m_strSessionId.data ()));
		soAVPValue.os.len = p_psoSessInfo->m_strSessionId.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_sess_set (psoReq, psoSess), goto out);
	}

	/* Now set all AVPs values */

	/* Set Origin-Host & Origin-Realm */
  CHECK_FCT_DO( iRetVal = fd_msg_add_origin( psoReq, g_ui32OriginStateId ), goto out );

	/* Set the Destination-Host AVP */
	{
    avp_value soAVPValue;
    avp *psoAVP;

    CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t*>(const_cast<char*>(p_psoSessInfo->m_coOriginHost.v.data()));
		soAVPValue.os.len  = p_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT_DO (iRetVal = fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (iRetVal = fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
    avp_value soAVPValue;
    avp *psoAVP;

		CHECK_FCT_DO (iRetVal = fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = reinterpret_cast<uint8_t *>(const_cast<char*>(p_psoSessInfo->m_coOriginRealm.v.data()));
		soAVPValue.os.len = p_psoSessInfo->m_coOriginRealm.v.length ();
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
    for ( std::list<int32_t>::const_iterator iter = p_plistTrigger->begin(); iter != p_plistTrigger->end(); ++iter ) {
      CHECK_FCT_DO( set_event_trigger( psoReq, *iter ), /* continue */ );
    }
  }
	/* Charging-Rule-Remove */
  if (NULL != p_pvectActiveRules) {
    avp *psoAVP;

    psoAVP = pcrf_make_CRR( p_psoSessInfo, *p_pvectActiveRules );
    if (psoAVP) {
      /* put 'Charging-Rule-Remove' into request */
      CHECK_FCT_DO(iRetVal = fd_msg_avp_add(psoReq, MSG_BRW_LAST_CHILD, psoAVP), );
    }
  }

  /* Usage-Monitoring-Information */
  CHECK_POSIX_DO( pcrf_make_UMI( psoReq, *( p_psoSessInfo ) ), /* continue */ );

  /* Charging-Rule-Install */
  if ( NULL != p_plistAbonRules ) {
    avp *psoAVP;

    psoAVP = pcrf_make_CRI( p_psoSessInfo, p_psoReqInfo, *p_plistAbonRules, psoReq );
    if ( psoAVP ) {
      /* put 'Charging-Rule-Install' into request */
      CHECK_FCT_DO( ( iRetVal = fd_msg_avp_add( psoReq, MSG_BRW_LAST_CHILD, psoAVP ) ), /* continue */ );
    }
  }

  LOG_D("Session-Id: '%s'; Origin-Host: '%s'; Origin-Realm: '%s'",
    p_psoSessInfo->m_strSessionId.c_str(),
    p_psoSessInfo->m_coOriginHost.is_null() ? "<null>" : p_psoSessInfo->m_coOriginHost.v.c_str(),
    p_psoSessInfo->m_coOriginRealm.is_null() ? "<null>" : p_psoSessInfo->m_coOriginRealm.v.c_str());

	/* Send the request */
  if (0 == p_uiUsec) {
    CHECK_FCT_DO((iRetVal = fd_msg_send(&psoReq, pcrf_client_gx_raa, p_psoRARRes )), goto out);
  } else {
    timespec soTimeSpec;

    CHECK_FCT_DO((iRetVal = pcrf_make_timespec_timeout(soTimeSpec, 0, p_uiUsec)), goto out);
    CHECK_FCT_DO((iRetVal = fd_msg_send_timeout(&psoReq, pcrf_client_gx_raa, p_psoRARRes, pcrf_client_gx_raa_expire, &soTimeSpec)), goto out);
  }

out:
	stat_measure (g_psoGxClientStat, __FUNCTION__, &coTM);

	return iRetVal;
}

/* отправка RAR сообщения, содержащего Session-Release-Cause */
int pcrf_client_gx_rar_w_SRCause (SSessionInfo &p_soSessInfo)
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

    CHECK_FCT_DO(fd_sess_fromsid_msg((uint8_t *)p_soSessInfo.m_strSessionId.data(), p_soSessInfo.m_strSessionId.length(), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *)p_soSessInfo.m_strSessionId.data();
		soAVPValue.os.len = p_soSessInfo.m_strSessionId.length();
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
  CHECK_FCT_DO( fd_msg_add_origin( psoReq, g_ui32OriginStateId ), goto out );

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
	CHECK_FCT_DO (fd_msg_send (&psoReq, pcrf_client_gx_raa, NULL), goto out);

out:
  stat_measure(g_psoGxClientStat, __FUNCTION__, &coTM);

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}

/* проверка наличия изменений в политиках */
int pcrf_client_is_any_changes(std::vector<SDBAbonRule> &p_vectActive, std::list<SDBAbonRule> &p_listAbonRules)
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
	for (std::list<SDBAbonRule>::iterator iter = p_listAbonRules.begin(); iter != p_listAbonRules.end(); ++iter) {
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
	std::list<SDBAbonRule>::iterator iterAbonRule;

	/* загружаем из БД список сессий абонента */
	CHECK_POSIX( pcrf_client_db_load_session_list( p_pcoDBConn, p_soRefQueue, vectSessionList ) );

	/* обходим все сессии абонента */
	for( std::vector<std::string>::iterator iterSess = vectSessionList.begin(); iterSess != vectSessionList.end(); ++iterSess ) {
		listEventTrigger.clear();
		/* сведения о сессии */
		SMsgDataForDB soSessInfo;
		/* список правил профиля абонента */
		std::list<SDBAbonRule> listAbonRules;
		/* список активных правил абонента */
		std::vector<SDBAbonRule> vectActive;

		/* инициализация структуры хранения данных сообщения */
		CHECK_FCT_DO( pcrf_server_DBstruct_init( &soSessInfo ), goto clear_and_continue );
		/* задаем идентификтор сессии */
		soSessInfo.m_psoSessInfo->m_strSessionId = *iterSess;
		/* загружаем из БД информацию о сессии абонента */
		{
		  /* ищем информацию о базовой сессии в кеше */
			if( 0 != pcrf_session_cache_get( soSessInfo.m_psoSessInfo->m_strSessionId, soSessInfo.m_psoSessInfo, soSessInfo.m_psoReqInfo, NULL ) ) {
				goto clear_and_continue;
			}
			/* необходимо определить диалект хоста */
			CHECK_POSIX_DO( pcrf_peer_dialect( *soSessInfo.m_psoSessInfo ), goto clear_and_continue );
			/* для Procera нам понадобится дополнительная информация */
			if( GX_PROCERA == soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
				SSessionInfo soUGWSessInfo;
				std::string strUGWSessionId;
				if( 0 == pcrf_server_find_core_sess_byframedip( soSessInfo.m_psoSessInfo->m_coFramedIPAddress.v, soUGWSessInfo ) ) {
					strUGWSessionId = soUGWSessInfo.m_strSessionId;
					/* ищем информацию о базовой сессии в кеше */
					pcrf_session_cache_get( strUGWSessionId, soSessInfo.m_psoSessInfo, soSessInfo.m_psoReqInfo, NULL );
				}
			}
		}
		/* проверяем, подключен ли пир к freeDiameterd */
		if( !pcrf_peer_is_connected( *soSessInfo.m_psoSessInfo ) ) {
			iRetVal = ENOTCONN;
			goto clear_and_continue;
		}
		/* если в поле action задано значение abort_session */
		if( 0 == p_soRefQueue.m_coAction.is_null() && 0 == p_soRefQueue.m_coAction.v.compare( "abort_session" ) ) {
			CHECK_POSIX_DO( pcrf_client_gx_rar_w_SRCause( *( soSessInfo.m_psoSessInfo ) ), );
			goto clear_and_continue;
		}
		if( GX_PROCERA == soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
			pcrf_procera_additional_rules(
				soSessInfo.m_psoSessInfo->m_coIMEI,
				soSessInfo.m_psoSessInfo->m_coCalledStationId,
				soSessInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI,
				soSessInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI,
				listAbonRules );
		}
		/* загружаем из БД правила абонента */
		CHECK_POSIX_DO( pcrf_server_create_abon_rule_list(
			p_pcoDBConn,
			soSessInfo.m_psoSessInfo->m_strSubscriberId,
			soSessInfo.m_psoSessInfo->m_uiPeerDialect,
			soSessInfo.m_psoReqInfo->m_soUserEnvironment.m_coIPCANType,
			soSessInfo.m_psoReqInfo->m_soUserEnvironment.m_coRATType,
			soSessInfo.m_psoSessInfo->m_coCalledStationId,
			soSessInfo.m_psoReqInfo->m_soUserEnvironment.m_coSGSNAddress,
			soSessInfo.m_psoSessInfo->m_coIMEI,
			listAbonRules ), /* continue */ );
		/* если у абонента нет активных политик завершаем его сессию */
		if( 0 == listAbonRules.size() ) {
			CHECK_POSIX_DO( pcrf_client_gx_rar_w_SRCause( *( soSessInfo.m_psoSessInfo ) ), );
			goto clear_and_continue;
		}
		/* загружаем список активных правил */
		CHECK_POSIX_DO( pcrf_session_rule_cache_get( soSessInfo.m_psoSessInfo->m_strSessionId, vectActive ), /* continue */ );
		/* формируем список неактуальных правил */
		pcrf_server_select_notrelevant_active( listAbonRules, vectActive );
		/* формируем список ключей мониторинга */
		pcrf_make_mk_list( listAbonRules, soSessInfo.m_psoSessInfo->m_mapMonitInfo );
		/* загружаем информацию о мониторинге */
		CHECK_POSIX_DO( pcrf_server_db_monit_key( p_pcoDBConn, soSessInfo.m_psoSessInfo->m_strSubscriberId, soSessInfo.m_psoSessInfo->m_mapMonitInfo ), /* continue */ );
		/* проверяем наличие изменений в политиках */
		if( !pcrf_client_is_any_changes( vectActive, listAbonRules ) ) {
			UTL_LOG_N( *g_pcoLog, "subscriber_id: '%s'; session_id: '%s': no any changes", soSessInfo.m_psoSessInfo->m_strSubscriberId.c_str(), soSessInfo.m_psoSessInfo->m_strSessionId.c_str() );
			goto clear_and_continue;
		}

		/* обходим все правила */
		for( iterAbonRule = listAbonRules.begin(); iterAbonRule != listAbonRules.end(); ++iterAbonRule ) {
		  /* проверяем наличие ключей мониторинга */
			if( ! bMKInstalled && 0 != iterAbonRule->m_vectMonitKey.size() ) {
				bMKInstalled = true;
				break;
			}
		}

		/* готовим список триггеров */
		/* RAT_CHANGE */
		switch( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				listEventTrigger.push_back( 2 );
				break;
		}
		/* TETHERING_REPORT */
		switch( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				listEventTrigger.push_back( 101 );
				break;
		}
	#if 1
		/* USER_LOCATION_CHANGE && SGSN_CHANGE */
		switch( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				listEventTrigger.push_back( 13 );
				listEventTrigger.push_back( 0 );
				break;
		}
	#endif
		/* USAGE_REPORT */
		if( bMKInstalled ) {
			switch( soSessInfo.m_psoSessInfo->m_uiPeerDialect ) {
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
		CHECK_POSIX_DO( pcrf_client_gx_rar( soSessInfo.m_psoSessInfo, soSessInfo.m_psoReqInfo, &vectActive, &listAbonRules, &listEventTrigger, NULL, 0 ), /* continue */ );

		/* освобождаем ресуры*/
	clear_and_continue:
		pcrf_server_DBStruct_cleanup( &soSessInfo );
		if( iRetVal )
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

	/* в рабочем режиме мьютекс всегда будет находиться в заблокированном состоянии и обработка будет запускаться по истечению таймаута */
	/* для завершения работы потока мьютекс принудительно разблокируется чтобы не дожидаться истечения таймаута */
	while( ETIMEDOUT == pthread_mutex_timedlock( &g_tDBReqMutex, &soWaitTime ) ) {
		/* задаем время следующего запуска */
		pcrf_make_timespec_timeout( soWaitTime, ( g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL ), 0 );

		/* очередь обновления политик */
		std::list<SRefQueue> listQueue;
		std::list<SRefQueue>::iterator iter;

		/* если внешняя очередь обновления политик обрабатывается */
		if( 0 != g_psoConf->m_iOperateRefreshQueue ) {
			/* запрашиваем подключение к БД */
			if( 0 == pcrf_db_pool_get( &( pcoDBConn ), __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
			  /* создаем список обновления политик */
				CHECK_POSIX_DO( pcrf_client_db_load_refqueue_data( pcoDBConn, listQueue ), goto __clear_and_continue__ );
			}
		}

		/* загружаем данные из локальной очереди обновления политик */
		CHECK_POSIX_DO( pcrf_client_load_localQueue_data( listQueue ), goto __clear_and_continue__ );

		for( iter = listQueue.begin(); iter != listQueue.end() && 0 == g_iStop; ++iter ) {
		  /* обрабатываем запись очереди обновлений политик */
			if( 0 == pcrf_client_operate_refqueue_record( pcoDBConn, *iter ) ) {
				pcrf_client_db_delete_refqueue( pcoDBConn, *iter );
			}
		}

	__clear_and_continue__:
	/* очищаем вектор */
		listQueue.clear();
		/* если мы получили в распоряжение подключение к БД его надо освободить */
		if( NULL != pcoDBConn ) {
			pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
			pcoDBConn = NULL;
		}
	}

	pthread_exit( 0 );
}

void pcrf_local_refresh_queue_add( const time_t &p_tmTime, const std::string &p_strIdentifier, const char *p_pszIdentifierType, const char *p_pszAction )
{
	SRefQueue soRefQueue( p_strIdentifier, p_pszIdentifierType, p_pszAction );

	CHECK_POSIX_DO( pthread_mutex_lock( &g_tLocalQueueMutex ), return );

	g_mmapLocalRefreshQueue.insert( std::pair<time_t, SRefQueue>( p_tmTime, soRefQueue ) );

	UTL_LOG_D(
		*g_pcoLog,
		"placed in local refresh queue: '%u'; '%s'; '%s'; '%s'",
		p_tmTime, p_strIdentifier.c_str(), p_pszIdentifierType, ( NULL == p_pszAction ) ? "<null>" : p_pszAction );

	CHECK_POSIX_DO( pthread_mutex_unlock( &g_tLocalQueueMutex ), /* void */ );
}

/* инициализация клиента */
int pcrf_cli_init (void)
{
	/* создания списка сессий */
	CHECK_FCT (fd_sess_handler_create (&g_psoSessionHandler, sess_state_cleanup, NULL, NULL));

  /* мьютекс локальной очереди обновления политик */
	CHECK_POSIX (pthread_mutex_init (&g_tLocalQueueMutex, NULL));

  /* инициализация мьютекса обращения к БД */
	CHECK_POSIX (pthread_mutex_init (&g_tDBReqMutex, NULL));
	/* блокируем мьютекс чтобы перевести создаваемый ниже поток в состояние ожидания */
	CHECK_POSIX (pthread_mutex_lock (&g_tDBReqMutex));

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
  /* освобождаем мьютекс локальной очереди обновления политик */
	CHECK_POSIX_DO (pthread_mutex_destroy (&g_tLocalQueueMutex), /* void */ );

	/* останавливаем поток обработки запрсов к БД */
	/* устанавливаем флаг завершения работы потока*/
	g_iStop = 1;
	/* отпускаем мьютекс */
	CHECK_POSIX_DO (pthread_mutex_unlock (&g_tDBReqMutex), /* void */ );
	/* ждем окончания работы потока */
  if (0 != g_tThreadId) {
    CHECK_POSIX_DO(pthread_join(g_tThreadId, NULL), /* void */);
  }
	/* освобождение ресурсов, занятых мьютексом */
	CHECK_POSIX_DO (pthread_mutex_destroy (&g_tDBReqMutex), /* void */ );
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque)
{
  /* suppress compiler warning */
  sid = sid; opaque = opaque;

  if (NULL != state) {
		delete state;
	}
}

SRefQueue::SRefQueue()
{
}

SRefQueue::SRefQueue( const std::string &p_strIdentifier, const char *p_pszIdentifierType, const char *p_pszAction ) :
	m_strIdentifier( p_strIdentifier ),
	m_strIdentifierType( ( NULL != p_pszIdentifierType ) ? p_pszIdentifierType : "" )
{
	m_coAction.v.assign( ( NULL != p_pszAction ) ? p_pszAction : "" );
	if( 0 != m_coAction.v.length() ) {
		m_coAction.set_non_null();
	}
}

static int pcrf_client_load_localQueue_data( std::list<SRefQueue> &p_listQueue )
{
	int iRetVal = 0;
	std::multimap<time_t, SRefQueue>::iterator iterLocalQueueUpper;
	std::multimap<time_t, SRefQueue>::iterator iterLocalQueue;

	/* блокируем мьютекс */
	CHECK_POSIX( pthread_mutex_lock( &g_tLocalQueueMutex ) );
	/* быстренько копируем все идентификаторы сессий, чтобы не затягивать с блокировкой мьтекса */
	iterLocalQueueUpper = g_mmapLocalRefreshQueue.upper_bound( time( NULL ) );
	iterLocalQueue = g_mmapLocalRefreshQueue.begin();
	while( iterLocalQueue != iterLocalQueueUpper ) {
		p_listQueue.push_back( iterLocalQueue->second );
		++iterLocalQueue;
	}
	/* очищаем локальную очередь */
	g_mmapLocalRefreshQueue.erase( g_mmapLocalRefreshQueue.begin(), iterLocalQueueUpper );
	/* снимаем блокировку мьютекса */
	CHECK_POSIX_DO( pthread_mutex_unlock( &g_tLocalQueueMutex ), /* void */ );

	return iRetVal;
}
