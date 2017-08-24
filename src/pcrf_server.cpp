#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <vector>
#include <stdio.h>

CLog *g_pcoLog = NULL;

/* handler for CCR req cb */
static disp_hdl * app_pcrf_hdl_ccr = NULL;

/* функция формирования avp 'QoS-Information' */
avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Charging-Rule-Definition */
avp * pcrf_make_CRD (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Supported-Features */
avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo);

/* функция заполнения Subscription-Id */
int pcrf_make_SI(msg *p_psoMsg, SMsgDataForDB &p_soReqInfo);

/* функция заполнения APN-Aggregate-Max-Bitrate */
int pcrf_make_APNAMBR(msg *p_psoMsg, SRequestInfo &p_soReqInfo);

/* функция заполнения Default-EPS-Bearer-QoS */
int pcrf_make_DefaultEPSBearerQoS(msg *p_psoMsg, SRequestInfo &p_soReqInfo);

/* функция заполнения avp X-HW-Usage-Report */
avp * pcrf_make_HWUR ();

/* выборка идентификатора абонента */
int pcrf_extract_SubscriptionId(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка данных об устройстве абонента */
int pcrf_extract_UEI(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка рапорта о назначении политик */
int pcrf_extract_CRR (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Supported-Features */
int pcrf_extract_SF (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Usage-Monitoring-Information */
int pcrf_extract_UMI (avp *p_psoAVP, SRequestInfo &p_soReqInfo);
/* выборка значений Used-Service-Unit */
int pcrf_extract_USU (avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo);
/* парсинг Default-EPS-Bearer-QoS */
int pcrf_extract_DefaultEPSBearerQoS (avp *p_soAVPValue, SRequestInfo &p_soReqInfo);
/* парсинг 3GPP-User-Location-Info */
int pcrf_parse_user_location (avp_value &p_soAVPValue, SUserLocationInfo &p_soUserLocationInfo);
/* парсинг RAI */
int pcrf_parse_RAI(avp_value &p_soAVPValue, otl_value<std::string> &p_coValue);
/* посылает команду на изменение локации пользователя на сервер Procera */
int pcrf_procera_change_uli (otl_connect *p_pcoDBConn, SMsgDataForDB &p_soReqData);
/* добавляет в ответ Subscription-Id для Procera */
int pcrf_procera_make_subscription_id (msg *p_soAns, otl_value<std::string> &p_coEndUserIMSI, otl_value<std::string> &p_coEndUserE164);
/* формирование запроса на завершение сессии Procera */
int pcrf_procera_terminate_session( otl_value<std::string> &p_coUGWSessionId );
/* формирование запроса на завершение сессии Procera */
int pcrf_procera_oper_thetering_report( SMsgDataForDB &p_soRequestInfo, std::vector<SDBAbonRule> &p_vectAbonRule, std::vector<SDBAbonRule> &p_vectActiveRule );

/* определение набора необходимых действий при обработке CCR */
#define ACTION_COPY_DEFBEARER           static_cast<unsigned int>(0x00000001)
#define ACTION_UPDATE_SESSIONCACHE      static_cast<unsigned int>(0x00000002)
#define ACTION_UPDATE_RULE              static_cast<unsigned int>(0x00000004)
#define ACTION_UPDATE_QUOTA             static_cast<unsigned int>(0x00000008)

#define ACTION_UGW_STORE_THET_INFO      static_cast<unsigned int>(0x00000010)

#define ACTION_PROCERA_STORE_THET_INFO  static_cast<unsigned int>(0x00000020)
#define ACTION_PROCERA_CHANGE_ULI       static_cast<unsigned int>(0x00000040)

unsigned int pcrf_server_determine_action_set( SMsgDataForDB &p_soMsgInfoCache );

static std::map<std::string,int32_t> *g_pmapTethering = NULL;

/* указатели на объекты учета статистики */
SStat *g_psoDBStat;
SStat *g_psoGxSesrverStat;

static int app_pcrf_ccr_cb(
  msg ** p_ppsoMsg,
  avp * p_psoAVP,
  session * p_psoSess,
  void * opaque,
  enum disp_action * p_pAct )
{
  /* проверка входных параметров */
  if ( NULL != p_ppsoMsg ) {
  } else {
    return EINVAL;
  }

  /* suppress compiler warning */
  p_psoAVP = p_psoAVP; p_psoSess = p_psoSess; opaque = opaque; p_pAct = p_pAct;

  CTimeMeasurer coTM;

  SMsgDataForDB soMsgInfoCache;
  unsigned int uiActionSet;

  unsigned int uiResultCode = 2001; /* DIAMETER_SUCCESS */
  std::string *pstrUgwSessionId = NULL;

  /* инициализация структуры хранения данных сообщения */
  CHECK_POSIX_DO( pcrf_server_DBstruct_init( &soMsgInfoCache ), /*continue*/ );

  /* выбираем данные из сообщения */
  msg_or_avp *pMsgOrAVP = *p_ppsoMsg;
  pcrf_extract_req_data( pMsgOrAVP, &soMsgInfoCache );

  /* необходимо определить диалект хоста */
  CHECK_POSIX_DO( pcrf_peer_dialect( *soMsgInfoCache.m_psoSessInfo ), /*continue*/ );

  /* определяем набор действий, необходимых для формирования ответа */
  uiActionSet = pcrf_server_determine_action_set( soMsgInfoCache );
  LOG_D( "Session-Id: %s: Action Set: %#x", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str(), uiActionSet );

  UTL_LOG_D( *g_pcoLog, "session-id: '%s'; peer: '%s@%s'; dialect: %u",
    soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str(),
    soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.c_str(),
    soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.c_str(),
    soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );

  if ( GX_CISCO_SCE != soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
  } else {	/* Gx Cisco SCE */
    /* исправляем косяки циски */
    /* переносим значение E164 на IMSI */
    if ( soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI.is_null() && !soMsgInfoCache.m_psoSessInfo->m_coEndUserE164.is_null() ) {
      soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI = soMsgInfoCache.m_psoSessInfo->m_coEndUserE164;
      soMsgInfoCache.m_psoSessInfo->m_coEndUserE164.v.clear();
      soMsgInfoCache.m_psoSessInfo->m_coEndUserE164.set_null();
    }
    /* отсекаем суффикс от IMSI */
    if ( !soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI.is_null() ) {
      if ( soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI.v.length() > 15 ) {
        size_t stPos;
        stPos = soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI.v.find( "_" );
        if ( stPos != std::string::npos )
          soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI.v.resize( stPos );
      }
    }
  }

  int iFnRes;
  otl_connect *pcoDBConn = NULL;
  std::vector<SDBAbonRule> vectAbonRules;    /* список правил профиля абонента */
  std::vector<SDBAbonRule> vectActive;       /* список активных правил абонента */
  SSessionInfo *psoSessShouldBeTerm = NULL;

  bool bRulesChanged = false;        /* признак того, что правила были изменены */
  bool bMKInstalled = false;         /* признак того, что ключи мониторинга были инсталлированы */
  std::vector<SDBAbonRule>::iterator iterAbonRule;

  /* запрашиваем объект класса для работы с БД */
  /* взаимодействие с БД необходимо лишь в случае INITIAL_REQUEST и UPDATE_REQUEST в сочетании с ACTION_UPDATE_RULE */
  /* так же для всех запросов (временно), поступающих от клиентов с пропиетарными диалектами */
  if ( ( uiActionSet & ACTION_UPDATE_RULE )
    || ( uiActionSet & ACTION_UPDATE_QUOTA )
    || INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType
    || GX_3GPP != soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect )
  {
    iFnRes = pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC );
    if ( 0 == iFnRes && NULL != pcoDBConn ) {
    } else {
      if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect && UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
        /* попытка оптимизации взаимодействия с ugw */
      } else {
        uiResultCode = 3004; /* DIAMETER_TOO_BUSY */
      }
    }
    LOG_D( "Session-Id: %s: Action DB connection is requested", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
  }

  /* дополняем данные запроса необходимыми параметрами */
  switch ( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
    case INITIAL_REQUEST: /* INITIAL_REQUEST */
      if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        /* загружаем идентификтор абонента из профиля абонента */
        pcrf_server_db_load_subscriber_id( pcoDBConn, soMsgInfoCache );
        /* проверка наличия зависших сессий */
        if ( g_psoConf->m_iLook4StalledSession ) {
          CHECK_POSIX_DO( pcrf_server_db_look4stalledsession( pcoDBConn, soMsgInfoCache.m_psoSessInfo ), /*continue*/ );
        }
      } else if ( GX_CISCO_SCE == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        /* загружаем идентификтор абонента из профиля абонента */
        pcrf_server_db_load_subscriber_id( pcoDBConn, soMsgInfoCache );
        pstrUgwSessionId = new std::string;
        if ( 0 == pcrf_server_find_ugw_session( pcoDBConn, soMsgInfoCache.m_psoSessInfo->m_strSubscriberId, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, *pstrUgwSessionId ) ) {
          /* ищем сведения о сессии в кеше */
          pcrf_session_cache_get( *pstrUgwSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
        } else {
          delete pstrUgwSessionId;
          pstrUgwSessionId = NULL;
        }
      } else if ( GX_PROCERA == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        SSessionInfo soSessInfo;
        /* загрузка данных сессии UGW для обслуживания запроса Procera */
        pstrUgwSessionId = new std::string;
        if ( 0 == pcrf_server_find_ugw_sess_byframedip( pcoDBConn, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, soSessInfo ) && 0 == soSessInfo.m_coSessionId.is_null() ) {
          *pstrUgwSessionId = soSessInfo.m_coSessionId.v;
          pcrf_session_cache_get( *pstrUgwSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
        } else {
          delete pstrUgwSessionId;
          pstrUgwSessionId = NULL;
          uiResultCode = 5030; /* USER_UNKNOWN */
          goto answer;
        }
      } else {
        UTL_LOG_E( *g_pcoLog, "unsupported peer dialect: '%u'", soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );
      }
      pcrf_session_cache_insert( soMsgInfoCache.m_psoSessInfo->m_coSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo, pstrUgwSessionId );
      break;/* INITIAL_REQUEST */
    case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
      pcrf_fill_otl_datetime( soMsgInfoCache.m_psoSessInfo->m_coTimeEnd, NULL );
      /* для Procera инициируем завершение сессии в том случае, когда завершена сессия на ugw */
      if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        pcrf_procera_terminate_session( soMsgInfoCache.m_psoSessInfo->m_coSessionId );
      }
      /* если необходимо писать cdr */
      if ( 0 != g_psoConf->m_iGenerateCDR && GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        /* запрашиваем сведения о сессии из кэша */
        pcrf_session_cache_get( soMsgInfoCache.m_psoSessInfo->m_coSessionId.v, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
      }
      pcrf_session_cache_remove( soMsgInfoCache.m_psoSessInfo->m_coSessionId.v );
      break;  /* TERMINATION_REQUEST */
    case UPDATE_REQUEST: /* UPDATE_REQUEST */
    {
      int iSessNotFound;
      /* загружаем идентификатор абонента из списка активных сессий абонента */
      iSessNotFound = pcrf_session_cache_get( soMsgInfoCache.m_psoSessInfo->m_coSessionId.v, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
      /* загрузка данных сессии UGW для обслуживания запроса SCE */
      if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        if ( 0 == iSessNotFound ) {
        } else {
          /* если сессия ugw не найдена просим завершить ее. таким образом избавляемся от сессий, неизвестных pcrf */
          psoSessShouldBeTerm = new SSessionInfo;
          psoSessShouldBeTerm->m_coSessionId = soMsgInfoCache.m_psoSessInfo->m_coSessionId.v;
          psoSessShouldBeTerm->m_coOriginHost = soMsgInfoCache.m_psoSessInfo->m_coOriginHost;
          psoSessShouldBeTerm->m_coOriginRealm = soMsgInfoCache.m_psoSessInfo->m_coOriginRealm;
        }
      } else if ( GX_CISCO_SCE == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        pstrUgwSessionId = new std::string;
        /* ищем базовую сессию ugw */
        if ( 0 == pcrf_server_find_ugw_session( pcoDBConn, soMsgInfoCache.m_psoSessInfo->m_strSubscriberId, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, *pstrUgwSessionId ) ) {
          /* ищем информацию о базовой сессии в кеше */
          pcrf_session_cache_get( *pstrUgwSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
        } else {
          delete pstrUgwSessionId;
          pstrUgwSessionId = NULL;
          uiResultCode = 5030; /* USER_UNKNOWN */
          goto answer;
        }
      } else if ( GX_PROCERA == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        SSessionInfo soSessInfo;
        pstrUgwSessionId = new std::string;
        if ( 0 == pcrf_server_find_ugw_sess_byframedip( pcoDBConn, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, soSessInfo ) && 0 == soSessInfo.m_coSessionId.is_null() ) {
          *pstrUgwSessionId = soSessInfo.m_coSessionId.v;
          pcrf_session_cache_get( *pstrUgwSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo );
        } else {
          delete pstrUgwSessionId;
          pstrUgwSessionId = NULL;
        }
      } else {
        UTL_LOG_E( *g_pcoLog, "unsupported peer dialect: '%u'", soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );
      }
    }
    break;  /* UPDATE_REQUEST */
    default:  /* DEFAULT */
      break;  /* DEFAULT */
  }

  /* сохраняем в БД запрос */
  pcrf_server_req_db_store( pcoDBConn, &soMsgInfoCache );

  /* если необходимо писать cdr */
  if ( 0 != g_psoConf->m_iGenerateCDR && GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
    pcrf_cdr_write_cdr( soMsgInfoCache );
  }

  /* загружаем правила из БД */
  if ( UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType && ( uiActionSet & ACTION_UPDATE_RULE )
    || INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType
    || GX_3GPP != soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect )
  {
    /* загружаем из БД правила абонента */
    CHECK_POSIX_DO( pcrf_server_create_abon_rule_list( pcoDBConn, soMsgInfoCache, vectAbonRules ), /* continue */ );
    LOG_D( "Session-Id: %s: abon rules information is loaded", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
  }

  answer:

  msg *ans = NULL;
  avp *psoChildAVP = NULL;
  union avp_value soAVPVal;

  /* Create answer header */
  CHECK_FCT_DO( fd_msg_new_answer_from_req( fd_g_config->cnf_dict, p_ppsoMsg, 0 ), goto cleanup_and_exit );
  ans = *p_ppsoMsg;

  /* Auth-Application-Id */
  do {
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAuthApplicationId, 0, &psoChildAVP ), break );
    soAVPVal.u32 = 16777238;
    CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
    CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
  } while ( 0 );

  /* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
  switch ( uiResultCode ) {
    case 2001: /* DIAMETER_SUCCESS */
      CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast<char*>( "DIAMETER_SUCCESS" ), NULL, NULL, 1 ), /*continue*/ );
      break;
    case 3004: /* DIAMETER_TOO_BUSY */
      CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast<char*>( "DIAMETER_TOO_BUSY" ), NULL, NULL, 1 ), /*continue*/ );
      break;
    case 5030: /* USER_UNKNOWN */
      CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast<char*>( "USER_UNKNOWN" ), NULL, NULL, 1 ), /*continue*/ );
      break;
  }

  /* put 'CC-Request-Type' into answer */
  do {
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCRequestType, 0, &psoChildAVP ), break );
    soAVPVal.i32 = soMsgInfoCache.m_psoReqInfo->m_iCCRequestType;
    CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
    CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
  } while ( 0 );
  /* put 'CC-Request-Number' into answer */
  do {
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCRequestNumber, 0, &psoChildAVP ), break );
    soAVPVal.u32 = soMsgInfoCache.m_psoReqInfo->m_coCCRequestNumber.v;
    CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
    CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
  } while ( 0 );
  /* put 'Origin-State-Id' into answer */
  do {
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictOriginStateId, 0, &psoChildAVP ), break );
    soAVPVal.u32 = soMsgInfoCache.m_psoSessInfo->m_coOriginStateId.v;
    CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
    CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
  } while ( 0 );

  switch ( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
    case INITIAL_REQUEST: /* INITIAL_REQUEST */
      /* Supported-Features */
      psoChildAVP = pcrf_make_SF( &soMsgInfoCache );
      if ( psoChildAVP ) {
        /* put 'Supported-Features' into answer */
        CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /* continue */ );
      }
      /* дополняем ответ на CCR-I для Procera информацией о пользователе */
      /* Subscription-Id */
      if ( GX_PROCERA == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        CHECK_FCT_DO( set_event_trigger( ans, 777 ), /* continue */ );
        CHECK_FCT_DO( pcrf_procera_make_subscription_id( ans, soMsgInfoCache.m_psoSessInfo->m_coEndUserIMSI, soMsgInfoCache.m_psoSessInfo->m_coEndUserE164 ), /* continue */ );
      }
      /* формируем список ключей мониторинга */
      pcrf_make_mk_list( vectAbonRules, soMsgInfoCache.m_psoSessInfo );
      /* запрашиваем информацию о ключах мониторинга */
      CHECK_POSIX_DO( pcrf_server_db_monit_key( pcoDBConn, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );
      LOG_D( "Session-Id: %s: monitoring key information is requested", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      /* Usage-Monitoring-Information */
      CHECK_FCT_DO( pcrf_make_UMI( ans, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );
      /* Charging-Rule-Install */
      psoChildAVP = pcrf_make_CRI( &soMsgInfoCache, vectAbonRules, ans );
      /* put 'Charging-Rule-Install' into answer */
      if ( psoChildAVP ) {
        CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
      }
      break; /* INITIAL_REQUEST */
    case UPDATE_REQUEST: /* UPDATE_REQUEST */
      if ( uiActionSet & ACTION_COPY_DEFBEARER ) {
        /* Default-EPS-Bearer-QoS */
        pcrf_make_DefaultEPSBearerQoS( ans, *soMsgInfoCache.m_psoReqInfo );
        CHECK_FCT_DO( pcrf_make_APNAMBR( ans, *soMsgInfoCache.m_psoReqInfo ), /* continue */ );
        LOG_D( "Session-Id: %s: default bearer is accepted", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      if ( uiActionSet & ACTION_UPDATE_SESSIONCACHE ) {
        pcrf_session_cache_insert( soMsgInfoCache.m_psoSessInfo->m_coSessionId, *soMsgInfoCache.m_psoSessInfo, *soMsgInfoCache.m_psoReqInfo, pstrUgwSessionId );
        LOG_D( "Session-Id: %s: session cache is updated", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      if ( uiActionSet & ACTION_PROCERA_CHANGE_ULI ) {
        CHECK_FCT_DO( pcrf_procera_change_uli( pcoDBConn, soMsgInfoCache ), /* continue */ );
        LOG_D( "Session-Id: %s: user location is changed on procera", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      if ( uiActionSet & ACTION_UPDATE_RULE ) {
        /* загружаем список активных правил */
        CHECK_POSIX_DO( pcrf_session_rule_cache_get( soMsgInfoCache.m_psoSessInfo->m_coSessionId.v, vectActive ), /* continue */ );
        /* формируем список неактуальных правил */
        pcrf_server_select_notrelevant_active( vectAbonRules, vectActive );
        /* формируем список ключей мониторинга */
        pcrf_make_mk_list( vectAbonRules , soMsgInfoCache.m_psoSessInfo );
        /* запрашиваем информацию о ключах мониторинга */
        CHECK_POSIX_DO( pcrf_server_db_monit_key( pcoDBConn, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );
        LOG_D( "Session-Id: %s: monitoring key information is requested", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
        /* Charging-Rule-Remove */
        psoChildAVP = pcrf_make_CRR( *soMsgInfoCache.m_psoSessInfo, vectActive );
        /* put 'Charging-Rule-Remove' into answer */
        if ( psoChildAVP ) {
          CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
        }
        /* Usage-Monitoring-Information */
        CHECK_FCT_DO( pcrf_make_UMI( ans, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );
        /* Charging-Rule-Install */
        psoChildAVP = pcrf_make_CRI( &soMsgInfoCache, vectAbonRules, ans );
        /* put 'Charging-Rule-Install' into answer */
        if ( psoChildAVP ) {
          CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
        }
        LOG_D( "Session-Id: %s: session rules are operated", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      if ( uiActionSet & ACTION_UGW_STORE_THET_INFO ) {
        pcrf_server_db_insert_tethering_info( soMsgInfoCache );
        LOG_D( "Session-Id: %s: ugw thetering info is stored", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      if ( uiActionSet & ACTION_PROCERA_STORE_THET_INFO ) {
        if ( 0 == soMsgInfoCache.m_psoSessInfo->m_coSessionId.is_null() ) {
          pcrf_procera_oper_thetering_report( soMsgInfoCache, vectAbonRules, vectActive );
        }
        LOG_D( "Session-Id: %s: procera thetering info is stored", soMsgInfoCache.m_psoSessInfo->m_coSessionId.v.c_str() );
      }
      break; /* UPDATE_REQUEST */
  }

  /* обходим все правила */
  if ( INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType
    || UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType && ( uiActionSet & ACTION_UPDATE_RULE ) ) {
    for ( iterAbonRule = vectAbonRules.begin(); iterAbonRule != vectAbonRules.end(); ++iterAbonRule ) {
      /* если найдены правила, подлежащие инсталляции */
      if ( ! bRulesChanged && ! iterAbonRule->m_bIsActive && iterAbonRule->m_bIsRelevant ) {
        bRulesChanged = true;
      }
      /* среди релевантных правил проверяем наличие ключей мониторинга */
      if ( ! bMKInstalled && iterAbonRule->m_bIsRelevant && 0 != iterAbonRule->m_vectMonitKey.size() ) {
        bMKInstalled = true;
      }
      /* ищем, есть ли правила, подлежащие удалению */
      if ( ! bRulesChanged && iterAbonRule->m_bIsActive && ! iterAbonRule->m_bIsRelevant ) {
        bRulesChanged = true;
      }
      /* если нашли оба признака дальше искать нет смысла */
      if ( bRulesChanged && bMKInstalled ) {
        break;
      }
    }
  }

  /* задаем Event-Trigger */
  if( INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType 
    || UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType && bRulesChanged )
  {
    /* Event-Trigger RAT_CHANGE */
    if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
      CHECK_FCT_DO( set_event_trigger( ans, 2 ), /* continue */ );
    }
    /* Event-Trigger TETHERING_REPORT */
    if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
      CHECK_FCT_DO( set_event_trigger( ans, 101 ), /* continue */ );
    }
#if 0
    /* Event-Trigger USER_LOCATION_CHANGE */
    if ( GX_3GPP == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
      CHECK_FCT_DO( set_event_trigger( ans, 13 ), /* continue */ );
    }
#endif
    /* Event-Trigger USAGE_REPORT */
    if ( bMKInstalled ) {
      switch ( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
        case GX_3GPP:
        case GX_PROCERA:
          CHECK_FCT_DO( set_event_trigger( ans, 33 ), /* continue */ );
          break;
        case GX_CISCO_SCE:
          CHECK_FCT_DO( set_event_trigger( ans, 26 ), /* continue */ );
          break;
      }
    }
  }

  cleanup_and_exit:
  if ( pstrUgwSessionId ) {
    delete pstrUgwSessionId;
    pstrUgwSessionId = NULL;
  }
  /* фиксируем статистику */

  /* освобождаем занятые ресурсы */
  pcrf_server_DBStruct_cleanup( &soMsgInfoCache );

  /* освобождаем объект класса взаимодействия с БД */
  if ( NULL != pcoDBConn ) {
    CHECK_POSIX_DO( pcrf_db_pool_rel( reinterpret_cast<void *>( pcoDBConn ), __FUNCTION__ ), /*continue*/ );
  }

  /* если ответ сформирован отправляем его */
  if ( NULL != ans ) {
    CHECK_FCT_DO( fd_msg_send( p_ppsoMsg, NULL, NULL ), /*continue*/ );
  }

  /* статистика по работе функции */
  stat_measure( g_psoGxSesrverStat, __FUNCTION__, &coTM );

  /* если сессию следует завершить */
  if ( NULL != psoSessShouldBeTerm ) {
    pcrf_local_refresh_queue_add( *psoSessShouldBeTerm );
    delete psoSessShouldBeTerm;
  }

  return 0;
}

int app_pcrf_serv_init (void)
{
	disp_when data;

	TRACE_DEBUG (FULL, "Initializing dispatch callbacks for test");

	memset (&data, 0, sizeof(data));
	data.app = g_psoDictApp;
	data.command = g_psoDictCCR;

	/* Now specific handler for CCR */
	CHECK_FCT (fd_disp_register (app_pcrf_ccr_cb, DISP_HOW_CC, &data, NULL, &app_pcrf_hdl_ccr));

  /* Advertise the support for the Gx application in the peer */
  CHECK_FCT_DO( fd_disp_app_support (g_psoDictApp, g_psoDictVend, 1, 0), /* continue */ );

  /* инициализация объектов статистики */
  g_psoDBStat = stat_get_branch("DB operation");
  g_psoGxSesrverStat = stat_get_branch("gx server");

	return 0;
}

void app_pcrf_serv_fini (void)
{
	if (app_pcrf_hdl_ccr) {
		(void) fd_disp_unregister (&app_pcrf_hdl_ccr, NULL);
	}
}

int pcrf_logger_init(void)
{
	g_pcoLog = new CLog;

	return g_pcoLog->Init (g_psoConf->m_pszLogFileMask);
}

void pcrf_logger_fini(void)
{
	g_pcoLog->Flush();
	delete g_pcoLog;
	g_pcoLog = NULL;
}

void pcrf_server_select_notrelevant_active(std::vector<SDBAbonRule> &p_vectAbonRules, std::vector<SDBAbonRule> &p_vectActive)
{
	/* обходим все активные правила */
	std::vector<SDBAbonRule>::iterator iterRule;
	std::vector<SDBAbonRule>::iterator iterActive;
	/* цикл актуальных правил */
	for (iterRule = p_vectAbonRules.begin(); iterRule != p_vectAbonRules.end(); ++iterRule) {
		/* цикл активных правил */
		for (iterActive = p_vectActive.begin(); iterActive != p_vectActive.end(); ++iterActive) {
			/* если имена правил совпадают, значит активное правило актуально */
			if (iterActive->m_coRuleName.v == iterRule->m_coRuleName.v) {
				/* фиксируем, что правило активировано */
				iterRule->m_bIsActive = true;
				iterActive->m_bIsRelevant = true;
			}
		}
  }
}

avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule)
{
	avp *psoAVPQoSI = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	int32_t i32Value;
	uint32_t ui32Value;

	do {
		/* QoS-Information */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictQoSInformation, 0, &psoAVPQoSI), return NULL);

		/* QoS-Class-Identifier */
		if (! p_soAbonRule.m_coQoSClassIdentifier.is_null ()) {
			// /* TODO #1 */
			// if (!p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.is_null()
			// 	&& 0 == p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.v.compare("GERAN")
			// 	&& !p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.is_null()
			// 	&& (0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250070700308195")
			// 		|| 0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250270100006021"))) {
			// 	i32Value = 8;
			// } else
			// /* TODO #1*/
			i32Value = p_soAbonRule.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictQoSClassIdentifier, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = i32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Max-Requested-Bandwidth-UL */
		if (! p_soAbonRule.m_coMaxRequestedBandwidthUl.is_null ()) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthUl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v ? p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			// /* TODO #1 */
			// if (!p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.is_null()
			// 	&& 0 == p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.v.compare("GERAN")
			// 	&& !p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.is_null()
			// 	&& (0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250070700308195")
			// 		|| 0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250270100006021"))) {
			// 		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateUL, 0, &psoAVPChild), return NULL);
			// } else
			// /* TODO #1*/
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictMaxRequestedBandwidthUL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Max-Requested-Bandwidth-DL */
		if (! p_soAbonRule.m_coMaxRequestedBandwidthDl.is_null ()) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthDl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.v ? p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			// /* TODO #1 */
			// if (!p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.is_null()
			// 	&& 0 == p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType.v.compare("GERAN")
			// 	&& !p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.is_null()
			// 	&& (0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250070700308195")
			// 		|| 0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250270100006021"))) {
			// 		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateDL, 0, &psoAVPChild), return NULL);
			// } else
			// /* TODO #1*/
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictMaxRequestedBandwidthDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Guaranteed-Bitrate-UL */
		if (! p_soAbonRule.m_coGuaranteedBitrateUl.is_null ()) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateUl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.v ? p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			// /* TODO #1*/
			// if (!p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.is_null()
			// 		&& (0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250070700308195")
			// 			|| 0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250270100006021"))) {
			// 	ui32Value = 0;
			// }
			// /* TODO #1*/
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictGuaranteedBitrateUL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Guaranteed-Bitrate-DL */
		if (! p_soAbonRule.m_coGuaranteedBitrateDl.is_null ()) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateDl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.v ? p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			// /* TODO #1*/
			// if (!p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.is_null()
			// 		&& (0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250070700308195")
			// 			|| 0 == p_psoReqInfo->m_psoSessInfo->m_coEndUserIMSI.v.compare("250270100006021"))) {
			// 	ui32Value = 0;
			// }
			// /* TODO #1*/
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictGuaranteedBitrateDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Allocation-Retention-Priority */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAllocationRetentionPriority, 0, &psoAVPParent), return NULL);

		/* Priority-Level */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPriorityLevel, 0, &psoAVPChild), return NULL);
		if (!p_soAbonRule.m_soARP.m_coPriorityLevel.is_null()) {
			soAVPVal.u32 = p_soAbonRule.m_soARP.m_coPriorityLevel.v;
		} else if (!p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.is_null ()
				&& !p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.is_null()
				&& !p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null()) {
			soAVPVal.u32 = p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
		} else {
			soAVPVal.u32 = 2;
		}
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* Pre-emption-Capability */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPreemptionCapability, 0, &psoAVPChild), return NULL);
		if (!p_soAbonRule.m_soARP.m_coPreemptionCapability.is_null ()) {
			soAVPVal.i32 = p_soAbonRule.m_soARP.m_coPreemptionCapability.v;
		} else {
			soAVPVal.i32 = 1; /* по умолчанию задаем 1 */
		}
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* Pre-emption-Vulnerability */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPreemptionVulnerability, 0, &psoAVPChild), return NULL);
		if (!p_soAbonRule.m_soARP.m_coPreemptionVulnerability.is_null ()) {
			soAVPVal.i32 = p_soAbonRule.m_soARP.m_coPreemptionVulnerability.v;
		} else {
			soAVPVal.i32 = 0; /* по умолчанию задаем 0 */
		}
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* put 'Allocation-Retention-Priority' into 'QoS-Information' */
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);

	} while (0);

	return psoAVPQoSI;
}

int pcrf_set_CRN (avp *p_pParent, dict_object *p_psoDictObj, std::string &p_strName)
{
  avp *psoAVPCRN = NULL;
  avp_value soAVPVal;

  CHECK_FCT_DO (fd_msg_avp_new (p_psoDictObj, 0, &psoAVPCRN), return EINVAL);
  soAVPVal.os.data = (uint8_t*) p_strName.c_str ();
  soAVPVal.os.len = p_strName.length ();
  CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPCRN, &soAVPVal), return EINVAL);
  CHECK_FCT_DO (fd_msg_avp_add (p_pParent, MSG_BRW_LAST_CHILD, psoAVPCRN), return EINVAL);

  return 0;
}

avp * pcrf_make_CRR( SSessionInfo &p_soSessInfo, std::vector<SDBAbonRule> &p_vectActive )
{
	/* если список пустой выходим ничего не делая */
	if (0 == p_vectActive.size()) {
		return NULL;
	}

	avp *psoAVPCRR = NULL; /* Charging-Rule-Remove */
	std::vector<SDBAbonRule>::iterator iter = p_vectActive.begin();

	/* обходим все элементы списка */
	for (; iter != p_vectActive.end(); ++iter) {
		/* если правило актуально переходим к другому */
		if (iter->m_bIsRelevant)
			continue;
		switch (p_soSessInfo.m_uiPeerDialect) {
		case GX_3GPP: /* Gx */
    case GX_PROCERA: /* Gx Procera */
            /* Charging-Rule-Remove */
			if (NULL == psoAVPCRR) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleRemove, 0, &psoAVPCRR), return NULL);
			}
			/* если это динамическое правило */
			if (! iter->m_coDynamicRuleFlag.is_null () && iter->m_coDynamicRuleFlag.v) {
				/* Charging-Rule-Name */
        CHECK_FCT_DO (pcrf_set_CRN (psoAVPCRR, g_psoDictChargingRuleName, iter->m_coRuleName.v), continue);
			}
			/* если это предопределенное правило */
			else {
				/* если это групповое правило */
				if (! iter->m_coRuleGroupFlag.is_null () && iter->m_coRuleGroupFlag.v) {
					/* Charging-Rule-Base-Name */
          CHECK_FCT_DO (pcrf_set_CRN (psoAVPCRR, g_psoDictChargingRuleBaseName, iter->m_coRuleName.v), continue);
				} else {
					/* Charging-Rule-Name */
          CHECK_FCT_DO (pcrf_set_CRN (psoAVPCRR, g_psoDictChargingRuleName, iter->m_coRuleName.v), continue);
        }
			}
      pcrf_db_close_session_rule( p_soSessInfo, iter->m_coRuleName.v );
      break; /* Gx */
		case GX_CISCO_SCE: /* Gx Cisco SCE */
      pcrf_db_close_session_rule( p_soSessInfo, iter->m_coRuleName.v );
      break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRR;
}

avp * pcrf_make_CRI (
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	msg *p_soAns)
{
	/* если в списке нет ни одного правила */
	if (0 == p_vectAbonRules.size ()) {
		return NULL;
	}

	CTimeMeasurer coTM;
	avp *psoAVPCRI = NULL; /* Charging-Rule-Install */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
  std::string strValue;

	std::vector<SDBAbonRule>::iterator iter = p_vectAbonRules.begin ();
	/* обходим все правила */
	for (; iter != p_vectAbonRules.end (); ++ iter) {
		/* если првило уже активировано переходим к следующей итерации */
		switch (p_psoReqInfo->m_psoSessInfo->m_uiPeerDialect) {
    case GX_3GPP:     /* Gx */
    case GX_PROCERA: /* Gx Procera */
      if (iter->m_bIsActive) {
				continue;
			}
			/* Charging-Rule-Install */
			/* создаем avp 'Charging-Rule-Install' только по необходимости */
			if (NULL == psoAVPCRI) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleInstall, 0, &psoAVPCRI), return NULL);
				/* Bearer-Identifier */
				if (0 == p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType
						&& ! p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.is_null ()) {
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictBearerIdentifier, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.c_str ();
					soAVPVal.os.len = p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Bearer-Identifier' into 'Charging-Rule-Install' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
			}
			/* Charging-Rule-Definition */
			psoAVPChild = pcrf_make_CRD (p_psoReqInfo, *iter);
			if (psoAVPChild) {
				/* put 'Charging-Rule-Definition' into 'Charging-Rule-Install' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				/* сохраняем выданную политику в БД */
        pcrf_db_insert_rule( *( p_psoReqInfo->m_psoSessInfo ), *iter );
      }
			break; /* Gx */
		case GX_CISCO_SCE: /* Gx Cisco SCE */
			/* Cisco-SCA BB-Package-Install */
			if (! iter->m_coSCE_PackageId.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBPackageInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_PackageId.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Package-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Real-time-monitor-Install */
			if (! iter->m_coSCE_RealTimeMonitor.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBRTMonitorInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_RealTimeMonitor.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Real-time-monitor-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Vlink-Upstream-Install */
			if (! iter->m_coSCE_UpVirtualLink.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBVlinkUStreamInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_UpVirtualLink.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Vlink-Upstream-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Vlink-Downstream-Install */
			if (! iter->m_coSCE_DownVirtualLink.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBVlinkDStreamInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_DownVirtualLink.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Vlink-Downstream-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* сохраняем выданную политику в БД */
      if (! iter->m_bIsActive) {
        pcrf_db_insert_rule( *( p_psoReqInfo->m_psoSessInfo ), *iter );
      }
			break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRI;
}

avp * pcrf_make_CRD (
	SMsgDataForDB *p_psoReqInfo,
	SDBAbonRule &p_soAbonRule)
{
  avp *psoAVPRetVal = NULL;
	avp *psoAVPCRD = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	int iIpCanType;

	/* сохраняем значение IP-CAN-Type в локальной переменной, т.к. оно часто испольуется */
	iIpCanType = p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType;

	/* если это динамическое правило */
	if (! p_soAbonRule.m_coDynamicRuleFlag.is_null () && p_soAbonRule.m_coDynamicRuleFlag.v) {
    /* Charging-Rule-Definition */
    {
	    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleDefinition, 0, &psoAVPCRD), return NULL);
      psoAVPRetVal = psoAVPCRD;
    }
		/* Charging-Rule-Name */
    CHECK_FCT_DO (pcrf_set_CRN (psoAVPCRD, g_psoDictChargingRuleName, p_soAbonRule.m_coRuleName.v), return NULL);
		/* Service-Identifier */
		if (! p_soAbonRule.m_coServiceId.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictServiceIdentifier, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coServiceId.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Rating-Group */
		if (! p_soAbonRule.m_coRatingGroupId.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRatingGroup, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coRatingGroupId.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Flow-Description */
		std::vector<std::string>::iterator iterFD = p_soAbonRule.m_vectFlowDescr.begin ();
		for (; iterFD != p_soAbonRule.m_vectFlowDescr.end (); ++ iterFD) {
			switch (iIpCanType) {
			case 0:
				/* Flow-Description */
				{
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowDescription, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) iterFD->c_str ();
					soAVPVal.os.len  = (size_t) iterFD->length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Flow-Information - in' into 'Charging-Rule-Definition' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
				break;
			case 5:
				/* Flow-Information */
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowInformation, 0, &psoAVPParent), return NULL);
				/* Flow-Description */
				{
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowDescription, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) iterFD->c_str ();
					soAVPVal.os.len  = (size_t) iterFD->length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Flow-Information - in' into 'Flow-Information' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
				/* put 'Flow-Information - in' into 'Charging-Rule-Definition' */
				{
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);
				}
				break;
			}
		}
		/* QoS-Information */
		psoAVPChild = pcrf_make_QoSI (p_psoReqInfo, p_soAbonRule);
		/* put 'QoS-Information' into 'Charging-Rule-Definition' */
		if (psoAVPChild) {
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Online */
		if (! p_soAbonRule.m_coOnlineCharging.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictOnline, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coOnlineCharging.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Offline */
		if (! p_soAbonRule.m_coOfflineCharging.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictOffline, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coOfflineCharging.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Metering-Method */
		if (! p_soAbonRule.m_coMeteringMethod.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMeteringMethod, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coMeteringMethod.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Precedence */
		if (! p_soAbonRule.m_coPrecedenceLevel.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPrecedence, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coPrecedenceLevel.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Monitoring-Key */
		if (0 < p_soAbonRule.m_vectMonitKey.size()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *)p_soAbonRule.m_vectMonitKey[0].data();
			soAVPVal.os.len = (size_t)p_soAbonRule.m_vectMonitKey[0].length();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Redirect-Server */
		if (! p_soAbonRule.m_coRedirectAddressType.is_null () && ! p_soAbonRule.m_coRedirectServerAddress.is_null ()) {
			/* Redirect-Server */
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectServer, 0, &psoAVPParent), return NULL);
			/* Redirect-Address-Type */
			{
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectAddressType, 0, &psoAVPChild), return NULL);
				soAVPVal.i32 = p_soAbonRule.m_coRedirectAddressType.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Redirect-Address-Type' into 'Redirect-Server' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Redirect-Server-Address */
			{
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectServerAddress, 0, &psoAVPChild), return NULL);
				soAVPVal.os.data = (uint8_t *) p_soAbonRule.m_coRedirectServerAddress.v.c_str ();
				soAVPVal.os.len  = (size_t) p_soAbonRule.m_coRedirectServerAddress.v.length ();
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Redirect-Server-Address' into 'Redirect-Server' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* put 'Redirect-Server' into 'Charging-Rule-Definition' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);
		}
	}
	/* если это предопределенное правило */
	else {
		/* если это пакетное правило */
		if (! p_soAbonRule.m_coRuleGroupFlag.is_null () && p_soAbonRule.m_coRuleGroupFlag.v) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleBaseName, 0, &psoAVPChild), return NULL);
		} else {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleName, 0, &psoAVPChild), return NULL);
		}
		soAVPVal.os.data = (uint8_t *) p_soAbonRule.m_coRuleName.v.c_str ();
		soAVPVal.os.len  = (size_t) p_soAbonRule.m_coRuleName.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
    psoAVPRetVal = psoAVPChild;
	}

	return psoAVPRetVal;
}

avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo)
{
	avp * psoAVPSF = NULL;
	avp * psoAVPChild = NULL;
	avp_value soAVPVal;

	if (! p_psoReqInfo->m_psoSessInfo->m_coFeatureListId.is_null () && ! p_psoReqInfo->m_psoSessInfo->m_coFeatureList.is_null ()) {
		/* Supported-Features */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSupportedFeatures, 0, &psoAVPSF), return NULL);
		/* Vendor- Id */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictVendorId, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) 10415;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		/* Feature-List-Id */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFeatureListID, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) p_psoReqInfo->m_psoSessInfo->m_coFeatureListId.v;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		/* Feature-List */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFeatureList, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) p_psoReqInfo->m_psoSessInfo->m_coFeatureList.v;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	} else {
		return NULL;
	}

	return psoAVPSF;
}

int pcrf_make_SI(msg *p_psoMsg, SMsgDataForDB &p_soReqInfo)
{
	int iRetVal = 0;
	avp *psoSI;
	avp *psoSIType;
	avp *psoSIData;
	avp_value soAVPVal;

	/* END_USER_E164 */
	if (!p_soReqInfo.m_psoSessInfo->m_coEndUserE164.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionId, 0, &psoSI), return 0);
		/* Subscription-Id-Type */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdType, 0, &psoSIType), return 0);
		soAVPVal.u32 = (uint32_t) 0; /* END_USER_E164 */
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIType, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIType), return 0);
		/* Subscription-Id-Data */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdData, 0, &psoSIData), return 0);
		soAVPVal.os.data = (uint8_t*) p_soReqInfo.m_psoSessInfo->m_coEndUserE164.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_coEndUserE164.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIData, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIData), return 0);
		/**/
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoSI), return 0);
	}

	/* END_USER_IMSI */
	if (!p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionId, 0, &psoSI), return 0);
		/* Subscription-Id-Type */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdType, 0, &psoSIType), return 0);
		soAVPVal.u32 = (uint32_t)1; /* END_USER_IMSI */
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIType, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIType), return 0);
		/* Subscription-Id-Data */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdData, 0, &psoSIData), return 0);
		soAVPVal.os.data = (uint8_t*)p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIData, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIData), return 0);
		/**/
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoSI), return 0);
	}

	return iRetVal;
}

int pcrf_make_APNAMBR(msg *p_psoMsg, SRequestInfo &p_soReqInfo)
{
	avp *psoAVPQoSI = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;

	if (!p_soReqInfo.m_coAPNAggregateMaxBitrateDL.is_null() || !p_soReqInfo.m_coAPNAggregateMaxBitrateUL.is_null()) {
		/* QoS-Information */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictQoSInformation, 0, &psoAVPQoSI), return __LINE__);
		/* APN-Aggregate-Max-Bitrate-UL */
		if (!p_soReqInfo.m_coAPNAggregateMaxBitrateUL.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateUL, 0, &psoAVPChild), return __LINE__);
			soAVPVal.u32 = p_soReqInfo.m_coAPNAggregateMaxBitrateUL.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return __LINE__);
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return __LINE__);
		}
		/* APN-Aggregate-Max-Bitrate-DL */
		if (!p_soReqInfo.m_coAPNAggregateMaxBitrateDL.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateDL, 0, &psoAVPChild), return __LINE__);
			soAVPVal.u32 = p_soReqInfo.m_coAPNAggregateMaxBitrateDL.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return __LINE__);
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return __LINE__);
		}
		/* put 'QoS-Information' into 'answer' */
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoAVPQoSI), return __LINE__);
	}

	return 0;
}

int pcrf_make_DefaultEPSBearerQoS(msg *p_psoMsg, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;
	avp *psoDEPSBQoS;
	avp *psoARP;
	avp *psoAVPChild;
	avp_value soAVPVal;

	/* Default-EPS-Bearer-QoS */
	if (!p_soReqInfo.m_coDEPSBQoS.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictDefaultEPSBearerQoS, 0, &psoDEPSBQoS), return 0);
		/* QoS-Class-Identifier */
		if (!p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.is_null()) {
			memset(&soAVPVal, 0, sizeof(soAVPVal));
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictQoSClassIdentifier, 0, &psoAVPChild), return 0);
			soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
			CHECK_FCT_DO(fd_msg_avp_add(psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
		}
		/* Allocation-Retention-Priority */
		if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAllocationRetentionPriority, 0, &psoARP), return 0);
			/* Priority-Level */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPriorityLevel, 0, &psoAVPChild), return 0);
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			/* Pre-emption-Capability */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPreemptionCapability, 0, &psoAVPChild), return 0);
				soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			/* Pre-emption-Vulnerability */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPreemptionVulnerability, 0, &psoAVPChild), return 0);
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			CHECK_FCT_DO(fd_msg_avp_add(psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoARP), return 0);
		}
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoDEPSBQoS), return 0);
	}

	return iRetVal;
}

int pcrf_make_UMI( msg_or_avp *p_psoMsgOrAVP, SSessionInfo &p_soSessInfo )
{
  /* если список пуст выходим из функции */
  if ( 0 != p_soSessInfo.m_mapMonitInfo.size() ) {
  } else {
    LOG_D( "%s: empty list", __FUNCTION__ );
    return 0;
  }

  LOG_D( "enter: %s;", __FUNCTION__ );

  avp *psoAVPUMI = NULL; /* Usage-Monitoring-Information */
  avp *psoAVPGSU = NULL; /* Granted-Service-Unit */
  avp *psoAVPChild = NULL;
  union avp_value soAVPVal;

  std::map<std::string, SDBMonitoringInfo>::iterator iterMonitInfo = p_soSessInfo.m_mapMonitInfo.begin();
  for ( ; iterMonitInfo != p_soSessInfo.m_mapMonitInfo.end(); ++iterMonitInfo ) {
    /* если задана хотябы одна квота */
    if ( 0 == iterMonitInfo->second.m_coDosageTotalOctets.is_null()
      || 0 == iterMonitInfo->second.m_coDosageOutputOctets.is_null()
      || 0 == iterMonitInfo->second.m_coDosageInputOctets.is_null() ) {
    } else {
      /* иначе переходим к следующему ключу */
      continue;
    }
    psoAVPUMI = NULL;
    /* Usage-Monitoring-Information */
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringInformation, 0, &psoAVPUMI ), return __LINE__ );
    /* Monitoring-Key */
    {
      CHECK_FCT_DO( fd_msg_avp_new( g_psoDictMonitoringKey, 0, &psoAVPChild ), return __LINE__ );
      soAVPVal.os.data = (uint8_t *)iterMonitInfo->first.data();
      soAVPVal.os.len = (size_t)iterMonitInfo->first.length();
      CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
      CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
    }
    /* дополнительные параметры */
    if ( iterMonitInfo->second.m_bIsReported && GX_CISCO_SCE == p_soSessInfo.m_uiPeerDialect ) {
      /* Usage-Monitoring-Level */
      CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringLevel, 0, &psoAVPChild ), return __LINE__ );
      soAVPVal.i32 = 1;  /* PCC_RULE_LEVEL */
      CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
      /* put 'Usage-Monitoring-Level' into 'Usage-Monitoring-Information' */
      CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
    }
    if ( ! iterMonitInfo->second.m_bIsReported ) {
      /* Usage-Monitoring-Level */
      CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringLevel, 0, &psoAVPChild ), return __LINE__ );
      soAVPVal.i32 = 1;  /* PCC_RULE_LEVEL */
      CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
      /* put 'Usage-Monitoring-Level' into 'Usage-Monitoring-Information' */
      CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
      /* Usage-Monitoring-Report */
      if ( GX_PROCERA != p_soSessInfo.m_uiPeerDialect ) {
        CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringReport, 0, &psoAVPChild ), return __LINE__ );
        soAVPVal.i32 = 0; /* USAGE_MONITORING_REPORT_REQUIRED */
        CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
        /* put 'Usage-Monitoring-Report' into 'Usage-Monitoring-Information' */
        CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
      }
    }
    /* Granted-Service-Unit */
    CHECK_FCT_DO( fd_msg_avp_new( g_psoDictGrantedServiceUnit, 0, &psoAVPGSU ), return __LINE__ );
    /* CC-Total-Octets */
    if ( 0 == iterMonitInfo->second.m_coDosageTotalOctets.is_null() ) {
      CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCTotalOctets, 0, &psoAVPChild ), return __LINE__ );
      soAVPVal.u64 = iterMonitInfo->second.m_coDosageTotalOctets.v;
      CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
      /* put 'CC-Total-Octets' into 'Granted-Service-Unit' */
      CHECK_FCT_DO( fd_msg_avp_add( psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
    } else {
      /* CC-Input-Octets */
      if ( 0 == iterMonitInfo->second.m_coDosageInputOctets.is_null() ) {
        CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCInputOctets, 0, &psoAVPChild ), return __LINE__ );
        soAVPVal.u64 = iterMonitInfo->second.m_coDosageInputOctets.v;
        CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
        /* put 'CC-Input-Octets' into 'Granted-Service-Unit' */
        CHECK_FCT_DO( fd_msg_avp_add( psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
      }
      /* CC-Output-Octets */
      if ( 0 == iterMonitInfo->second.m_coDosageOutputOctets.is_null() ) {
        CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCOutputOctets, 0, &psoAVPChild ), return __LINE__ );
        soAVPVal.u64 = iterMonitInfo->second.m_coDosageOutputOctets.v;
        CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
        /* put 'CC-Output-Octets' into 'Granted-Service-Unit' */
        CHECK_FCT_DO( fd_msg_avp_add( psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
      }
    }
    /* put 'Granted-Service-Unit' into 'Usage-Monitoring-Information' */
    CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPGSU ), return __LINE__ );
    /* put into request */
    CHECK_FCT( fd_msg_avp_add( p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVPUMI ) );
  }

  LOG_D( "leave: %s;", __FUNCTION__ );

  return 0;
}

avp * pcrf_make_HWUR ()
{
	avp
		*psoAVPHWUR = NULL, /* X-HW-Usage-Report */
		*psoAVPHWSrvU = NULL, /* X-HW-Service-Usage */
		*psoAVPHWSsnU = NULL, /* X-HW-Session-Usage */
		*psoAVPChild = NULL;
	union avp_value soVal;

	/* X-HW-Usage-Report */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWUsageReport, 0, &psoAVPHWUR), return NULL);

	/* X-HW-Service-Usage */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWServiceUsage, 0, &psoAVPHWSrvU), return NULL);
	/* Rating-Group */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRatingGroup, 0, &psoAVPChild), return NULL);
	soVal.u32 = (uint32_t) 1;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'Rating-Group' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Input-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Input-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Output-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Output-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* put 'X-HW-Service-Usage' into 'Usage-Monitoring-Information' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWUR, MSG_BRW_LAST_CHILD, psoAVPHWSrvU), return NULL);

	/* X-HW-Session-Usage */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWSessionUsage, 0, &psoAVPHWSsnU), return NULL);
	/* CC-Input-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Input-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSsnU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Output-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Output-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSsnU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* put 'X-HW-Service-Usage' into 'Usage-Monitoring-Information' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWUR, MSG_BRW_LAST_CHILD, psoAVPHWSsnU), return NULL);

	/* X-HW-Session-Usage */

	return psoAVPHWUR;
}

int set_event_trigger(
  msg_or_avp *p_psoMsgOrAVP,
  int32_t p_iTrigId )
{
  int iRetVal = 0;
  avp *psoAVP;
  avp_value soAVPValue;

  CHECK_FCT( fd_msg_avp_new( g_psoDictEventTrigger, 0, &psoAVP ) );
  soAVPValue.i32 = p_iTrigId;
  CHECK_FCT( fd_msg_avp_setvalue( psoAVP, &soAVPValue ) );
  CHECK_FCT( fd_msg_avp_add( p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVP ) );

  return iRetVal;
}

int pcrf_extract_req_data (msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;

	/* проверка параметров */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
		|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	char mcValue[0x100];
	vendor_id_t tVenId;

	/* ищем первую AVP */
	iRetVal = fd_msg_browse_internal(p_psoMsgOrAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		if (AVP_FLAG_VENDOR & psoAVPHdr->avp_flags) {
			tVenId = psoAVPHdr->avp_vendor;
		} else {
			tVenId = 0;
		}
		switch (tVenId) {
		case 0: /* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 8: /* Framed-IP-Address */
        pcrf_ip_addr_to_string(psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len, p_psoMsgInfo->m_psoSessInfo->m_coFramedIPAddress);
        if ( psoAVPHdr->avp_value->os.len == sizeof( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress ) ) {
          memcpy( reinterpret_cast<void*>(&p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress), psoAVPHdr->avp_value->os.data, sizeof( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress ) );
          p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress = ntohl( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress );
        }
				break;
			case 30: /* Called-Station-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.set_non_null();
				break;
			case 263: /* Session-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.set_non_null();
				break;
			case 264: /* Origin-Host */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.set_non_null();
				break;
			case 278: /* Origin-State-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginStateId = psoAVPHdr->avp_value->u32;
				break;
			case 296: /* Origin-Realm */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.set_non_null();
				break;
			case 295: /* Termination-Cause */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoSessInfo->m_coTermCause = mcValue;
				}
				break;
			case 416: /* CC-Request-Type */
				p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coCCRequestType = mcValue;
				}
				break;
			case 443: /* Subscription-Id */
				pcrf_extract_SubscriptionId(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 415: /* CC-Request-Number */
				p_psoMsgInfo->m_psoReqInfo->m_coCCRequestNumber = psoAVPHdr->avp_value->u32;
				break;
			case 458: /* User-Equipment-Info */
				pcrf_extract_UEI(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			}
			break; /* Diameter */
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 6: /* 3GPP-SGSN-Address */
        pcrf_ip_addr_to_string(psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len, p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress);
			break;
			case 18: /* 3GPP-SGSN-MCC-MNC */
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC.set_non_null();
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				break;
			case 21: /* 3GPP-RAT-Type */
				if (!psoAVPHdr->avp_value->os.len)
					break;
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				switch (psoAVPHdr->avp_value->os.data[0]) {
				case 1:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "UTRAN";
					break;
				case 2:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "GERAN";
					break;
				case 3:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "WLAN";
					break;
				case 4:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "GAN";
					break;
				case 5:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "HSPA Evolution";
					break;
				default:
          UTL_LOG_N(*g_pcoLog, "unknown 3GPP-RAT-Type: '%u'", psoAVPHdr->avp_value->os.data[0]);
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = false;
					break;
				}
				break;
			case 22: /* 3GPP-User-Location-Info */
				pcrf_parse_user_location(*psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo);
				break;
			case 515: /* Max-Requested-Bandwidth-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl = psoAVPHdr->avp_value->u32;
				break;
			case 516: /* Max-Requested-Bandwidth-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl = psoAVPHdr->avp_value->u32;
				break;
			case 628: /* Supported-Features */
				pcrf_extract_SF(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 909: /* RAI */
				pcrf_parse_RAI(*psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRAI);
				break;
			case 1000: /* Bearer-Usage */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerUsage = mcValue;
				}
				break;
			case 1006: /* Event-Trigger */
				{
					p_psoMsgInfo->m_psoReqInfo->m_vectEventTrigger.push_back(psoAVPHdr->avp_value->i32);
				}
				break;
			case 1009: /* Online */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOnlineCharging = mcValue;
				}
				break;
			case 1008: /* Offline */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOfflineCharging = mcValue;
				}
				break;
			case 1016: /* QoS-Information */
				pcrf_extract_req_data((void *)psoAVP, p_psoMsgInfo);
				break;
			case 1018: /* Charging-Rule-Report */
				pcrf_extract_CRR(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 1020: /* Bearer-Identifier */
				if (p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.is_null()) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.set_non_null();
				}
				break;
			case 1021: /* Bearer-Operation */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerOperation = mcValue;
				}
				break;
			case 1025: /* Guaranteed-Bitrate-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coGuaranteedBitrateDl = psoAVPHdr->avp_value->u32;
				break;
			case 1026: /* Guaranteed-Bitrate-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coGuaranteedBitrateUl = psoAVPHdr->avp_value->u32;
				break;
			case 1027: /* IP-CAN-Type */
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType = psoAVPHdr->avp_value->i32;
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType = mcValue;
				}
				break;
			case 1029: /* QoS-Negotiation */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSNegotiation = mcValue;
				}
				break;
			case 1030: /* QoS-Upgrade */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSUpgrade = mcValue;
				}
				break;
			case 1032: /* RAT-Type */
        p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_iRATType = psoAVPHdr->avp_value->i32;
        p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
        if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = mcValue;
				}
				break;
			case 1040: /* APN-Aggregate-Max-Bitrate-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL = psoAVPHdr->avp_value->u32;
				break;
			case 1041: /* APN-Aggregate-Max-Bitrate-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL = psoAVPHdr->avp_value->u32;
				break;
			case 1049: /* Default-EPS-Bearer-QoS */
				p_psoMsgInfo->m_psoReqInfo->m_coDEPSBQoS.set_non_null();
				pcrf_extract_DefaultEPSBearerQoS(psoAVP, *p_psoMsgInfo->m_psoReqInfo);
				break;
			case 1067: /* Usage-Monitoring-Information */
				pcrf_extract_UMI(psoAVP, *(p_psoMsgInfo->m_psoReqInfo));
				break;
			}
			break; /* 3GPP */
    case 2011:  /* Huawai */
      switch (psoAVPHdr->avp_code) {
      case 2029: /* X-HW-Tethering-Status */
        p_psoMsgInfo->m_psoReqInfo->m_coTetheringFlag = psoAVPHdr->avp_value->u32;
        break;
      }
      break;    /* Huawai */
    case 15397: /* Procera */
      switch (psoAVPHdr->avp_code) {
      case 777: /* Procera-Tethering-Flag */
        p_psoMsgInfo->m_psoReqInfo->m_coTetheringFlag = psoAVPHdr->avp_value->u32;
        break;
      }
      break; /* Procera */
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	return iRetVal;
}

int pcrf_extract_SubscriptionId (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	int iSubscriptionIdType = -1;
	std::string strSubscriptionIdData;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 450: /* Subscription-Id-Type */
				iSubscriptionIdType = psoAVPHdr->avp_value->i32;
				break;
			case 444: /* Subscription-Id-Data */
				strSubscriptionIdData.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	if (strSubscriptionIdData.length()) {
		switch (iSubscriptionIdType) {
		case 0: /* END_USER_E164 */
			p_soSessInfo.m_coEndUserE164 = strSubscriptionIdData;
			break;
		case 1: /* END_USER_IMSI */
			p_soSessInfo.m_coEndUserIMSI = strSubscriptionIdData;
			break;
		case 2: /* END_USER_SIP_URI */
			p_soSessInfo.m_coEndUserSIPURI = strSubscriptionIdData;
			break;
		case 3: /* END_USER_NAI */
			p_soSessInfo.m_coEndUserNAI = strSubscriptionIdData;
			break;
		case 4: /* END_USER_PRIVATE */
			p_soSessInfo.m_coEndUserPrivate = strSubscriptionIdData;
			break;
		}
	}

	return iRetVal;
}

int pcrf_extract_UEI (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	int iType = -1;
	std::string strInfo;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 459: /* User-Equipment-Info-Type */
				iType = psoAVPHdr->avp_value->i32;
				break;
			case 460: /* User-Equipment-Info-Value */
				strInfo.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	switch (iType) {
	case 0: /* IMEISV */
		p_soSessInfo.m_coIMEI = strInfo;
		break;
	}

	return iRetVal;
}

int pcrf_extract_CRR (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	SSessionPolicyInfo soPolicy;
	char mcValue[0x10000];

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1005: /* Charging-Rule-Name */
				soPolicy.m_coChargingRuleName.v.insert(0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soPolicy.m_coChargingRuleName.set_non_null();
				break;
			case 1019: /* PCC-Rule-Status */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coPCCRuleStatus = mcValue;
				}
				break;
			case 1031: /* Rule-Failure-Code */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coRuleFailureCode = mcValue;
				}
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	/* добавляем в список если найдено что-то полезное */
	if (! soPolicy.m_coChargingRuleName.is_null() && !soPolicy.m_coRuleFailureCode.is_null()) {
		p_soSessInfo.m_vectCRR.push_back(soPolicy);
    UTL_LOG_D(*g_pcoLog, "session id: '%s'; rule name: '%s'; failury code: '%s'", p_soSessInfo.m_coSessionId.v.c_str(), soPolicy.m_coChargingRuleName.v.c_str(), soPolicy.m_coRuleFailureCode.v.c_str());
	}

	return iRetVal;
}

int pcrf_extract_SF (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 629: /* Feature-List-Id */
				p_soSessInfo.m_coFeatureListId = psoAVPHdr->avp_value->u32;
				break;
			case 630: /* Feature-List */
				p_soSessInfo.m_coFeatureList = psoAVPHdr->avp_value->u32;
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	return iRetVal;
}

int pcrf_extract_UMI (avp *p_psoAVP, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	SSessionUsageInfo soUsageInfo;
	bool bDone = false;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1066: /* Monitoring-Key */
				bDone = true;
				soUsageInfo.m_coMonitoringKey.v.assign((const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soUsageInfo.m_coMonitoringKey.set_non_null();
				break; /* Monitoring-Key */
			}
			break; /* 3GPP */
		case 0:	/* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 446: /* Used-Service-Unit */
				pcrf_extract_USU(psoAVP, soUsageInfo);
				break;
			}
			break;	/* Diameter */
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	if (bDone) {
		p_soReqInfo.m_vectUsageInfo.push_back(soUsageInfo);
	}

	return iRetVal;
}

int pcrf_extract_USU (avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			break;
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 412: /* CC-Input-Octets */
				p_soUsageInfo.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
				break;
			case 414: /* CC-Output-Octets */
				p_soUsageInfo.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
				break;
			case 421: /* CC-Total-Octets */
				p_soUsageInfo.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	return iRetVal;
}

int pcrf_extract_DefaultEPSBearerQoS (avp *p_soAVP, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;


	iRetVal = fd_msg_browse_internal((void *)p_soAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP)
			break;
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1028: /* QoS-Class-Identifier */
				p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier = psoAVPHdr->avp_value->i32;
				break;
			case 1034: /* Allocation-Retention-Priority */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.set_non_null();
				pcrf_extract_DefaultEPSBearerQoS(psoAVP, p_soReqInfo);
				break;
			case 1046: /* Priority-Level */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel = psoAVPHdr->avp_value->u32;
				break;
			case 1047: /* Pre-emption-Capability */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability = psoAVPHdr->avp_value->i32;
				break;
			case 1048: /* Pre-emption-Vulnerability */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability = psoAVPHdr->avp_value->u32;
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL));

	return iRetVal;
}

int pcrf_procera_change_uli( otl_connect *p_pcoDBConn, SMsgDataForDB &p_soReqData )
{
  SDBAbonRule soAbonRule( false, true );
  std::vector<SDBAbonRule> vectOldRule;
  std::vector<SDBAbonRule> vectNewRule;
  std::vector<SSessionInfo> vectSessList;

  /* обрабатыаем новую локацию */
  soAbonRule.m_coDynamicRuleFlag = 0;
  soAbonRule.m_coRuleGroupFlag = 0;

  pcrf_procera_make_uli_rule(
    0 == p_soReqData.m_psoReqInfo->m_soUserLocationInfo.m_coECGI.is_null() ?
    p_soReqData.m_psoReqInfo->m_soUserLocationInfo.m_coECGI :
    p_soReqData.m_psoReqInfo->m_soUserLocationInfo.m_coCGI,
    soAbonRule );
  vectNewRule.push_back( soAbonRule );

  CHECK_FCT_DO( pcrf_procera_db_load_sess_list( p_soReqData.m_psoSessInfo->m_coSessionId, vectSessList ), return 0 );

  SMsgDataForDB soReqInfo;
  pcrf_server_DBstruct_init( &soReqInfo );
  for ( std::vector<SSessionInfo>::iterator iter = vectSessList.begin(); iter != vectSessList.end(); ++iter ) {
    CHECK_FCT_DO( pcrf_procera_db_load_location_rule( p_pcoDBConn, iter->m_coSessionId, vectOldRule ), break );
    *soReqInfo.m_psoSessInfo = *iter;
    CHECK_FCT_DO( pcrf_client_rar( soReqInfo, &vectOldRule, vectNewRule, NULL, NULL, 0 ), break );
  }
  pcrf_server_DBStruct_cleanup( &soReqInfo );

  return 0;
}

int pcrf_procera_make_subscription_id (msg *p_soAns, otl_value<std::string> &p_coEndUserIMSI, otl_value<std::string> &p_coEndUserE164)
{
  int iRetVal = 0;
  avp *psoAVPCI = NULL;
  avp *psoAVPChild = NULL;
  avp_value soAVPVal;

  /* Subscription-Id */
  /* IMSI */
  if (! p_coEndUserIMSI.is_null()) {
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionId, 0, &psoAVPCI), return 0);
    /* Subscription-Id-Type */
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionIdType, 0, &psoAVPChild), return 0);
	  soAVPVal.i32 = 1; /* END_USER_IMSI */
	  CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return 0);
    /* put Subscription-Id-Type into Subscription-Id */
	  CHECK_FCT_DO (fd_msg_avp_add (psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
    /* Subscription-Id-Data */
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionIdData, 0, &psoAVPChild), return 0);
    soAVPVal.os.data = (uint8_t*) p_coEndUserIMSI.v.c_str();
    soAVPVal.os.len = p_coEndUserIMSI.v.length();
	  CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return 0);
    /* put Subscription-Id-Data into Subscription-Id */
	  CHECK_FCT_DO (fd_msg_avp_add (psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
    /* put Subscription-Id into answer */
    CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPCI), return 0);
  }
  /* E164 */
  if (! p_coEndUserE164.is_null()) {
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionId, 0, &psoAVPCI), return 0);
    /* Subscription-Id-Type */
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionIdType, 0, &psoAVPChild), return 0);
	  soAVPVal.i32 = 0; /* END_USER_E164 */
	  CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return 0);
    /* put Subscription-Id-Type into Subscription-Id */
	  CHECK_FCT_DO (fd_msg_avp_add (psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
    /* Subscription-Id-Data */
    CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSubscriptionIdData, 0, &psoAVPChild), return 0);
    soAVPVal.os.data = (uint8_t*) p_coEndUserE164.v.c_str();
    soAVPVal.os.len = p_coEndUserE164.v.length();
	  CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return 0);
    /* put Subscription-Id-Data into Subscription-Id */
	  CHECK_FCT_DO (fd_msg_avp_add (psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);

    /* put Subscription-Id into answer */
    CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPCI), return 0);
  }

  return iRetVal;
}

int pcrf_procera_make_uli_rule (otl_value<std::string> &p_coULI, SDBAbonRule &p_soAbonRule)
{
  int iRetVal = 0;

  if (p_coULI.is_null ()) {
    /* если локация пользователя не задана */
    p_soAbonRule.m_coRuleName = "/User-Location/inRoaming";
  } else {
    /* выбираем необходимые данные */
    std::size_t iBSPos = std::string::npos, iSectorPos;
    iSectorPos = p_coULI.v.find_last_of ('-');
    if (std::string::npos != iSectorPos) {
      iBSPos = p_coULI.v.find_last_of ('-', iSectorPos - 1);
    } else {
      return EINVAL;
    }
    /* если найдены все необходимые разделители */
    if (std::string::npos != iBSPos) {
      p_soAbonRule.m_coRuleName = "/User-Location/";
      if (iSectorPos - iBSPos > 1) {
        p_soAbonRule.m_coRuleName.v += p_coULI.v.substr (iBSPos + 1, iSectorPos - iBSPos - 1);
      } else {
        return EINVAL;
      }
      p_soAbonRule.m_coRuleName.v += '/';
      p_soAbonRule.m_coRuleName.v += p_coULI.v.substr (iSectorPos + 1);
    } else {
      return EINVAL;
    }
  }
  p_soAbonRule.m_coDynamicRuleFlag = 0;
  p_soAbonRule.m_coRuleGroupFlag = 0;

  return iRetVal;
}

int pcrf_procera_terminate_session( otl_value<std::string> &p_coUGWSessionId )
{
  if ( 0 != pcrf_peer_is_dialect_used( GX_PROCERA ) ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  std::vector<SSessionInfo> vectSessList;

  pcrf_procera_db_load_sess_list( p_coUGWSessionId, vectSessList );
  for ( std::vector<SSessionInfo>::iterator iter = vectSessList.begin(); iter != vectSessList.end(); ++iter ) {
    pcrf_local_refresh_queue_add( *iter );
  }

  return iRetVal;
}

/* загружает описание правил */
int load_rule_info(
  std::vector<std::string> &p_vectRuleList,
  std::vector<SDBAbonRule> &p_vectAbonRules )
{
  int iRetVal = 0;
  std::vector<std::string>::iterator iter = p_vectRuleList.begin();

  for ( ; iter != p_vectRuleList.end(); ++iter ) {
    {
      SDBAbonRule soAbonRule;
      if ( 0 == pcrf_rule_cache_get_rule_info( *iter, soAbonRule ) ) {
        p_vectAbonRules.push_back( soAbonRule );
      }
    }
  }

  return iRetVal;
}

int pcrf_server_create_abon_rule_list( otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo, std::vector<SDBAbonRule> &p_vectAbonRules )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  int iRetVal = 0;

  /* очищаем список перед выполнением */
  p_vectAbonRules.clear();

  do {
    /* дополнительная информация для Procera */
    if ( GX_PROCERA == p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect ) {
      SDBAbonRule soAbonRule( false, true );
      std::string strValue;

      soAbonRule.m_coDynamicRuleFlag = 0;
      soAbonRule.m_coRuleGroupFlag = 0;

      /* добавляем IMEISV */
      if ( !p_soMsgInfo.m_psoSessInfo->m_coIMEI.is_null() ) {
        strValue = "/IMEISV/" + p_soMsgInfo.m_psoSessInfo->m_coIMEI.v;
        soAbonRule.m_coRuleName = strValue;
        p_vectAbonRules.push_back( soAbonRule );
      }
      /* добавляем APN */
      if ( !p_soMsgInfo.m_psoSessInfo->m_coCalledStationId.is_null() ) {
        strValue = "/APN/" + p_soMsgInfo.m_psoSessInfo->m_coCalledStationId.v;
        soAbonRule.m_coRuleName = strValue;
        p_vectAbonRules.push_back( soAbonRule );
      }
      /* user location */
      pcrf_procera_make_uli_rule(
        0 == p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI.is_null() ?
        p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI :
        p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI,
        soAbonRule );
      p_vectAbonRules.push_back( soAbonRule );
    }
    /* список идентификаторов правил абонента */
    std::vector<std::string> vectRuleList;
    /* загружаем правила абонента */
    pcrf_load_abon_rule_list( p_pcoDBConn, p_soMsgInfo, vectRuleList );
    /* если список идентификаторов правил не пустой */
    if ( vectRuleList.size() ) {
      load_rule_info( vectRuleList, p_vectAbonRules );
      /* в случае с SCE нам надо оставить одно правило с наивысшим приоритетом */
      if ( p_vectAbonRules.size() && GX_CISCO_SCE == p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect ) {
        SDBAbonRule soAbonRule;
        std::vector<SDBAbonRule>::iterator iterList = p_vectAbonRules.begin();
        if ( iterList != p_vectAbonRules.end() ) {
          soAbonRule = *iterList;
          ++iterList;
        }
        while ( iterList != p_vectAbonRules.end() ) {
          if ( soAbonRule.m_coPrecedenceLevel.v > iterList->m_coPrecedenceLevel.v )
            soAbonRule = *iterList;
          ++iterList;
        }
        p_vectAbonRules.clear();
        p_vectAbonRules.push_back( soAbonRule );
      }
    }
  } while ( 0 );

  LOG_D( "leave: %s; result code: %d", __FUNCTION__, iRetVal );

  return iRetVal;
}

int pcrf_procera_oper_thetering_report( SMsgDataForDB &p_soRequestInfo, std::vector<SDBAbonRule> &p_vectAbonRule, std::vector<SDBAbonRule> &p_vectActiveRule )
{
  SDBAbonRule soRule;
  std::map<std::string, int32_t>::iterator iter;

  soRule.m_coDynamicRuleFlag = 0;
  soRule.m_coRuleGroupFlag = 0;
  soRule.m_coRuleName = "/PSM/Policies/Redirect_blocked_device";

  if ( NULL != g_pmapTethering && NULL != p_soRequestInfo.m_psoSessInfo && 0 == p_soRequestInfo.m_psoSessInfo->m_coSessionId.is_null() ) {
  } else {
    g_pmapTethering = new std::map<std::string, int32_t>;
  }

  iter = g_pmapTethering->find( p_soRequestInfo.m_psoSessInfo->m_coSessionId.v );
  if ( iter == g_pmapTethering->end() ) {
    if ( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.is_null() ) {
    } else {
      switch ( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.v ) {
        case 0:
          soRule.m_bIsActive = true;
          p_vectActiveRule.push_back( soRule );
        default:
          soRule.m_bIsRelevant = true;
          p_vectAbonRule.push_back( soRule );
          g_pmapTethering->insert( std::make_pair( p_soRequestInfo.m_psoSessInfo->m_coSessionId.v, 1 ) );
      }
    }
  } else {
    if ( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.is_null() ) {
      switch ( iter->second ) {
        case 0:
          g_pmapTethering->erase( iter );
          break;
        default:
          soRule.m_bIsRelevant = true;
          p_vectAbonRule.push_back( soRule );
      }
    } else {
      if ( 0 == p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.v ) {
        g_pmapTethering->erase( iter );
      } else {
        soRule.m_bIsRelevant = true;
        p_vectAbonRule.push_back( soRule );
        iter->second = 1;
      }
    }
  }
}

unsigned int pcrf_server_determine_action_set( SMsgDataForDB &p_soRequestInfo )
{
  unsigned int uiRetVal = 0;
  std::vector<int32_t>::iterator iter = p_soRequestInfo.m_psoReqInfo->m_vectEventTrigger.begin();

  for ( ; iter != p_soRequestInfo.m_psoReqInfo->m_vectEventTrigger.end(); ++iter ) {
    switch ( *iter ) {
      case 2:	/* RAT_CHANGE */
        uiRetVal |= ACTION_UPDATE_SESSIONCACHE;
        uiRetVal |= ACTION_UPDATE_RULE;
        LOG_D( "session-id: %s; RAT_CHANGE", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 13: /* USER_LOCATION_CHANGE */
        uiRetVal |= ACTION_UPDATE_SESSIONCACHE;
        /* Event-Trigger USER_LOCATION_CHANGE */
        if ( GX_3GPP == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
          if ( pcrf_peer_is_dialect_used( GX_PROCERA ) ) {
            uiRetVal |= ACTION_PROCERA_CHANGE_ULI;
          }
        }
        LOG_D( "session-id: %s; USER_LOCATION_CHANGE", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 20: /* DEFAULT_EPS_BEARER_QOS_CHANGE */
        uiRetVal |= ACTION_COPY_DEFBEARER;
        LOG_D( "session-id: %s; DEFAULT_EPS_BEARER_QOS_CHANGE", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 26: /* USAGE_REPORT */ /* Cisco SCE Gx notation */
        uiRetVal |= ACTION_UPDATE_RULE;
        LOG_D( "session-id: %s; USAGE_REPORT[26]", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 33: /* USAGE_REPORT */
        uiRetVal |= ACTION_UPDATE_RULE;
        uiRetVal |= ACTION_UPDATE_QUOTA;
        LOG_D( "session-id: %s; USAGE_REPORT[33]", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 101: /* TETHERING_REPORT */
        if ( GX_3GPP == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
          uiRetVal |= ACTION_UGW_STORE_THET_INFO;
        }
        LOG_D( "session-id: %s; TETHERING_REPORT", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
      case 777:
        if ( GX_PROCERA == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
          uiRetVal |= ACTION_PROCERA_STORE_THET_INFO;
        }
        LOG_D( "session-id: %s; Event-Trigger[777]", p_soRequestInfo.m_psoSessInfo->m_coSessionId.v.c_str() );
        break;
    }
  }

  return uiRetVal;
}

void pcrf_make_mk_list( std::vector<SDBAbonRule> &p_vectAbonRules, SSessionInfo *p_psoSessInfo )
{
  /* проверяем состояние указателя */
  if ( NULL != p_psoSessInfo ) {
  } else {
    return;
  }

  std::vector<SDBAbonRule>::iterator iterRule;
  std::vector<std::string>::iterator iterMonitKey;

  /* обходим все правила из списка */
  for ( iterRule = p_vectAbonRules.begin(); iterRule != p_vectAbonRules.end(); ++iterRule ) {
    /* если очередное правило релевантно и неактивно (т.е. впоследствие будет инсталлировано) */
    if ( iterRule->m_bIsRelevant && ! iterRule->m_bIsActive ) {
      /* обходим все ключи мониторинга подлежащего инсталляции правила */
      for ( iterMonitKey = iterRule->m_vectMonitKey.begin(); iterMonitKey != iterRule->m_vectMonitKey.end(); ++iterMonitKey ) {
        /* добавляем (или пытаемся добавить) их в список ключей мониторинга сессии */
        p_psoSessInfo->m_mapMonitInfo.insert( std::pair<std::string, SDBMonitoringInfo>( *iterMonitKey, SDBMonitoringInfo() ) );
      }
    }
  }
}
