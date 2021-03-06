#include <vector>
#include <stdio.h>

#include "cache/pcrf_rule_cache.h"
#include "cache/pcrf_subscriber_cache.h"
#include "pcrf_linked_session.h"
#include "pcrf_session_cache.h"
#include "pcrf_session_cache_index.h"
#include "procera/pcrf_procera.h"
#include "data_proc/pcrf_timed_oper.h"
#include "app_pcrf.h"
#include "app_pcrf_header.h"

CLog *g_pcoLog = NULL;

extern uint32_t g_ui32OriginStateId;

/* handler for CCR req cb */
static disp_hdl * app_pcrf_hdl_ccr = NULL;

/* функция формирования avp 'QoS-Information' */
avp * pcrf_make_QoSI( const SRequestInfo *p_psoReqInfo, const SDBAbonRule &p_soAbonRule );

/* функция заполнения avp Charging-Rule-Definition */
avp * pcrf_make_CRD( const SRequestInfo *p_psoReqInfo, const SDBAbonRule &p_soAbonRule );

/* функция заполнения Subscription-Id */
int pcrf_make_SI(msg *p_psoMsg, SMsgDataForDB &p_soReqInfo);

/* функция заполнения Default-EPS-Bearer-QoS */
int pcrf_make_DefaultEPSBearerQoS(msg *p_psoMsg, SRequestInfo &p_soReqInfo);

/* функция заполнения QoS-Information */
int pcrf_make_QoSInformation( msg *p_psoMsg, SRequestInfo &p_soReqInfo );

/* функция заполнения avp X-HW-Usage-Report */
avp * pcrf_make_HWUR();

/* выборка идентификатора абонента */
int pcrf_extract_SubscriptionId(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка данных об устройстве абонента */
int pcrf_extract_UEI(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка рапорта о назначении политик */
int pcrf_extract_ChargingRuleReport( avp *p_psoAVP, SRequestInfo &p_soReqInfo );
/* выборка значений Supported-Features */
int pcrf_extract_SF( avp *p_psoAVP, std::list<SSF> &p_listSupportedFeatures );
/* выборка значений Usage-Monitoring-Information */
int pcrf_extract_UMI( avp *p_psoAVP, SRequestInfo &p_soReqInfo );
/* выборка значений Used-Service-Unit */
int pcrf_extract_USU( avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo );
/* парсинг Default-EPS-Bearer-QoS */
int pcrf_extract_DefaultEPSBearerQoS( avp *p_psoAVP, SRequestInfo &p_soReqInfo );
/* выборка QoS-Information */
int pcrf_extract_QoSInformation(avp *p_psoAVP, otl_value<SQoSInformation> &p_coQoSInformation );

/* парсинг RAI */
int pcrf_parse_RAI(avp_value &p_soAVPValue, otl_value<std::string> &p_coValue);

static unsigned int pcrf_server_determine_action_set( SMsgDataForDB &p_soRequestInfo );

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
	if( NULL != p_ppsoMsg ) {
	} else {
		return EINVAL;
	}

	/* suppress compiler warning */
	p_psoAVP = p_psoAVP; p_psoSess = p_psoSess; opaque = opaque; p_pAct = p_pAct;

	CTimeMeasurer coTM;

	SMsgDataForDB soMsgInfoCache;
	unsigned int uiActionSet;

	unsigned int uiResultCode = 2001; /* DIAMETER_SUCCESS */
	std::string *pstrIPCANSessionId = NULL;

	/* инициализация структуры хранения данных сообщения */
	CHECK_POSIX_DO( pcrf_server_DBstruct_init( &soMsgInfoCache ), /*continue*/ );

	/* выбираем данные из сообщения */
	msg_or_avp *pMsgOrAVP = *p_ppsoMsg;
	pcrf_extract_req_data( pMsgOrAVP, &soMsgInfoCache );

	/* необходимо определить диалект хоста */
	CHECK_POSIX_DO( pcrf_peer_dialect( *soMsgInfoCache.m_psoSessInfo ), /*continue*/ );

	if( INITIAL_REQUEST != soMsgInfoCache.m_psoReqInfo->m_iCCRequestType
		|| 0 == g_psoConf->m_iLook4StalledSession ) {
	} else {
	  /* проверка наличия зависших сессий */
		CHECK_POSIX_DO( pcrf_server_look4stalledsession( soMsgInfoCache.m_psoSessInfo ), /*continue*/ );
	}

	/* определяем набор действий, необходимых для формирования ответа */
	uiActionSet = pcrf_server_determine_action_set( soMsgInfoCache );
	LOG_D( "Session-Id: %s: Action Set: %#x", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str(), uiActionSet );

	UTL_LOG_D( *g_pcoLog, "session-id: '%s'; peer: '%s@%s'; dialect: %u",
			   soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str(),
			   soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.c_str(),
			   soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.c_str(),
			   soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );

	if( GX_CISCO_SCE != soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
	} else {	/* Gx Cisco SCE */
		SSubscriptionIdData &psoSubscrData = soMsgInfoCache.m_psoSessInfo->m_soSubscriptionData;
		/* исправляем косяки циски */
		/* переносим значение E164 на IMSI */
		if( psoSubscrData.m_coEndUserIMSI.is_null() && ! psoSubscrData.m_coEndUserE164.is_null() ) {
			psoSubscrData.m_coEndUserIMSI = psoSubscrData.m_coEndUserE164;
			psoSubscrData.m_coEndUserE164.v.clear();
			psoSubscrData.m_coEndUserE164.set_null();
		}
		/* отсекаем суффикс от IMSI */
		if( ! psoSubscrData.m_coEndUserIMSI.is_null() ) {
			if( psoSubscrData.m_coEndUserIMSI.v.length() > 15 ) {
				size_t stPos;
				stPos = psoSubscrData.m_coEndUserIMSI.v.find( "_" );
				if( stPos != std::string::npos )
					psoSubscrData.m_coEndUserIMSI.v.resize( stPos );
			}
		}
	}

	int iFnRes;
	otl_connect *pcoDBConn = NULL;
	std::list<SDBAbonRule> listAbonRules;    /* список правил профиля абонента */
	std::vector<SDBAbonRule> vectActive;       /* список активных правил абонента */
	std::string *pstrSessShouldBeTerm = NULL;

	bool bRulesChanged = false;        /* признак того, что правила были изменены */
	bool bMKInstalled = false;         /* признак того, что ключи мониторинга были инсталлированы */
	std::list<SDBAbonRule>::iterator iterAbonRule;

	/* запрашиваем объект класса для работы с БД */
	/* взаимодействие с БД необходимо лишь в случае INITIAL_REQUEST либо UPDATE_REQUEST в сочетании с ACTION_OPERATE_RULE и (или) ACTION_UPDATE_QUOTA */
	/* так же для всех запросов (временно), поступающих от клиентов с пропиетарными диалектами */
	if( ( uiActionSet & ACTION_OPERATE_RULE ) || ( uiActionSet & ACTION_UPDATE_QUOTA ) ) {
	#ifdef DEBUG
		iFnRes = pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC );
	#else
		iFnRes = pcrf_db_pool_get( &pcoDBConn, NULL, USEC_PER_SEC );
	#endif
		if( 0 == iFnRes && NULL != pcoDBConn ) {
		  /* подклчение к БД получено успешно */
		} else {
		  /* подключение к БД не получено */
			if( ( GX_HW_UGW == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect
				  || GX_ERICSSN == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect )
				&& UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
				/* попытка оптимизации взаимодействия с ugw */
			} else {
				uiResultCode = 3004; /* DIAMETER_TOO_BUSY */
			}
			UTL_LOG_E(
				*g_pcoLog,
				"db connection is not given: request type: %s; action set: %#x",
				( INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) ? "CCR-I" :
				( UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) ? "CCR-U" :
				( TERMINATION_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) ? "CCR-T" : "CCR-?",
				uiActionSet );
		}
		LOG_D( "Session-Id: %s: Action DB connection is requested", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
	}

	/* дополняем данные запроса необходимыми параметрами */
	switch( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
		case INITIAL_REQUEST: /* INITIAL_REQUEST */
			switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				case GX_HW_UGW:
				case GX_ERICSSN:
				  /* загружаем идентификтор абонента из профиля абонента */
					pcrf_subscriber_cache_get_subscriber_id( soMsgInfoCache.m_psoSessInfo->m_soSubscriptionData, soMsgInfoCache.m_psoSessInfo->m_strSubscriberId );
					break;
				case GX_CISCO_SCE:
				  /* загружаем идентификтор абонента из профиля абонента */
					pcrf_subscriber_cache_get_subscriber_id( soMsgInfoCache.m_psoSessInfo->m_soSubscriptionData, soMsgInfoCache.m_psoSessInfo->m_strSubscriberId );
					pstrIPCANSessionId = new std::string;
					if( 0 == pcrf_server_find_core_session( soMsgInfoCache.m_psoSessInfo->m_strSubscriberId, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, *pstrIPCANSessionId ) ) {
					  /* ищем сведения о сессии в кеше */
						pcrf_session_cache_get( *pstrIPCANSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
					} else {
						delete pstrIPCANSessionId;
						pstrIPCANSessionId = NULL;
					}
					break;
				case GX_PROCERA:
				{
					SSessionInfo soSessInfo;
					/* загрузка данных сессии UGW для обслуживания запроса Procera */
					pstrIPCANSessionId = new std::string;
					if( 0 == pcrf_server_find_core_sess_byframedip( soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, soSessInfo ) ) {
						*pstrIPCANSessionId = soSessInfo.m_strSessionId;
						pcrf_session_cache_get( *pstrIPCANSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
					} else {
						delete pstrIPCANSessionId;
						pstrIPCANSessionId = NULL;
						uiResultCode = 5030; /* USER_UNKNOWN */
					}
				}
				break;
				default:
					UTL_LOG_E( *g_pcoLog, "unsupported peer dialect: '%u'", soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );
					break;
			}
			pcrf_session_cache_insert( soMsgInfoCache.m_psoSessInfo->m_strSessionId, *soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, pstrIPCANSessionId );
			break;/* INITIAL_REQUEST */
		case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
			pcrf_fill_otl_datetime( soMsgInfoCache.m_psoSessInfo->m_coTimeEnd, NULL );
			/* для Procera инициируем завершение сессии в том случае, когда завершена сессия на ugw */
			switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				case GX_HW_UGW:
				case GX_ERICSSN:
					pcrf_linked_session_terminate( soMsgInfoCache.m_psoSessInfo->m_strSessionId );
					/* если необходимо писать cdr */
					if( 0 != g_psoConf->m_iGenerateCDR
						|| ( uiActionSet & ACTION_UPDATE_QUOTA ) ) {
					  /* запрашиваем сведения о сессии из кэша */
						pcrf_session_cache_get( soMsgInfoCache.m_psoSessInfo->m_strSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
					}
					break;
			}
			pcrf_session_cache_remove( soMsgInfoCache.m_psoSessInfo->m_strSessionId );
			break;  /* TERMINATION_REQUEST */
		case UPDATE_REQUEST: /* UPDATE_REQUEST */
		{
			int iSessNotFound;
			/* загружаем идентификатор абонента из списка активных сессий абонента */
			iSessNotFound = pcrf_session_cache_get( soMsgInfoCache.m_psoSessInfo->m_strSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
			/* загрузка данных сессии UGW для обслуживания запроса SCE */
			switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				case GX_HW_UGW:
				case GX_ERICSSN:
					if( 0 == iSessNotFound ) {
						pcrf_linked_session_rule_report( soMsgInfoCache.m_psoSessInfo->m_strSessionId, soMsgInfoCache.m_psoReqInfo->m_vectCRR );
					} else {
					  /* если сессия ugw не найдена просим завершить ее. таким образом избавляемся от сессий, неизвестных pcrf */
						pstrSessShouldBeTerm = new std::string;
						*pstrSessShouldBeTerm = soMsgInfoCache.m_psoSessInfo->m_strSessionId;
					}
					break;
				case GX_CISCO_SCE:
				{
					pstrIPCANSessionId = new std::string;
					/* ищем базовую сессию ugw */
					if( 0 == pcrf_server_find_core_session( soMsgInfoCache.m_psoSessInfo->m_strSubscriberId, soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, *pstrIPCANSessionId ) ) {
					  /* ищем информацию о базовой сессии в кеше */
						pcrf_session_cache_get( *pstrIPCANSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
					} else {
						delete pstrIPCANSessionId;
						pstrIPCANSessionId = NULL;
						uiResultCode = 5030; /* USER_UNKNOWN */
					}
				}
				break;
				case GX_PROCERA:
				{
					SSessionInfo soSessInfo;
					pstrIPCANSessionId = new std::string;
					if( 0 == pcrf_server_find_core_sess_byframedip( soMsgInfoCache.m_psoSessInfo->m_coFramedIPAddress.v, soSessInfo ) ) {
						*pstrIPCANSessionId = soSessInfo.m_strSessionId;
						pcrf_session_cache_get( *pstrIPCANSessionId, soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, NULL );
					} else {
						delete pstrIPCANSessionId;
						pstrIPCANSessionId = NULL;
					}
				}
				break;
				default:
					UTL_LOG_E( *g_pcoLog, "unsupported peer dialect: '%u'", soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect );
					break;
			}
		}
		break;  /* UPDATE_REQUEST */
		default:  /* DEFAULT */
			break;  /* DEFAULT */
	}

	/* если необходимо писать cdr */
	if( 0 != g_psoConf->m_iGenerateCDR ) {
		switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				pcrf_cdr_write_cdr( soMsgInfoCache );
				break;
		}
	}
	/* сохраняем в БД запрос */
	pcrf_server_req_db_store( &soMsgInfoCache );

	/* если обработка данных прошла успешно */
	if( 2001 == uiResultCode ) {
		if( uiActionSet & ACTION_OPERATE_RULE ) {
			if( GX_PROCERA == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				pcrf_procera_additional_rules(
					soMsgInfoCache.m_psoSessInfo->m_coIMEI,
					soMsgInfoCache.m_psoSessInfo->m_coCalledStationId,
					soMsgInfoCache.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI,
					soMsgInfoCache.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI,
					listAbonRules );
			}
		}

		if( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType == UPDATE_REQUEST && uiActionSet & ACTION_OPERATE_RULE ) {
			/* загружаем список активных правил */
			CHECK_POSIX_DO( pcrf_session_rule_cache_get( soMsgInfoCache.m_psoSessInfo->m_strSessionId, vectActive ), /* continue */ );
		}

		SSubscriberData *psoSubscrData;

		psoSubscrData = pcrf_subscriber_data_prepare( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType,
													  soMsgInfoCache.m_psoSessInfo->m_strSubscriberId,
													  soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect,
													  soMsgInfoCache.m_psoReqInfo->m_vectUsageInfo,
													  soMsgInfoCache.m_psoReqInfo->m_soUserEnvironment,
													  soMsgInfoCache.m_psoSessInfo->m_coCalledStationId,
													  soMsgInfoCache.m_psoSessInfo->m_coIMEI,
													  vectActive,
													  uiActionSet,
													  listAbonRules,
													  soMsgInfoCache.m_psoSessInfo->m_mapMonitInfo );

		if( 0 == pcrf_subscriber_data_proc( psoSubscrData ) ) {
		} else {
			/* произошел сбой при формировании списка правил абонента */
			pcrf_local_refresh_queue_add(
				time_t( NULL ) + g_psoConf->m_uiRefreshDefRuleIn,
				soMsgInfoCache.m_psoSessInfo->m_strSubscriberId,
				"subscriber_id",
				NULL );
		}
	}

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
	} while( 0 );

	/* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
	switch( uiResultCode ) {
		case 2001: /* DIAMETER_SUCCESS */
			CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast< char* >( "DIAMETER_SUCCESS" ), NULL, NULL, 1 ), /*continue*/ );
			break;
		case 3004: /* DIAMETER_TOO_BUSY */
			CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast< char* >( "DIAMETER_TOO_BUSY" ), NULL, NULL, 1 ), /*continue*/ );
			break;
		case 5030: /* USER_UNKNOWN */
			CHECK_FCT_DO( fd_msg_rescode_set( ans, const_cast< char* >( "USER_UNKNOWN" ), NULL, NULL, 1 ), /*continue*/ );
			break;
	}

	/* put 'CC-Request-Type' into answer */
	do {
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCRequestType, 0, &psoChildAVP ), break );
		soAVPVal.i32 = soMsgInfoCache.m_psoReqInfo->m_iCCRequestType;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
		CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
	} while( 0 );
	/* put 'CC-Request-Number' into answer */
	do {
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCRequestNumber, 0, &psoChildAVP ), break );
		soAVPVal.u32 = soMsgInfoCache.m_psoReqInfo->m_coCCRequestNumber.v;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
		CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
	} while( 0 );
	/* put 'Origin-State-Id' into answer */
	do {
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictOriginStateId, 0, &psoChildAVP ), break );
		soAVPVal.u32 = g_ui32OriginStateId;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
		CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
	} while( 0 );

	switch( soMsgInfoCache.m_psoReqInfo->m_iCCRequestType ) {
		case INITIAL_REQUEST: /* INITIAL_REQUEST */
		  /* Supported-Features */
			pcrf_make_SF( ans, soMsgInfoCache.m_psoSessInfo->m_listSF );
			/* дополняем ответ на CCR-I для Procera информацией о пользователе */
			/* Subscription-Id */
			if( GX_PROCERA == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				CHECK_FCT_DO( set_event_trigger( ans, 777 ), /* continue */ );
				CHECK_FCT_DO( pcrf_procera_make_subscription_id( ans, soMsgInfoCache.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI, soMsgInfoCache.m_psoSessInfo->m_soSubscriptionData.m_coEndUserE164 ), /* continue */ );
			}
			/* Default-EPS-Bearer-QoS */
			pcrf_make_DefaultEPSBearerQoS( ans, *soMsgInfoCache.m_psoReqInfo );
			/* QoS-Information */
			pcrf_make_QoSInformation( ans, *soMsgInfoCache.m_psoReqInfo );
			/* Usage-Monitoring-Information */
			CHECK_FCT_DO( pcrf_make_UMI( ans, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );
			/* Charging-Rule-Install */
			psoChildAVP = pcrf_make_CRI( soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, listAbonRules, ans );
			/* put 'Charging-Rule-Install' into answer */
			if( psoChildAVP ) {
				CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
			}
			/* Bearer-Control-Mode */
			do {
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAVPBearerControlMode, 0, &psoChildAVP ), break );
				soAVPVal.i32 = 2; /* UE_NW */
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoChildAVP, &soAVPVal ), break );
				CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), break );
			} while( 0 );
			break; /* INITIAL_REQUEST */
		case UPDATE_REQUEST: /* UPDATE_REQUEST */
			if( uiActionSet & ACTION_COPY_DEFBEARER ) {
			  /* Default-EPS-Bearer-QoS */
				pcrf_make_DefaultEPSBearerQoS( ans, *soMsgInfoCache.m_psoReqInfo );
				LOG_D( "Session-Id: %s: default bearer is accepted", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
				/* QoS-Information */
				pcrf_make_QoSInformation( ans, *soMsgInfoCache.m_psoReqInfo );
			}
			if( uiActionSet & ACTION_UPDATE_SESSIONCACHE ) {
				pcrf_session_cache_insert(
					soMsgInfoCache.m_psoSessInfo->m_strSessionId,
					*soMsgInfoCache.m_psoSessInfo,
					soMsgInfoCache.m_psoReqInfo,
					pstrIPCANSessionId );
				LOG_D( "Session-Id: %s: session cache is updated", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
			}
			if( uiActionSet & ACTION_PROCERA_CHANGE_ULI ) {
				CHECK_FCT_DO( pcrf_procera_change_uli( pcoDBConn,
													   soMsgInfoCache.m_psoSessInfo->m_strSessionId,
													   soMsgInfoCache.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI,
													   soMsgInfoCache.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI ), /* continue */ );
				LOG_D( "Session-Id: %s: user location is changed on procera", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
			}
			if( uiActionSet & ACTION_OPERATE_RULE ) {
				/* Charging-Rule-Remove */
				psoChildAVP = pcrf_make_CRR( soMsgInfoCache.m_psoSessInfo, vectActive );
				/* put 'Charging-Rule-Remove' into answer */
				if( psoChildAVP ) {
					CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
				}
				/* Charging-Rule-Install */
				psoChildAVP = pcrf_make_CRI( soMsgInfoCache.m_psoSessInfo, soMsgInfoCache.m_psoReqInfo, listAbonRules, ans );
				/* put 'Charging-Rule-Install' into answer */
				if( psoChildAVP ) {
					CHECK_FCT_DO( fd_msg_avp_add( ans, MSG_BRW_LAST_CHILD, psoChildAVP ), /*continue*/ );
				}
				LOG_D( "Session-Id: %s: session rules are operated", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
			}

			/* Usage-Monitoring-Information */
			CHECK_FCT_DO( pcrf_make_UMI( ans, *( soMsgInfoCache.m_psoSessInfo ) ), /* continue */ );

			if( uiActionSet & ACTION_UGW_STORE_THET_INFO ) {
				pcrf_server_db_insert_tethering_info( soMsgInfoCache );
				LOG_D( "Session-Id: %s: ugw thetering info is stored", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
			}
			if( uiActionSet & ACTION_PROCERA_STORE_THET_INFO ) {
				pcrf_procera_oper_thetering_report( soMsgInfoCache, listAbonRules, vectActive );
				LOG_D( "Session-Id: %s: procera thetering info is stored", soMsgInfoCache.m_psoSessInfo->m_strSessionId.c_str() );
			}
			break; /* UPDATE_REQUEST */
	}

	/* обходим все правила */
	if( uiActionSet & ACTION_OPERATE_RULE ) {
		for( iterAbonRule = listAbonRules.begin(); iterAbonRule != listAbonRules.end(); ++iterAbonRule ) {
		  /* если найдены правила, подлежащие инсталляции */
			if( ! bRulesChanged && ! iterAbonRule->m_bIsActive && iterAbonRule->m_bIsRelevant ) {
				bRulesChanged = true;
			}
			/* среди релевантных правил проверяем наличие ключей мониторинга */
			if( ! bMKInstalled && iterAbonRule->m_bIsRelevant && 0 != iterAbonRule->m_vectMonitKey.size() ) {
				bMKInstalled = true;
			}
			/* ищем, есть ли правила, подлежащие удалению */
			if( ! bRulesChanged && iterAbonRule->m_bIsActive && ! iterAbonRule->m_bIsRelevant ) {
				bRulesChanged = true;
			}
			/* если нашли оба признака дальше искать нет смысла */
			if( bRulesChanged && bMKInstalled ) {
				break;
			}
		}
	}

	/* задаем Event-Trigger */
	if( INITIAL_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType
		|| UPDATE_REQUEST == soMsgInfoCache.m_psoReqInfo->m_iCCRequestType && bRulesChanged ) {
		/* Event-Trigger RAT_CHANGE */
		switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				CHECK_FCT_DO( set_event_trigger( ans, 2 ), /* continue */ );
				break;
		}
		/* Event-Trigger TETHERING_REPORT */
		if( GX_HW_UGW == soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
			CHECK_FCT_DO( set_event_trigger( ans, 101 ), /* continue */ );
		}
	#if 1
		/* Event-Trigger USER_LOCATION_CHANGE && SGSN_CHANGE */
		switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:
			case GX_ERICSSN:
				CHECK_FCT_DO( set_event_trigger( ans, 13 ), /* continue */ );
				CHECK_FCT_DO( set_event_trigger( ans, 0 ), /* continue */ );
				break;
		}
	#endif
		/* Event-Trigger USAGE_REPORT */
		if( bMKInstalled ) {
			switch( soMsgInfoCache.m_psoSessInfo->m_uiPeerDialect ) {
				case GX_HW_UGW:
				case GX_PROCERA:
				case GX_ERICSSN:
					CHECK_FCT_DO( set_event_trigger( ans, 33 ), /* continue */ );
					break;
				case GX_CISCO_SCE:
					CHECK_FCT_DO( set_event_trigger( ans, 26 ), /* continue */ );
					break;
			}
		}
	}

cleanup_and_exit:
	if( pstrIPCANSessionId ) {
		delete pstrIPCANSessionId;
		pstrIPCANSessionId = NULL;
	}
	/* фиксируем статистику */

	/* освобождаем занятые ресурсы */
	pcrf_server_DBStruct_cleanup( &soMsgInfoCache );

	/* освобождаем объект класса взаимодействия с БД */
	if( NULL != pcoDBConn ) {
		CHECK_POSIX_DO( pcrf_db_pool_rel( reinterpret_cast< void * >( pcoDBConn ), __FUNCTION__ ), /*continue*/ );
	}

	/* если ответ сформирован отправляем его */
	if( NULL != ans ) {
		CHECK_FCT_DO( fd_msg_send( p_ppsoMsg, NULL, NULL ), /*continue*/ );
	}

	/* статистика по работе функции */
	stat_measure( g_psoGxSesrverStat, __FUNCTION__, &coTM );

	/* если сессию следует завершить */
	if( NULL != pstrSessShouldBeTerm ) {
		pcrf_local_refresh_queue_add( static_cast< time_t >( 0 ), *pstrSessShouldBeTerm, "session_id", "abort_session" );
		delete pstrSessShouldBeTerm;
	}

	return 0;
}

int app_pcrf_serv_init( void )
{
	disp_when data;

	memset( &data, 0, sizeof( data ) );
	data.app = g_psoDictApp;
	data.command = g_psoDictCCR;

	/* Now specific handler for CCR */
	CHECK_FCT( fd_disp_register( app_pcrf_ccr_cb, DISP_HOW_CC, &data, NULL, &app_pcrf_hdl_ccr ) );

  /* Advertise the support for the Gx application in the peer */
	CHECK_FCT_DO( fd_disp_app_support( g_psoDictApp, g_psoDictVend, 1, 0 ), /* continue */ );

	/* инициализация объектов статистики */
	g_psoDBStat = stat_get_branch( "DB operation" );
	g_psoGxSesrverStat = stat_get_branch( "gx server" );

	return 0;
}

void app_pcrf_serv_fini( void )
{
	if( app_pcrf_hdl_ccr ) {
		( void ) fd_disp_unregister( &app_pcrf_hdl_ccr, NULL );
	}
}

int pcrf_logger_init( void )
{
	g_pcoLog = new CLog;

	return g_pcoLog->Init( g_psoConf->m_pszLogFileMask );
}

void pcrf_logger_fini(void)
{
	g_pcoLog->Flush();
	delete g_pcoLog;
	g_pcoLog = NULL;
}

void pcrf_server_select_notrelevant_active( std::list<SDBAbonRule> &p_listAbonRules, std::vector<SDBAbonRule> &p_vectActive )
{
	/* обходим все активные правила */
	std::list<SDBAbonRule>::iterator iterRule;
	std::vector<SDBAbonRule>::iterator iterActive;
	/* цикл актуальных правил */
	for( iterRule = p_listAbonRules.begin(); iterRule != p_listAbonRules.end(); ++iterRule ) {
		/* цикл активных правил */
		for( iterActive = p_vectActive.begin(); iterActive != p_vectActive.end(); ++iterActive ) {
			/* если имена правил совпадают, значит активное правило актуально */
			if( iterActive->m_strRuleName == iterRule->m_strRuleName ) {
				/* фиксируем, что правило активировано */
				iterRule->m_bIsActive = true;
				iterActive->m_bIsRelevant = true;
			}
		}
	}
}

avp * pcrf_make_QoSI( const SRequestInfo *p_psoReqInfo, const SDBAbonRule &p_soAbonRule )
{
	avp *psoAVPQoSI = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	int32_t i32Value;
	uint32_t ui32Value;

	do {
		/* QoS-Information */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDicAVPtQoSInformation, 0, &psoAVPQoSI ), return NULL );

		/* QoS-Class-Identifier */
		if( ! p_soAbonRule.m_coQoSClassIdentifier.is_null() ) {
			i32Value = p_soAbonRule.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictQoSClassIdentifier, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = i32Value;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* Max-Requested-Bandwidth-UL */
		if( ! p_soAbonRule.m_coMaxRequestedBandwidthUl.is_null() ) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthUl.v;
			if( NULL != p_psoReqInfo && ! p_psoReqInfo->m_coMaxRequestedBandwidthUl.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coMaxRequestedBandwidthUl.v ? p_psoReqInfo->m_coMaxRequestedBandwidthUl.v : ui32Value;
			}
			if( NULL != p_psoReqInfo && 0 == p_psoReqInfo->m_coQoSInformation.is_null() && 0 == p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictMaxRequestedBandwidthUL, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* Max-Requested-Bandwidth-DL */
		if( ! p_soAbonRule.m_coMaxRequestedBandwidthDl.is_null() ) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthDl.v;
			if( NULL != p_psoReqInfo && ! p_psoReqInfo->m_coMaxRequestedBandwidthDl.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coMaxRequestedBandwidthDl.v ? p_psoReqInfo->m_coMaxRequestedBandwidthDl.v : ui32Value;
			}
			if( NULL != p_psoReqInfo && 0 == p_psoReqInfo->m_coQoSInformation.is_null() && 0 == p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictMaxRequestedBandwidthDL, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* Guaranteed-Bitrate-UL */
		if( ! p_soAbonRule.m_coGuaranteedBitrateUl.is_null() ) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateUl.v;
			if( NULL != p_psoReqInfo && ! p_psoReqInfo->m_coGuaranteedBitrateUl.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coGuaranteedBitrateUl.v ? p_psoReqInfo->m_coGuaranteedBitrateUl.v : ui32Value;
			}
			if( NULL != p_psoReqInfo && 0 == p_psoReqInfo->m_coQoSInformation.is_null() && 0 == p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictGuaranteedBitrateUL, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* Guaranteed-Bitrate-DL */
		if( ! p_soAbonRule.m_coGuaranteedBitrateDl.is_null() ) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateDl.v;
			if( NULL != p_psoReqInfo && ! p_psoReqInfo->m_coGuaranteedBitrateDl.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coGuaranteedBitrateDl.v ? p_psoReqInfo->m_coGuaranteedBitrateDl.v : ui32Value;
			}
			if( NULL != p_psoReqInfo && p_psoReqInfo->m_coQoSInformation.is_null() && 0 == p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.is_null() ) {
				ui32Value = ui32Value > p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictGuaranteedBitrateDL, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* Allocation-Retention-Priority */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAllocationRetentionPriority, 0, &psoAVPParent ), return NULL );

		/* Priority-Level */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPriorityLevel, 0, &psoAVPChild ), return NULL );
		if( !p_soAbonRule.m_soARP.m_coPriorityLevel.is_null() ) {
			soAVPVal.u32 = p_soAbonRule.m_soARP.m_coPriorityLevel.v;
		} else if( NULL != p_psoReqInfo
				   && !p_psoReqInfo->m_coDEPSBQoS.is_null()
				   && !p_psoReqInfo->m_coDEPSBQoS.v.m_soARP.is_null()
				   && !p_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null() ) {
			soAVPVal.u32 = p_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
		} else {
			soAVPVal.u32 = 2;
		}
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );

		/* Pre-emption-Capability */
		if( !p_soAbonRule.m_soARP.m_coPreemptionCapability.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPreemptionCapability, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_soARP.m_coPreemptionCapability.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Pre-emption-Vulnerability */
		if( !p_soAbonRule.m_soARP.m_coPreemptionVulnerability.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPreemptionVulnerability, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_soARP.m_coPreemptionVulnerability.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}

		/* put 'Allocation-Retention-Priority' into 'QoS-Information' */
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPParent ), return NULL );

	} while( 0 );

	return psoAVPQoSI;
}

int pcrf_set_CRN( avp *p_pParent, dict_object *p_psoDictObj, const std::string &p_strName )
{
	avp *psoAVPCRN = NULL;
	avp_value soAVPVal;

	CHECK_FCT_DO( fd_msg_avp_new( p_psoDictObj, 0, &psoAVPCRN ), return EINVAL );
	soAVPVal.os.data = ( uint8_t* ) p_strName.c_str();
	soAVPVal.os.len = p_strName.length();
	CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPCRN, &soAVPVal ), return EINVAL );
	CHECK_FCT_DO( fd_msg_avp_add( p_pParent, MSG_BRW_LAST_CHILD, psoAVPCRN ), return EINVAL );

	return 0;
}

avp * pcrf_make_CRR( const SSessionInfo *p_psoSessInfo, const std::vector<SDBAbonRule> &p_vectActive )
{
	/* если список пустой выходим ничего не делая */
	if( 0 == p_vectActive.size() ) {
		return NULL;
	}

	avp *psoAVPCRR = NULL; /* Charging-Rule-Remove */
	std::vector<SDBAbonRule>::const_iterator iter = p_vectActive.begin();

	/* обходим все элементы списка */
	for( ; iter != p_vectActive.end(); ++iter ) {
		/* если правило актуально переходим к другому */
		if( iter->m_bIsRelevant )
			continue;
		switch( p_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW: /* Gx */
			case GX_PROCERA: /* Gx Procera */
			case GX_ERICSSN: /* Gx Ericsson */
					/* Charging-Rule-Remove */
				if( NULL == psoAVPCRR ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictChargingRuleRemove, 0, &psoAVPCRR ), return NULL );
				}
				/* если это групповое правило */
				if( ! iter->m_coRuleGroupFlag.is_null() && iter->m_coRuleGroupFlag.v ) {
					/* Charging-Rule-Base-Name */
					CHECK_FCT_DO( pcrf_set_CRN( psoAVPCRR, g_psoDictChargingRuleBaseName, iter->m_strRuleName ), continue );
				} else {
					/* Charging-Rule-Name */
					CHECK_FCT_DO( pcrf_set_CRN( psoAVPCRR, g_psoDictChargingRuleName, iter->m_strRuleName ), continue );
				}
				pcrf_db_close_session_rule( p_psoSessInfo, iter->m_strRuleName );
				break; /* Gx */
			case GX_CISCO_SCE: /* Gx Cisco SCE */
				pcrf_db_close_session_rule( p_psoSessInfo, iter->m_strRuleName );
				break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRR;
}

avp * pcrf_make_CRI(
	const SSessionInfo *p_psoSessInfo,
	const SRequestInfo *p_psoReqInfo,
	const std::list<SDBAbonRule> &p_listAbonRules,
	msg *p_soAns )
{
	/* если в списке нет ни одного правила */
	if( 0 == p_listAbonRules.size() ) {
		return NULL;
	}

	CTimeMeasurer coTM;
	avp *psoAVPCRI = NULL; /* Charging-Rule-Install */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	std::string strValue;

	std::list<SDBAbonRule>::const_iterator iter = p_listAbonRules.begin();
	/* обходим все правила */
	for( ; iter != p_listAbonRules.end(); ++ iter ) {
		/* если првило уже активировано переходим к следующей итерации */
		switch( p_psoSessInfo->m_uiPeerDialect ) {
			case GX_HW_UGW:     /* Gx */
			case GX_PROCERA: /* Gx Procera */
			case GX_ERICSSN: /* Gx Ericsson */
				if( iter->m_bIsActive ) {
					continue;
				}
				/* Charging-Rule-Install */
				/* создаем avp 'Charging-Rule-Install' только по необходимости */
				if( NULL == psoAVPCRI ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictChargingRuleInstall, 0, &psoAVPCRI ), return NULL );
					/* Bearer-Identifier */
					if( NULL != p_psoReqInfo
						&& 0 == p_psoReqInfo->m_soUserEnvironment.m_iIPCANType
						&& ! p_psoReqInfo->m_coBearerIdentifier.is_null() ) {
						CHECK_FCT_DO( fd_msg_avp_new( g_psoDictBearerIdentifier, 0, &psoAVPChild ), return NULL );
						soAVPVal.os.data = ( uint8_t * ) p_psoReqInfo->m_coBearerIdentifier.v.c_str();
						soAVPVal.os.len = p_psoReqInfo->m_coBearerIdentifier.v.length();
						CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
						/* put 'Bearer-Identifier' into 'Charging-Rule-Install' */
						CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
					}
				}
				/* Charging-Rule-Definition */
				psoAVPChild = pcrf_make_CRD( p_psoReqInfo, *iter );
				if( psoAVPChild ) {
					/* put 'Charging-Rule-Definition' into 'Charging-Rule-Install' */
					CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
					/* сохраняем выданную политику в БД */
					pcrf_db_insert_rule( *( p_psoSessInfo ), *iter );
				}
				break; /* Gx */
			case GX_CISCO_SCE: /* Gx Cisco SCE */
				/* Cisco-SCA BB-Package-Install */
				if( ! iter->m_coSCE_PackageId.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCiscoBBPackageInstall, 0, &psoAVPChild ), return NULL );
					soAVPVal.u32 = iter->m_coSCE_PackageId.v;
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Cisco-SCA BB-Package-Install' into answer */
					CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
				/* Cisco-SCA BB-Real-time-monitor-Install */
				if( ! iter->m_coSCE_RealTimeMonitor.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCiscoBBRTMonitorInstall, 0, &psoAVPChild ), return NULL );
					soAVPVal.u32 = iter->m_coSCE_RealTimeMonitor.v;
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Cisco-SCA BB-Real-time-monitor-Install' into answer */
					CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
				/* Cisco-SCA BB-Vlink-Upstream-Install */
				if( ! iter->m_coSCE_UpVirtualLink.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCiscoBBVlinkUStreamInstall, 0, &psoAVPChild ), return NULL );
					soAVPVal.u32 = iter->m_coSCE_UpVirtualLink.v;
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Cisco-SCA BB-Vlink-Upstream-Install' into answer */
					CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
				/* Cisco-SCA BB-Vlink-Downstream-Install */
				if( ! iter->m_coSCE_DownVirtualLink.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCiscoBBVlinkDStreamInstall, 0, &psoAVPChild ), return NULL );
					soAVPVal.u32 = iter->m_coSCE_DownVirtualLink.v;
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Cisco-SCA BB-Vlink-Downstream-Install' into answer */
					CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
				/* сохраняем выданную политику в БД */
				if( ! iter->m_bIsActive ) {
					pcrf_db_insert_rule( *( p_psoSessInfo ), *iter );
				}
				break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRI;
}

avp * pcrf_make_CRD(
	const SRequestInfo *p_psoReqInfo,
	const SDBAbonRule &p_soAbonRule )
{
	avp *psoAVPRetVal = NULL;
	avp *psoAVPCRD = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	int iIpCanType;

	/* сохраняем значение IP-CAN-Type в локальной переменной, т.к. оно часто испольуется */
	if( NULL != p_psoReqInfo ) {
		iIpCanType = p_psoReqInfo->m_soUserEnvironment.m_iIPCANType;
	} else {
		iIpCanType = -1;
	}

	  /* если это динамическое правило */
	if( ! p_soAbonRule.m_coDynamicRuleFlag.is_null() && p_soAbonRule.m_coDynamicRuleFlag.v ) {
	/* Charging-Rule-Definition */
		{
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictChargingRuleDefinition, 0, &psoAVPCRD ), return NULL );
			psoAVPRetVal = psoAVPCRD;
		}
			/* Charging-Rule-Name */
		CHECK_FCT_DO( pcrf_set_CRN( psoAVPCRD, g_psoDictChargingRuleName, p_soAbonRule.m_strRuleName ), return NULL );
			/* Service-Identifier */
		if( ! p_soAbonRule.m_coServiceId.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictServiceIdentifier, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = p_soAbonRule.m_coServiceId.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
	/* Flow-Status */
		if( ! p_soAbonRule.m_coFlowStatus.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAVPFlowStatus, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_coFlowStatus.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
			/* Rating-Group */
		if( ! p_soAbonRule.m_coRatingGroupId.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictRatingGroup, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = p_soAbonRule.m_coRatingGroupId.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Flow-Description */
		std::vector<SFlowInformation>::const_iterator iterFD = p_soAbonRule.m_vectFlowDescr.begin();
		for( ; iterFD != p_soAbonRule.m_vectFlowDescr.end(); ++ iterFD ) {
			/* Flow-Information */
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictFlowInformation, 0, &psoAVPParent ), return NULL );
			/* Flow-Description */
			{
				if( 0 == iterFD->m_coFlowDescription.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictFlowDescription, 0, &psoAVPChild ), return NULL );
					soAVPVal.os.data = ( uint8_t * ) iterFD->m_coFlowDescription.v.c_str();
					soAVPVal.os.len = ( size_t ) iterFD->m_coFlowDescription.v.length();
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Flow-Description' into 'Flow-Information' */
					CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
				/* Flow-Direction */
				if( 0 == iterFD->m_coFlowDirection.is_null() ) {
					CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAVPFlowDirection, 0, &psoAVPChild ), return NULL );
					soAVPVal.i32 = iterFD->m_coFlowDirection.v;
					CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
					/* put 'Flow-Direction - in' into 'Flow-Information' */
					CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
				}
			}
				  /* put 'Flow-Information' into 'Charging-Rule-Definition' */
			{
				CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent ), return NULL );
			}
		}
		/* QoS-Information */
		psoAVPChild = pcrf_make_QoSI( p_psoReqInfo, p_soAbonRule );
		/* put 'QoS-Information' into 'Charging-Rule-Definition' */
		if( psoAVPChild ) {
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
	/* Reporting-Level */
		if( ! p_soAbonRule.m_coReportingLevel.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAVPReportingLevel, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_coReportingLevel.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Online */
		if( ! p_soAbonRule.m_coOnlineCharging.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictOnline, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_coOnlineCharging.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Offline */
		if( ! p_soAbonRule.m_coOfflineCharging.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictOffline, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_coOfflineCharging.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Metering-Method */
		if( ! p_soAbonRule.m_coMeteringMethod.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictMeteringMethod, 0, &psoAVPChild ), return NULL );
			soAVPVal.i32 = p_soAbonRule.m_coMeteringMethod.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Precedence */
		if( ! p_soAbonRule.m_coPrecedenceLevel.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPrecedence, 0, &psoAVPChild ), return NULL );
			soAVPVal.u32 = p_soAbonRule.m_coPrecedenceLevel.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Monitoring-Key */
		if( 0 < p_soAbonRule.m_vectMonitKey.size() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictMonitoringKey, 0, &psoAVPChild ), return NULL );
			soAVPVal.os.data = ( uint8_t * ) p_soAbonRule.m_vectMonitKey[0].data();
			soAVPVal.os.len = ( size_t ) p_soAbonRule.m_vectMonitKey[0].length();
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
		}
		/* Redirect-Server */
		if( ! p_soAbonRule.m_coRedirectAddressType.is_null() && ! p_soAbonRule.m_coRedirectServerAddress.is_null() ) {
			/* Redirect-Server */
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictRedirectServer, 0, &psoAVPParent ), return NULL );
			/* Redirect-Address-Type */
			{
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictRedirectAddressType, 0, &psoAVPChild ), return NULL );
				soAVPVal.i32 = p_soAbonRule.m_coRedirectAddressType.v;
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
				/* put 'Redirect-Address-Type' into 'Redirect-Server' */
				CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
			}
			/* Redirect-Server-Address */
			{
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictRedirectServerAddress, 0, &psoAVPChild ), return NULL );
				soAVPVal.os.data = ( uint8_t * ) p_soAbonRule.m_coRedirectServerAddress.v.c_str();
				soAVPVal.os.len = ( size_t ) p_soAbonRule.m_coRedirectServerAddress.v.length();
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
				/* put 'Redirect-Server-Address' into 'Redirect-Server' */
				CHECK_FCT_DO( fd_msg_avp_add( psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild ), return NULL );
			}
			/* put 'Redirect-Server' into 'Charging-Rule-Definition' */
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent ), return NULL );
		}
	}
	/* если это предопределенное правило */
	else {
		/* если это пакетное правило */
		if( ! p_soAbonRule.m_coRuleGroupFlag.is_null() && p_soAbonRule.m_coRuleGroupFlag.v ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictChargingRuleBaseName, 0, &psoAVPChild ), return NULL );
		} else {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictChargingRuleName, 0, &psoAVPChild ), return NULL );
		}
		soAVPVal.os.data = ( uint8_t * ) p_soAbonRule.m_strRuleName.c_str();
		soAVPVal.os.len = ( size_t ) p_soAbonRule.m_strRuleName.length();
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return NULL );
		psoAVPRetVal = psoAVPChild;
	}

	return psoAVPRetVal;
}

void pcrf_make_SF( msg_or_avp *p_psoAns, std::list<SSF> &p_listSupportedFeatures )
{
	avp * psoAVPSF = NULL;
	avp * psoAVPChild = NULL;
	avp_value soAVPVal;

	for( std::list<SSF>::iterator iter = p_listSupportedFeatures.begin(); iter != p_listSupportedFeatures.end(); ++iter ) {
		/* Supported-Features */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSupportedFeatures, 0, &psoAVPSF ), return );
		/* Vendor- Id */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictVendorId, 0, &psoAVPChild ), return );
		soAVPVal.u32 = iter->m_ui32VendorId;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return );
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild ), return );
		/* Feature-List-Id */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictFeatureListID, 0, &psoAVPChild ), return );
		soAVPVal.u32 = iter->m_ui32FeatureListID;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return );
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild ), return );
		/* Feature-List */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictFeatureList, 0, &psoAVPChild ), return );
		soAVPVal.u32 = iter->m_ui32FeatureList;
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return );
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild ), return );
	  /* put 'Supported-Features' into answer */
		CHECK_FCT_DO( fd_msg_avp_add( p_psoAns, MSG_BRW_LAST_CHILD, psoAVPSF ), return );
	}
}

int pcrf_make_SI( msg *p_psoMsg, SMsgDataForDB &p_soReqInfo )
{
	int iRetVal = 0;
	avp *psoSI;
	avp *psoSIType;
	avp *psoSIData;
	avp_value soAVPVal;

	/* END_USER_E164 */
	if( !p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserE164.is_null() ) {
		/* Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionId, 0, &psoSI ), return 0 );
		/* Subscription-Id-Type */
		memset( &soAVPVal, 0, sizeof( soAVPVal ) );
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdType, 0, &psoSIType ), return 0 );
		soAVPVal.u32 = ( uint32_t ) 0; /* END_USER_E164 */
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoSIType, &soAVPVal ), return 0 );
		CHECK_FCT_DO( fd_msg_avp_add( psoSI, MSG_BRW_LAST_CHILD, psoSIType ), return 0 );
		/* Subscription-Id-Data */
		memset( &soAVPVal, 0, sizeof( soAVPVal ) );
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdData, 0, &psoSIData ), return 0 );
		soAVPVal.os.data = ( uint8_t* ) p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserE164.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserE164.v.length();
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoSIData, &soAVPVal ), return 0 );
		CHECK_FCT_DO( fd_msg_avp_add( psoSI, MSG_BRW_LAST_CHILD, psoSIData ), return 0 );
		/**/
		CHECK_FCT_DO( fd_msg_avp_add( p_psoMsg, MSG_BRW_LAST_CHILD, psoSI ), return 0 );
	}

	/* END_USER_IMSI */
	if( !p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI.is_null() ) {
		/* Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionId, 0, &psoSI ), return 0 );
		/* Subscription-Id-Type */
		memset( &soAVPVal, 0, sizeof( soAVPVal ) );
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdType, 0, &psoSIType ), return 0 );
		soAVPVal.u32 = ( uint32_t ) 1; /* END_USER_IMSI */
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoSIType, &soAVPVal ), return 0 );
		CHECK_FCT_DO( fd_msg_avp_add( psoSI, MSG_BRW_LAST_CHILD, psoSIType ), return 0 );
		/* Subscription-Id-Data */
		memset( &soAVPVal, 0, sizeof( soAVPVal ) );
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdData, 0, &psoSIData ), return 0 );
		soAVPVal.os.data = ( uint8_t* ) p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI.v.length();
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoSIData, &soAVPVal ), return 0 );
		CHECK_FCT_DO( fd_msg_avp_add( psoSI, MSG_BRW_LAST_CHILD, psoSIData ), return 0 );
		/**/
		CHECK_FCT_DO( fd_msg_avp_add( p_psoMsg, MSG_BRW_LAST_CHILD, psoSI ), return 0 );
	}

	return iRetVal;
}

int pcrf_make_DefaultEPSBearerQoS( msg *p_psoMsg, SRequestInfo &p_soReqInfo )
{
	int iRetVal = 0;
	avp *psoDEPSBQoS;
	avp *psoARP;
	avp *psoAVPChild;
	avp_value soAVPVal;

	/* Default-EPS-Bearer-QoS */
	if( !p_soReqInfo.m_coDEPSBQoS.is_null() ) {
		/* Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictDefaultEPSBearerQoS, 0, &psoDEPSBQoS ), return 0 );
		/* QoS-Class-Identifier */
		if( !p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.is_null() ) {
			memset( &soAVPVal, 0, sizeof( soAVPVal ) );
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictQoSClassIdentifier, 0, &psoAVPChild ), return 0 );
			soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
			CHECK_FCT_DO( fd_msg_avp_add( psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
		}
		/* Allocation-Retention-Priority */
		if( !p_soReqInfo.m_coDEPSBQoS.v.m_soARP.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAllocationRetentionPriority, 0, &psoARP ), return 0 );
			/* Priority-Level */
			if( !p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null() ) {
				memset( &soAVPVal, 0, sizeof( soAVPVal ) );
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPriorityLevel, 0, &psoAVPChild ), return 0 );
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
				CHECK_FCT_DO( fd_msg_avp_add( psoARP, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
			}
			/* Pre-emption-Capability */
			if( !p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.is_null() ) {
				memset( &soAVPVal, 0, sizeof( soAVPVal ) );
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPreemptionCapability, 0, &psoAVPChild ), return 0 );
				soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.v;
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
				CHECK_FCT_DO( fd_msg_avp_add( psoARP, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
			}
			/* Pre-emption-Vulnerability */
			if( !p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.is_null() ) {
				memset( &soAVPVal, 0, sizeof( soAVPVal ) );
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictPreemptionVulnerability, 0, &psoAVPChild ), return 0 );
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.v;
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
				CHECK_FCT_DO( fd_msg_avp_add( psoARP, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
			}
			CHECK_FCT_DO( fd_msg_avp_add( psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoARP ), return 0 );
		}
		CHECK_FCT_DO( fd_msg_avp_add( p_psoMsg, MSG_BRW_LAST_CHILD, psoDEPSBQoS ), return 0 );
	}

	return iRetVal;
}

int pcrf_make_QoSInformation( msg *p_psoMsg, SRequestInfo &p_soReqInfo )
{
	int iRetVal = 0;
	avp *psoQoSInfo;
	avp *psoAVPChild;
	avp_value soAVPVal;

	/* QoS-Information */
	if( 0 == p_soReqInfo.m_coQoSInformation.is_null() ) {
	  /* QoS-Information */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDicAVPtQoSInformation, 0, &psoQoSInfo ), return 0 );
		/* APN-Aggregate-Max-Bitrate-DL */
		if( 0 == p_soReqInfo.m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.is_null() ) {
			memset( &soAVPVal, 0, sizeof( soAVPVal ) );
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictAVPAPNAggregateMaxBitrateDL, 0, &psoAVPChild ), return 0 );
			soAVPVal.u32 = p_soReqInfo.m_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
			CHECK_FCT_DO( fd_msg_avp_add( psoQoSInfo, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
		}
		/* APN-Aggregate-Max-Bitrate-UL */
		if( 0 == p_soReqInfo.m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.is_null() ) {
			memset( &soAVPVal, 0, sizeof( soAVPVal ) );
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDicAVPtAPNAggregateMaxBitrateUL, 0, &psoAVPChild ), return 0 );
			soAVPVal.u32 = p_soReqInfo.m_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
			CHECK_FCT_DO( fd_msg_avp_add( psoQoSInfo, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
		}
		CHECK_FCT_DO( fd_msg_avp_add( p_psoMsg, MSG_BRW_LAST_CHILD, psoQoSInfo ), return 0 );
	}

	return iRetVal;
}

int pcrf_make_UMI( msg_or_avp *p_psoMsgOrAVP, const SSessionInfo &p_soSessInfo, const bool p_bIsNeedUMR )
{
  /* если список пуст выходим из функции */
	if( 0 != p_soSessInfo.m_mapMonitInfo.size() ) {
	} else {
		LOG_D( "%s: empty list", __FUNCTION__ );
		return 0;
	}

	LOG_D( "enter: %s;", __FUNCTION__ );

	avp *psoAVPUMI = NULL; /* Usage-Monitoring-Information */
	avp *psoAVPGSU = NULL; /* Granted-Service-Unit */
	avp *psoAVPChild = NULL;
	union avp_value soAVPVal;

	std::map<std::string, SDBMonitoringInfo>::const_iterator iterMonitInfo = p_soSessInfo.m_mapMonitInfo.begin();
	for( ; iterMonitInfo != p_soSessInfo.m_mapMonitInfo.end(); ++iterMonitInfo ) {
	  /* если задана хотябы одна квота */
		if( 0 == iterMonitInfo->second.m_coDosageTotalOctets.is_null()
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
			soAVPVal.os.data = ( uint8_t * ) iterMonitInfo->first.data();
			soAVPVal.os.len = ( size_t ) iterMonitInfo->first.length();
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
		}
		/* дополнительные параметры */
		if( ! iterMonitInfo->second.m_bIsReported ) {
		  /* Usage-Monitoring-Level */
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringLevel, 0, &psoAVPChild ), return __LINE__ );
			soAVPVal.i32 = 1;  /* PCC_RULE_LEVEL */
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
			/* put 'Usage-Monitoring-Level' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
		}
		/* Usage-Monitoring-Report */
		if( p_bIsNeedUMR ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictUsageMonitoringReport, 0, &psoAVPChild ), return __LINE__ );
			soAVPVal.i32 = 0; /* USAGE_MONITORING_REPORT_REQUIRED */
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
			/* put 'Usage-Monitoring-Report' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
		}
		/* Granted-Service-Unit */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictGrantedServiceUnit, 0, &psoAVPGSU ), return __LINE__ );
		/* CC-Total-Octets */
		if( 0 == iterMonitInfo->second.m_coDosageTotalOctets.is_null() ) {
			CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCTotalOctets, 0, &psoAVPChild ), return __LINE__ );
			soAVPVal.u64 = iterMonitInfo->second.m_coDosageTotalOctets.v;
			CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
			/* put 'CC-Total-Octets' into 'Granted-Service-Unit' */
			CHECK_FCT_DO( fd_msg_avp_add( psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
		} else {
		  /* CC-Input-Octets */
			if( 0 == iterMonitInfo->second.m_coDosageInputOctets.is_null() ) {
				CHECK_FCT_DO( fd_msg_avp_new( g_psoDictCCInputOctets, 0, &psoAVPChild ), return __LINE__ );
				soAVPVal.u64 = iterMonitInfo->second.m_coDosageInputOctets.v;
				CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return __LINE__ );
				/* put 'CC-Input-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO( fd_msg_avp_add( psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild ), return __LINE__ );
			}
			/* CC-Output-Octets */
			if( 0 == iterMonitInfo->second.m_coDosageOutputOctets.is_null() ) {
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

int pcrf_extract_req_data( msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo )
{
	int iRetVal = 0;

	/* проверка параметров */
	if( NULL == p_psoMsgInfo->m_psoSessInfo
		|| NULL == p_psoMsgInfo->m_psoReqInfo ) {
		return EINVAL;
	}

	struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	char mcValue[0x100];
	vendor_id_t tVenId;

	/* ищем первую AVP */
	iRetVal = fd_msg_browse_internal( p_psoMsgOrAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if( NULL == psoAVP )
			break;
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		if( AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ) {
			tVenId = psoAVPHdr->avp_vendor;
		} else {
			tVenId = 0;
		}
		switch( tVenId ) {
			case 0: /* Diameter */
				switch( psoAVPHdr->avp_code ) {
					case 8: /* Framed-IP-Address */
						pcrf_ip_addr_to_string( psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len, p_psoMsgInfo->m_psoSessInfo->m_coFramedIPAddress );
						if( psoAVPHdr->avp_value->os.len == sizeof( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress ) ) {
							memcpy( reinterpret_cast< void* >( &p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress ), psoAVPHdr->avp_value->os.data, sizeof( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress ) );
							p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress = ntohl( p_psoMsgInfo->m_psoSessInfo->m_ui32FramedIPAddress );
						}
						break;
					case 30: /* Called-Station-Id */
						p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.v.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.set_non_null();
						break;
					case 263: /* Session-Id */
						p_psoMsgInfo->m_psoSessInfo->m_strSessionId.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						break;
					case 264: /* Origin-Host */
						p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.v.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.set_non_null();
						break;
					case 278: /* Origin-State-Id */
						p_psoMsgInfo->m_psoSessInfo->m_coOriginStateId = psoAVPHdr->avp_value->u32;
						break;
					case 296: /* Origin-Realm */
						p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.v.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.set_non_null();
						break;
					case 295: /* Termination-Cause */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoSessInfo->m_coTermCause = mcValue;
						}
						break;
					case 416: /* CC-Request-Type */
						p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType = psoAVPHdr->avp_value->i32;
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coCCRequestType = mcValue;
						}
						break;
					case 443: /* Subscription-Id */
						pcrf_extract_SubscriptionId( psoAVP, *( p_psoMsgInfo->m_psoSessInfo ) );
						break;
					case 415: /* CC-Request-Number */
						p_psoMsgInfo->m_psoReqInfo->m_coCCRequestNumber = psoAVPHdr->avp_value->u32;
						break;
					case 458: /* User-Equipment-Info */
						pcrf_extract_UEI( psoAVP, *( p_psoMsgInfo->m_psoSessInfo ) );
						break;
				}
				break; /* Diameter */
			case 10415: /* 3GPP */
				switch( psoAVPHdr->avp_code ) {
					case 6: /* 3GPP-SGSN-Address */
						pcrf_ip_addr_to_string( psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len, p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNAddress );
						break;
					case 18: /* 3GPP-SGSN-MCC-MNC */
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNMCCMNC.v.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNMCCMNC.v.insert( 3, 1, '-' );
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNMCCMNC.set_non_null();
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
						break;
					case 21: /* 3GPP-RAT-Type */
						if( !psoAVPHdr->avp_value->os.len )
							break;
						switch( psoAVPHdr->avp_value->os.data[0] ) {
							case 1:
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = "UTRAN";
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
								break;
							case 2:
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = "GERAN";
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
								break;
							case 3:
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = "WLAN";
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
								break;
							case 4:
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = "GAN";
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
								break;
							case 5:
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = "HSPA Evolution";
								p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
								break;
							default:
								UTL_LOG_N( *g_pcoLog, "unknown 3GPP-RAT-Type: '%u'", psoAVPHdr->avp_value->os.data[0] );
								break;
						}
						break;
					case 22: /* 3GPP-User-Location-Info */
						pcrf_parse_user_location( psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc, &p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded );
						break;
					case 515: /* Max-Requested-Bandwidth-DL */
						p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl = psoAVPHdr->avp_value->u32;
						break;
					case 516: /* Max-Requested-Bandwidth-UL */
						p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl = psoAVPHdr->avp_value->u32;
						break;
					case 628: /* Supported-Features */
						pcrf_extract_SF( psoAVP, p_psoMsgInfo->m_psoSessInfo->m_listSF );
						break;
					case 909: /* RAI */
						pcrf_parse_RAI( *psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coRAI );
						break;
					case 1000: /* Bearer-Usage */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coBearerUsage = mcValue;
						}
						break;
					case 1006: /* Event-Trigger */
					{
						p_psoMsgInfo->m_psoReqInfo->m_vectEventTrigger.push_back( psoAVPHdr->avp_value->i32 );
					}
					break;
					case 1009: /* Online */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coOnlineCharging = mcValue;
						}
						break;
					case 1008: /* Offline */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coOfflineCharging = mcValue;
						}
						break;
					case 1016: /* QoS-Information */
						pcrf_extract_QoSInformation( psoAVP, p_psoMsgInfo->m_psoReqInfo->m_coQoSInformation );
						break;
					case 1018: /* Charging-Rule-Report */
						pcrf_extract_ChargingRuleReport( psoAVP, *( p_psoMsgInfo->m_psoReqInfo ) );
						break;
					case 1020: /* Bearer-Identifier */
						if( p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.is_null() ) {
							p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.v.insert( 0, ( const char * ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
							p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.set_non_null();
						}
						break;
					case 1021: /* Bearer-Operation */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
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
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_iIPCANType = psoAVPHdr->avp_value->i32;
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coIPCANType = mcValue;
						}
						break;
					case 1029: /* QoS-Negotiation */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coQoSNegotiation = mcValue;
						}
						break;
					case 1030: /* QoS-Upgrade */
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_coQoSUpgrade = mcValue;
						}
						break;
					case 1032: /* RAT-Type */
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_iRATType = psoAVPHdr->avp_value->i32;
						p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_bLoaded = true;
						if( 0 == pcrf_extract_avp_enum_val( psoAVPHdr, mcValue, sizeof( mcValue ) ) ) {
							p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coRATType = mcValue;
						}
						break;
					case 1049: /* Default-EPS-Bearer-QoS */
						p_psoMsgInfo->m_psoReqInfo->m_coDEPSBQoS.set_non_null();
						pcrf_extract_DefaultEPSBearerQoS( psoAVP, *p_psoMsgInfo->m_psoReqInfo );
						break;
					case 1050: /* AN-GW-Address */
						if( 0 != p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNAddress.is_null() ) {
							sSA4 soAddr;

							memset( &soAddr, 0, sizeof( soAddr ) );
							if( 0 == fd_dictfct_Address_interpret( psoAVPHdr->avp_value, &soAddr ) ) {
								if( AF_INET == soAddr.sin_family ) {
									pcrf_ip_addr_to_string( reinterpret_cast< uint8_t* >( &soAddr.sin_addr.s_addr ), sizeof( soAddr.sin_addr.s_addr ), p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNAddress );
								} else if( AF_INET6 == soAddr.sin_family ) {
									pcrf_ip_addr_to_string( reinterpret_cast< uint8_t* >( &soAddr.sin_addr.s_addr ), sizeof( soAddr.sin_addr.s_addr ), p_psoMsgInfo->m_psoReqInfo->m_soUserEnvironment.m_coSGSNIPv6Address );
								}
							}
						}
						break;
					case 1067: /* Usage-Monitoring-Information */
						pcrf_extract_UMI( psoAVP, *( p_psoMsgInfo->m_psoReqInfo ) );
						break;
				}
				break; /* 3GPP */
			case 2011:  /* Huawai */
				switch( psoAVPHdr->avp_code ) {
					case 2029: /* X-HW-Tethering-Status */
						p_psoMsgInfo->m_psoReqInfo->m_coTetheringFlag = psoAVPHdr->avp_value->u32;
						break;
				}
				break;    /* Huawai */
			case 15397: /* Procera */
				switch( psoAVPHdr->avp_code ) {
					case 777: /* Procera-Tethering-Flag */
						p_psoMsgInfo->m_psoReqInfo->m_coTetheringFlag = psoAVPHdr->avp_value->u32;
						break;
				}
				break; /* Procera */
		}
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

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
		case DIAM_END_USER_E164:
			p_soSessInfo.m_soSubscriptionData.m_coEndUserE164 = strSubscriptionIdData;
			break;
		case DIAM_END_USER_IMSI:
			p_soSessInfo.m_soSubscriptionData.m_coEndUserIMSI = strSubscriptionIdData;
			break;
		case DIAM_END_USER_SIP_URI:
			p_soSessInfo.m_soSubscriptionData.m_coEndUserSIPURI = strSubscriptionIdData;
			break;
		case DIAM_END_USER_NAI:
			p_soSessInfo.m_soSubscriptionData.m_coEndUserNAI = strSubscriptionIdData;
			break;
		case DIAM_END_USER_PRIVATE:
			p_soSessInfo.m_soSubscriptionData.m_coEndUserPrivate = strSubscriptionIdData;
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

int pcrf_extract_ChargingRuleReport (avp *p_psoAVP, SRequestInfo &p_soReqInfo)
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
        soPolicy.m_coPCCRuleStatus = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coPCCRuleStatusEnum = mcValue;
				}
				break;
			case 1031: /* Rule-Failure-Code */
        soPolicy.m_coRuleFailureCode = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coRuleFailureCodeEnum = mcValue;
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
		p_soReqInfo.m_vectCRR.push_back(soPolicy);
	}

	return iRetVal;
}

int pcrf_extract_SF( avp *p_psoAVP, std::list<SSF> &p_listSupportedFeatures )
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	SSF soSF;

	iRetVal = fd_msg_browse_internal( ( void * ) p_psoAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if( NULL == psoAVP )
			break;
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		switch( psoAVPHdr->avp_vendor ) {
			case 0: /* Diameter */
				switch( psoAVPHdr->avp_code ) {
					case 266: /* Vendor-Id */
						soSF.m_ui32VendorId = psoAVPHdr->avp_value->u32;
						break;  /* Vendor-Id */
				}
				break; /* Diameter */
			case 10415: /* 3GPP */
				switch( psoAVPHdr->avp_code ) {
					case 629: /* Feature-List-Id */
						soSF.m_ui32FeatureListID = psoAVPHdr->avp_value->u32;
						break;
					case 630: /* Feature-List */
						soSF.m_ui32FeatureList = psoAVPHdr->avp_value->u32;
						break;
				}
				break;    /* 3GPP */
			default:
				break;
		}
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

	p_listSupportedFeatures.push_back( soSF );

	return iRetVal;
}

int pcrf_extract_UMI( avp *p_psoAVP, SRequestInfo &p_soReqInfo )
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	SSessionUsageInfo soUsageInfo;
	bool bDone = false;

	iRetVal = fd_msg_browse_internal( ( void * ) p_psoAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if( NULL != psoAVP ) {
		} else {
			break;
		}
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		switch( psoAVPHdr->avp_vendor ) {
			case 10415: /* 3GPP */
				switch( psoAVPHdr->avp_code ) {
					case 1066: /* Monitoring-Key */
						bDone = true;
						soUsageInfo.m_coMonitoringKey.v.assign( ( const char* ) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
						soUsageInfo.m_coMonitoringKey.set_non_null();
						break; /* Monitoring-Key */
				}
				break; /* 3GPP */
			case 0:	/* Diameter */
				switch( psoAVPHdr->avp_code ) {
					case 446: /* Used-Service-Unit */
						pcrf_extract_USU( psoAVP, soUsageInfo );
						break;
				}
				break;	/* Diameter */
			default:
				break;
		}
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

	if( bDone ) {
		p_soReqInfo.m_vectUsageInfo.push_back( soUsageInfo );
	}

	return iRetVal;
}

int pcrf_extract_USU( avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo )
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	iRetVal = fd_msg_browse_internal( ( void * ) p_psoAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if( NULL == psoAVP )
			break;
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		switch( psoAVPHdr->avp_vendor ) {
			case 10415: /* 3GPP */
				break;
			case 0:
				switch( psoAVPHdr->avp_code ) {
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
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

	return iRetVal;
}

int pcrf_extract_DefaultEPSBearerQoS( avp *p_psoAVP, SRequestInfo &p_soReqInfo )
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;


	iRetVal = fd_msg_browse_internal( ( void * ) p_psoAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		if( NULL == psoAVP )
			break;
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		switch( psoAVPHdr->avp_vendor ) {
			case 10415: /* 3GPP */
				switch( psoAVPHdr->avp_code ) {
					case 1028: /* QoS-Class-Identifier */
						p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier = psoAVPHdr->avp_value->i32;
						break;
					case 1034: /* Allocation-Retention-Priority */
						p_soReqInfo.m_coDEPSBQoS.v.m_soARP.set_non_null();
						pcrf_extract_DefaultEPSBearerQoS( psoAVP, p_soReqInfo );
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
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

	return iRetVal;
}

int pcrf_extract_QoSInformation( avp *p_psoAVP, otl_value<SQoSInformation> &p_coQoSInformation )
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;

	iRetVal = fd_msg_browse_internal( ( void * ) p_psoAVP, MSG_BRW_FIRST_CHILD, ( void ** ) &psoAVP, NULL );
	if( iRetVal ) {
		return iRetVal;
	}

	do {
	  /* получаем заголовок AVP */
		if( NULL == psoAVP )
			break;
		iRetVal = fd_msg_avp_hdr( psoAVP, &psoAVPHdr );
		if( iRetVal ) {
			break;
		}
		switch( psoAVPHdr->avp_vendor ) {
			case 10415: /* 3GPP */
				switch( psoAVPHdr->avp_code ) {
					case 1040: /* APN-Aggregate-Max-Bitrate-DL */
						p_coQoSInformation.v.m_coAPNAggregateMaxBitrateDL = psoAVPHdr->avp_value->u32;
						break;
					case 1041: /* APN-Aggregate-Max-Bitrate-UL */
						p_coQoSInformation.v.m_coAPNAggregateMaxBitrateUL = psoAVPHdr->avp_value->u32;
						break;
				}
				break; /* 3GPP */
			case 0:  /* Diameter-Base */
				break; /* Diameter-Base */
			default:
				break;
		}
	} while( 0 == fd_msg_browse_internal( ( void * ) psoAVP, MSG_BRW_NEXT, ( void ** ) &psoAVP, NULL ) );

	if( 0 == iRetVal ) {
		p_coQoSInformation.set_non_null();
	}

	return iRetVal;
}

/* загружает описание правил */
int load_rule_info( const std::list<std::string> &p_listRuleList, std::list<SDBAbonRule> &p_listAbonRules )
{
	int iRetVal = 0;
	std::list<std::string>::const_iterator iter = p_listRuleList.begin();

	for( ; iter != p_listRuleList.end(); ++iter ) {
		{
			SDBAbonRule soAbonRule;
			if( 0 == pcrf_rule_cache_get_rule_info( *iter, soAbonRule ) ) {
				p_listAbonRules.push_back( soAbonRule );
			}
		}
	}

	return iRetVal;
}

int pcrf_server_create_abon_rule_list(
	otl_connect *p_pcoDBConn,
	std::string &p_strSubscriberId,
	unsigned int p_uiPeerDialect,
	otl_value<std::string> &p_coIPCANType,
	otl_value<std::string> &p_coRATType,
	otl_value<std::string> &p_coCalledStationId,
	otl_value<std::string> &p_coSGSNAddress,
	otl_value<std::string> &p_coIMEI,
	std::list<SDBAbonRule> &p_listAbonRules )
{
	LOG_D( "enter: %s", __FUNCTION__ );

	int iRetVal = 0;

	/* очищаем список перед выполнением */
	p_listAbonRules.clear();

	do {
		/* список идентификаторов правил абонента */
		std::list<std::string> listRuleList;
		/* загружаем правила абонента */
		pcrf_db_load_abon_rule_list(
			p_pcoDBConn,
			p_strSubscriberId,
			p_uiPeerDialect,
			p_coIPCANType,
			p_coRATType,
			p_coCalledStationId,
			p_coSGSNAddress,
			p_coIMEI,
			listRuleList );
		pcrf_server_load_rule_info( listRuleList, p_uiPeerDialect, p_listAbonRules );
	} while( 0 );

	LOG_D( "leave: %s; result code: %d", __FUNCTION__, iRetVal );

	return iRetVal;
}

int pcrf_server_load_rule_info( const std::list<std::string> &p_listRuleName, const unsigned int p_uiPeerDialect, std::list<SDBAbonRule> &p_listAbonRules )
{
	if( p_listRuleName.size() ) {
		load_rule_info( p_listRuleName, p_listAbonRules );
		/* в случае с SCE нам надо оставить одно правило с наивысшим приоритетом */
		if( p_listAbonRules.size() && GX_CISCO_SCE == p_uiPeerDialect ) {
			SDBAbonRule soAbonRule;
			std::list<SDBAbonRule>::iterator iterList = p_listAbonRules.begin();
			if( iterList != p_listAbonRules.end() ) {
				soAbonRule = *iterList;
				++iterList;
			}
			while( iterList != p_listAbonRules.end() ) {
				if( soAbonRule.m_coPrecedenceLevel.v > iterList->m_coPrecedenceLevel.v )
					soAbonRule = *iterList;
				++iterList;
			}
			p_listAbonRules.clear();
			p_listAbonRules.push_back( soAbonRule );
		}
	}

	return 0;
}

static unsigned int pcrf_server_determine_action_set( SMsgDataForDB &p_soRequestInfo )
{
	unsigned int uiRetVal = 0;
	std::vector<int32_t>::iterator iter = p_soRequestInfo.m_psoReqInfo->m_vectEventTrigger.begin();

	for( ; iter != p_soRequestInfo.m_psoReqInfo->m_vectEventTrigger.end(); ++iter ) {
		switch( *iter ) {
			case 2:	/* RAT_CHANGE */
				uiRetVal |= ACTION_UPDATE_SESSIONCACHE;
				uiRetVal |= ACTION_OPERATE_RULE;
				LOG_D( "session-id: %s; RAT_CHANGE", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 0:  /* SGSN_CHANGE */
			case 13: /* USER_LOCATION_CHANGE */
				uiRetVal |= ACTION_UPDATE_SESSIONCACHE;
				/* Event-Trigger USER_LOCATION_CHANGE */
				switch( p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
					case GX_HW_UGW:
					case GX_ERICSSN:
						if( pcrf_peer_is_dialect_used( GX_PROCERA ) ) {
							uiRetVal |= ACTION_PROCERA_CHANGE_ULI;
						}
						break;
				}
				LOG_D( "session-id: %s; USER_LOCATION_CHANGE", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 20: /* DEFAULT_EPS_BEARER_QOS_CHANGE */
				uiRetVal |= ACTION_COPY_DEFBEARER;
				LOG_D( "session-id: %s; DEFAULT_EPS_BEARER_QOS_CHANGE", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 26: /* USAGE_REPORT */ /* Cisco SCE Gx notation */
				/*uiRetVal |= ACTION_OPERATE_RULE;*/
				uiRetVal |= ACTION_UPDATE_QUOTA;
				LOG_D( "session-id: %s; USAGE_REPORT[26]", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 33: /* USAGE_REPORT */
				/*uiRetVal |= ACTION_OPERATE_RULE;*/
				uiRetVal |= ACTION_UPDATE_QUOTA;
				LOG_D( "session-id: %s; USAGE_REPORT[33]", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 101: /* TETHERING_REPORT */
				if( GX_HW_UGW == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
					uiRetVal |= ACTION_UGW_STORE_THET_INFO;
				}
				LOG_D( "session-id: %s; TETHERING_REPORT", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
			case 777:
				if( GX_PROCERA == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
					uiRetVal |= ACTION_PROCERA_STORE_THET_INFO;
				}
				LOG_D( "session-id: %s; Event-Trigger[777]", p_soRequestInfo.m_psoSessInfo->m_strSessionId.c_str() );
				break;
		}
	}

	/* в ходе тестирования было обнаружено, что Ericsson добавляет Usage-Monitoring-Info в CCR-T */
	/* но при этом не взводит триггер USAGE_REPORT */
	if( GX_ERICSSN == p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect
		&& 0 != p_soRequestInfo.m_psoReqInfo->m_vectUsageInfo.size()
		&& TERMINATION_REQUEST == p_soRequestInfo.m_psoReqInfo->m_iCCRequestType ) {
	/* проверяем, надо ли обращаться к БД */
		std::vector<SSessionUsageInfo>::iterator iter = p_soRequestInfo.m_psoReqInfo->m_vectUsageInfo.begin();
		for( ; iter != p_soRequestInfo.m_psoReqInfo->m_vectUsageInfo.end(); ++iter ) {
			if( 0 == iter->m_coCCInputOctets.is_null() && 0 != iter->m_coCCInputOctets.v
				|| 0 == iter->m_coCCOutputOctets.is_null() && 0 != iter->m_coCCOutputOctets.v
				|| 0 == iter->m_coCCTotalOctets.is_null() && 0 != iter->m_coCCTotalOctets.v ) {
			  /* запись содержит ненулевые значения потребленного трафика */
			  /* вектор содержит полезную информацию */
				uiRetVal |= ACTION_UPDATE_QUOTA;
				break;
			}
		}
	}

	if( INITIAL_REQUEST == p_soRequestInfo.m_psoReqInfo->m_iCCRequestType ) {
		uiRetVal |= ACTION_OPERATE_RULE;
	}

	if( GX_HW_UGW != p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect && GX_ERICSSN != p_soRequestInfo.m_psoSessInfo->m_uiPeerDialect ) {
		uiRetVal |= ACTION_OPERATE_RULE;
	}

	return uiRetVal;
}

void pcrf_make_mk_list(
	std::list<SDBAbonRule> &p_listAbonRules,
	std::map<std::string, SDBMonitoringInfo> &p_mapMonitInfo )
{
	std::list<SDBAbonRule>::iterator iterRule;
	std::vector<std::string>::iterator iterMonitKey;

	/* обходим все правила из списка */
	for( iterRule = p_listAbonRules.begin(); iterRule != p_listAbonRules.end(); ++iterRule ) {
	  /* если очередное правило релевантно и неактивно (т.е. впоследствие будет инсталлировано) */
		if( iterRule->m_bIsRelevant && ! iterRule->m_bIsActive ) {
		  /* обходим все ключи мониторинга подлежащего инсталляции правила */
			for( iterMonitKey = iterRule->m_vectMonitKey.begin(); iterMonitKey != iterRule->m_vectMonitKey.end(); ++iterMonitKey ) {
			  /* добавляем (или пытаемся добавить) их в список ключей мониторинга сессии */
				p_mapMonitInfo.insert( std::pair<std::string, SDBMonitoringInfo>( *iterMonitKey, SDBMonitoringInfo() ) );
			}
		}
	}
}

int pcrf_server_look4stalledsession( SSessionInfo *p_psoSessInfo )
{
	if( NULL != p_psoSessInfo ) {
	} else {
		return EINVAL;
	}

	std::list<std::string> listSessionId;
	std::list<std::string>::iterator iterList;

	CHECK_FCT( pcrf_session_cache_index_frameIPAddress_get_sessionList( p_psoSessInfo->m_coFramedIPAddress.v, listSessionId ) );

	iterList = listSessionId.begin();

	for( ; iterList != listSessionId.end(); ++iterList ) {
		pcrf_local_refresh_queue_add( static_cast< time_t >( 0 ), *iterList, "session_id", "abort_session" );
	}

	return 0;
}

int pcrf_server_find_core_sess_byframedip( std::string &p_strFramedIPAddress, SSessionInfo &p_soSessInfo )
{
	int iRetVal = 0;
	std::list<std::string> listSessionId;
	std::string strSessionId;

	CHECK_FCT( pcrf_session_cache_index_frameIPAddress_get_sessionList( p_strFramedIPAddress, listSessionId ) );

	if( 0 < listSessionId.size() ) {
	  /* если список не пустой */
	  /* берем данные из конца списка, т.к. это самое релевантное значение */
		strSessionId = listSessionId.back();
		CHECK_FCT( pcrf_session_cache_get( strSessionId, &p_soSessInfo, NULL, NULL ) );
	} else {
		iRetVal = ENODATA;
	}

	return iRetVal;
}

int pcrf_server_find_IPCAN_sess_byframedip( otl_value<std::string> &p_coIPAddr, SSessionInfo &p_soIPCANSessInfo )
{
	if( 0 == p_coIPAddr.is_null() ) {
		return ( pcrf_server_find_core_sess_byframedip( p_coIPAddr.v, p_soIPCANSessInfo ) );
	} else {
		LOG_E( "Framed-IP-Address is empty" );
		return EINVAL;
	}
}
