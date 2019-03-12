#include <pthread.h>

#include "../app_pcrf.h"
#include "pcrf_defaultRuleSelector.h"
#include "pcrf_timed_oper.h"

extern CLog *g_pcoLog;
static pthread_mutex_t g_mutexThreadCounter;
static unsigned int g_uiThreadCount;

struct STimedData {
	/* copy of subscriber data */
	SSubscriberData								*m_psoData;
	/* data which will be getting while processing */
	std::list<SDBAbonRule>						*m_plistAbonRules;
	std::map<std::string, SDBMonitoringInfo>	*m_pmapMonitInfo;
	unsigned int								 m_uiActionSet;
	/* control */
	volatile int								 m_iInit;
	volatile int								 m_iAborted;
	pthread_mutex_t								 m_mutexDataReady;
	pthread_mutex_t								 m_mutexDataCopied;
	/* constructor */
	STimedData(
		SSubscriberData *p_psoData,
		std::list<SDBAbonRule> *p_plistAbonRules,
		std::map<std::string, SDBMonitoringInfo> *p_pmapMonitInfo );
	/* destructor */
	~STimedData();
};

static bool pcrf_subscriber_data_increment_counter();
static void pcrf_subscriber_data_decrement_counter();
static void * pcrf_subscriber_data_timed_oper( void* );
static void pcrf_subscriber_data_load_def_quota( const std::vector< SSessionUsageInfo > &p_vecUsageInfo, std::map<std::string, SDBMonitoringInfo> &p_mapMonitoringInfo );

int pcrf_subscriber_data_init()
{
	CHECK_FCT( pthread_mutex_init( &g_mutexThreadCounter, NULL ) );
}

void pcrf_subscriber_data_fini()
{
	CHECK_FCT_DO( pthread_mutex_destroy( &g_mutexThreadCounter ), /* continue */ );
}

struct SSubscriberData *
	pcrf_subscriber_data_prepare( int32_t									&p_iCCRType,
								  std::string								&p_strSubscriberId,
								  unsigned int								&p_uiPeerDialect,
								  std::vector<SSessionUsageInfo>			&p_vectUsageInfo,
								  SUserEnvironment							&p_coUserEnvironment,
								  otl_value<std::string>					&p_coCalledStationId,
								  otl_value<std::string>					&p_coIMEI,
								  std::vector<SDBAbonRule>					&p_vectActive,
								  unsigned int								&p_uiActionSet,
								  std::list<SDBAbonRule>					&p_listAbonRules,
								  std::map<std::string, SDBMonitoringInfo>	&p_mapMonitInfo )
{
	return new SSubscriberData( p_iCCRType,
								p_strSubscriberId,
								p_uiPeerDialect,
								p_vectUsageInfo,
								p_coUserEnvironment,
								p_coCalledStationId,
								p_coIMEI,
								p_vectActive,
								p_uiActionSet,
								p_listAbonRules,
								p_mapMonitInfo );
}

int pcrf_subscriber_data_proc( SSubscriberData *p_psoSubscriberData )
{
	int iRetVal;
	STimedData *psoThreadData = new STimedData( p_psoSubscriberData, new std::list<SDBAbonRule>, new std::map<std::string, SDBMonitoringInfo> );

	if( 2 == psoThreadData->m_iInit ) {
	} else {
		return EINVAL;
	}

	if( pcrf_subscriber_data_increment_counter() ) {
		pthread_t threadOper;

		CHECK_FCT( pthread_create( &threadOper, NULL, pcrf_subscriber_data_timed_oper, psoThreadData ) );
		UTL_LOG_D( *g_pcoLog, "thread was launched successfully" );

		CHECK_FCT( pthread_detach( threadOper ) );

		timespec soTimeSpec;

		CHECK_FCT( pcrf_make_timespec_timeout( soTimeSpec, g_psoConf->m_uiCCATimeoutSec, g_psoConf->m_uiCCATimeoutUSec ) );
		iRetVal = pthread_mutex_timedlock( &psoThreadData->m_mutexDataReady, &soTimeSpec );
		UTL_LOG_D( *g_pcoLog, "mutex was released with %u code", iRetVal );

		pcrf_subscriber_data_decrement_counter();
	} else {
		iRetVal = EBUSY;
	}
	if( 0 == iRetVal ) {
		UTL_LOG_D( *g_pcoLog, "started data copying" );
		/* copying rule list */
		std::list<SDBAbonRule>::iterator iterAbonRules = psoThreadData->m_plistAbonRules->begin();
		for( ; iterAbonRules != psoThreadData->m_plistAbonRules->end(); ++iterAbonRules ) {
			p_psoSubscriberData->m_listAbonRules.push_back( *iterAbonRules );
			UTL_LOG_D( *g_pcoLog, "data copyed: rule name: %s", iterAbonRules->m_strRuleName.c_str() );
		}

		/* copying monitoring keys */
		std::map<std::string, SDBMonitoringInfo>::iterator iterMonitInfo = psoThreadData->m_pmapMonitInfo->begin();
		for( ; iterMonitInfo != psoThreadData->m_pmapMonitInfo->end(); ++iterMonitInfo ) {
			p_psoSubscriberData->m_mapMonitInfo.insert( *iterMonitInfo );
			UTL_LOG_D( *g_pcoLog, "data copyed: monitoring key: %s", iterMonitInfo->first.c_str() );
		}

		/* copying action set */
		p_psoSubscriberData->m_uiActionSet = psoThreadData->m_uiActionSet;
	} else {
		UTL_LOG_E( *g_pcoLog,
				   "Subscriber-Id: '%s': error accurred while data gethering: code: '%u'. It will be default rule list using",
				   p_psoSubscriberData->m_strSubscriberId.c_str(), iRetVal );
		UTL_LOG_D( *g_pcoLog, "started default rule list creation" );
		/* aborting data retrieval */
		psoThreadData->m_iAborted = 1;

		/* getting of default rules */
		std::list<std::string> listRuleList;

		if( INITIAL_REQUEST == p_psoSubscriberData->m_i32CCRType ) {
			pcrf_drs_get_defaultRuleList( p_psoSubscriberData, &listRuleList );
			pcrf_server_load_rule_info( listRuleList, p_psoSubscriberData->m_uiPeerDialect, p_psoSubscriberData->m_listAbonRules );
		} else if( UPDATE_REQUEST == p_psoSubscriberData->m_i32CCRType ) {
			pcrf_subscriber_data_load_def_quota( p_psoSubscriberData->m_vectUsageInfo, p_psoSubscriberData->m_mapMonitInfo );
		}
	}

	CHECK_FCT( pthread_mutex_unlock( &psoThreadData->m_mutexDataCopied ) );

	return iRetVal;
}

static bool pcrf_subscriber_data_increment_counter()
{
	bool bRetVal = true;

	CHECK_FCT_DO( pthread_mutex_lock( &g_mutexThreadCounter ), return false );
	if( g_uiThreadCount < g_psoConf->m_uiMaxCCRHandlers ) {
		++g_uiThreadCount;
	} else {
		bRetVal = false;
		UTL_LOG_E( *g_pcoLog, "maximum thread number reached: %u; maximum alowed: %u", g_uiThreadCount, g_psoConf->m_uiMaxCCRHandlers );
	}
	CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexThreadCounter ), return false );

	return bRetVal;
}

static void pcrf_subscriber_data_decrement_counter()
{
	CHECK_FCT_DO( pthread_mutex_lock( &g_mutexThreadCounter ), return );
	--g_uiThreadCount;
	CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexThreadCounter ), return );
}

static void * pcrf_subscriber_data_timed_oper( void * p_pvParam )
{
	STimedData *psoParam = reinterpret_cast< STimedData* >( p_pvParam );
	int iFnRes;
	otl_connect *pcoDBConn = NULL;

	do {
		UTL_LOG_D(
			*g_pcoLog,
			"CCRType: %u; Subscriber-Id: %s; Action-Set: %u",
			psoParam->m_psoData->m_i32CCRType,
			psoParam->m_psoData->m_strSubscriberId.c_str(),
			psoParam->m_psoData->m_uiActionSet );
	#ifdef DEBUG
		iFnRes = pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC );
	#else
		iFnRes = pcrf_db_pool_get( &pcoDBConn, NULL, USEC_PER_SEC );
	#endif
		if( 0 == iFnRes && NULL != pcoDBConn ) {
			UTL_LOG_D( *g_pcoLog, "it has got DB connection %p", pcoDBConn );
		} else {
			UTL_LOG_N( *g_pcoLog, "can not to retrieve DB connection: code: %u", iFnRes );
			break;
		}

		if( 0 == psoParam->m_iAborted ) {
		} else {
			UTL_LOG_D( *g_pcoLog, "execution was aborted in %u line", __LINE__ );
			break;
		}

		/* если надо обновить квоты */
		if( psoParam->m_uiActionSet & ACTION_UPDATE_QUOTA ) {
			int iUpdateRule = 0;

			/* сохраняем информацию о потреблении трафика, загружаем информации об оставшихся квотах */
			iFnRes = pcrf_db_session_usage( pcoDBConn,
								   psoParam->m_psoData->m_strSubscriberId,
								   *psoParam->m_pmapMonitInfo,
								   psoParam->m_psoData->m_vectUsageInfo,
								   iUpdateRule );
			if( 0 == iUpdateRule ) {
			} else {
				psoParam->m_uiActionSet |= ACTION_OPERATE_RULE;
			}
			if( 0 == iFnRes ) {
			} else {
				UTL_LOG_D( *g_pcoLog, "it has got error while pcrf_db_session_usage executed: code: %u", iFnRes );
			}
		}

		if( 0 == psoParam->m_iAborted ) {
		} else {
			UTL_LOG_D( *g_pcoLog, "execution was aborted in %u line", __LINE__ );
			break;
		}

		if( psoParam->m_uiActionSet & ACTION_OPERATE_RULE ) {
			/* загружаем из БД правила абонента */
			iFnRes = pcrf_server_create_abon_rule_list( pcoDBConn,
														psoParam->m_psoData->m_strSubscriberId,
														psoParam->m_psoData->m_uiPeerDialect,
														psoParam->m_psoData->m_soUserEnvironment.m_coIPCANType,
														psoParam->m_psoData->m_soUserEnvironment.m_coRATType,
														psoParam->m_psoData->m_coCalledStationId,
														psoParam->m_psoData->m_soUserEnvironment.m_coSGSNAddress,
														psoParam->m_psoData->m_coIMEI,
														*psoParam->m_plistAbonRules );
			if( 0 == iFnRes ) {
				UTL_LOG_D( *g_pcoLog, "pcrf_server_create_abon_rule_list was executed successfully: rule number: %u", psoParam->m_plistAbonRules->size() );
			} else {
				UTL_LOG_D( *g_pcoLog, "it has got error while pcrf_server_create_abon_rule_list executed: code: %u", iFnRes );
			}
		}

		if( 0 == psoParam->m_iAborted ) {
		} else {
			UTL_LOG_D( *g_pcoLog, "execution was aborted in %u line", __LINE__ );
			break;
		}

		if( psoParam->m_uiActionSet & ACTION_OPERATE_RULE ) {
			if( psoParam->m_psoData->m_i32CCRType != INITIAL_REQUEST ) {
				/* формируем список неактуальных правил */
				pcrf_server_select_notrelevant_active( *psoParam->m_plistAbonRules, psoParam->m_psoData->m_vectActive );
			}

			if( 0 == psoParam->m_iAborted ) {
			} else {
				UTL_LOG_D( *g_pcoLog, "execution was aborted in %u line", __LINE__ );
				break;
			}
				
			/* формируем список ключей мониторинга */
			pcrf_make_mk_list( *psoParam->m_plistAbonRules, *psoParam->m_pmapMonitInfo );
			/* запрашиваем информацию о ключах мониторинга */
			iFnRes = pcrf_server_db_monit_key( pcoDBConn,
											   psoParam->m_psoData->m_strSubscriberId,
											   *psoParam->m_pmapMonitInfo );
			if( 0 == iFnRes ) {
				UTL_LOG_D( *g_pcoLog, "pcrf_server_db_monit_key was executed successfully: monitoring key number: %u", psoParam->m_pmapMonitInfo->size() );
			} else {
				UTL_LOG_D( *g_pcoLog, "it has got error while pcrf_server_db_monit_key executed: code: %u", iFnRes );
			}
		}
	} while( 0 );

	/* снимаем блокировку */
	CHECK_FCT_DO( pthread_mutex_unlock( &psoParam->m_mutexDataReady ), /* continue */ );
	UTL_LOG_D( *g_pcoLog, "data ready" );

	/* освобождаем объект класса взаимодействия с БД */
	if( NULL != pcoDBConn ) {
		CHECK_POSIX_DO( pcrf_db_pool_rel( reinterpret_cast< void * >( pcoDBConn ), __FUNCTION__ ), /* continue */ );
	}

	CHECK_FCT_DO( pthread_mutex_lock( &psoParam->m_mutexDataCopied ), /* report about problem in log-file */ );

	delete psoParam;
	UTL_LOG_D( *g_pcoLog, "thread data was destroyed" );

	return NULL;
}

static void pcrf_subscriber_data_load_def_quota( const std::vector< SSessionUsageInfo > &p_vecUsageInfo, std::map<std::string, SDBMonitoringInfo> &p_mapMonitoringInfo)
{
	std::vector<SSessionUsageInfo>::const_iterator iterUsage;

	for( iterUsage = p_vecUsageInfo.begin(); iterUsage != p_vecUsageInfo.end(); ++iterUsage ) {
		{
			SDBMonitoringInfo soMonitInfo;

			soMonitInfo.m_bIsReported = true;

			if( 0 == iterUsage->m_coCCTotalOctets.is_null() ) {
				soMonitInfo.m_coDosageTotalOctets = g_psoConf->m_uiDefaultQuota;
			}
			if( 0 == iterUsage->m_coCCOutputOctets.is_null() ) {
				soMonitInfo.m_coDosageOutputOctets = g_psoConf->m_uiDefaultQuota;
			}
			if( 0 == iterUsage->m_coCCInputOctets.is_null() ) {
				soMonitInfo.m_coDosageInputOctets = g_psoConf->m_uiDefaultQuota;
			}
			p_mapMonitoringInfo.insert( std::pair<std::string, SDBMonitoringInfo>( iterUsage->m_coMonitoringKey.v, soMonitInfo ) );
			UTL_LOG_D( *g_pcoLog,
					   "default quota: key: '%s'; total: '%u'; output: '%u'; input: '%u'",
					   iterUsage->m_coMonitoringKey.v.c_str(),
					   0 == soMonitInfo.m_coDosageTotalOctets.is_null() ? soMonitInfo.m_coDosageTotalOctets.v : 0,
					   0 == soMonitInfo.m_coDosageOutputOctets.is_null() ? soMonitInfo.m_coDosageOutputOctets.v : 0,
					   0 == soMonitInfo.m_coDosageInputOctets.is_null() ? soMonitInfo.m_coDosageInputOctets.v : 0 );
		}
	}
}

STimedData::STimedData(
	SSubscriberData *p_psoData,
	std::list<SDBAbonRule> *p_plistAbonRules,
	std::map<std::string, SDBMonitoringInfo> *p_pmapMonitInfo ) :
	m_psoData( p_psoData ), m_plistAbonRules( p_plistAbonRules ), m_pmapMonitInfo( p_pmapMonitInfo ), m_uiActionSet( p_psoData->m_uiActionSet ), m_iInit( 0 ), m_iAborted( 0 )
{
	/* инициализация мьютекса готовности данных */
	if( 0 == pthread_mutex_init( &m_mutexDataReady, NULL ) && 0 == pthread_mutex_lock( &m_mutexDataReady ) ) {
		m_iInit = 1;
	} else {
		return;
	}

	/* инициализация мьютекса завершения копирования */
	if( 0 == pthread_mutex_init( &m_mutexDataCopied, NULL ) && 0 == pthread_mutex_lock( &m_mutexDataCopied ) ) {
		m_iInit = 2;
	} else {
		return;
	}
}

STimedData::~STimedData()
{
	if( NULL != m_plistAbonRules ) {
		delete m_plistAbonRules;
		m_plistAbonRules = NULL;
	}
	if( NULL != m_pmapMonitInfo ) {
		delete m_pmapMonitInfo;
		m_pmapMonitInfo = NULL;
	}
	if( 1 <= m_iInit ) {
		CHECK_FCT_DO( pthread_mutex_unlock( &m_mutexDataReady ), /* continue */ );
		CHECK_FCT_DO( pthread_mutex_destroy( &m_mutexDataReady ), /* continue */ );
	}
	if( 2 == m_iInit ) {
		CHECK_FCT_DO( pthread_mutex_unlock( &m_mutexDataCopied ), /* continue */ );
		CHECK_FCT_DO( pthread_mutex_destroy( &m_mutexDataCopied ), /* continue */ );
	}
	if( NULL != m_psoData ) {
		delete m_psoData;
		m_psoData = NULL;
	}
}

SSubscriberData::SSubscriberData( int32_t &p_iCCRType,
								  std::string &p_strSubscriberId,
								  unsigned int &p_uiPeerDialect,
								  std::vector<SSessionUsageInfo> &p_vectUsageInfo,
								  SUserEnvironment &p_coUserEnvironment,
								  otl_value<std::string> &p_coCalledStationId,
								  otl_value<std::string> &p_coIMEI,
								  std::vector<SDBAbonRule> &p_vectActive,
								  unsigned int &p_uiActionSet,
								  std::list<SDBAbonRule> &p_listAbonRules,
								  std::map<std::string, SDBMonitoringInfo> &p_mapMonitInfo )
	:
	m_i32CCRType( p_iCCRType ),
	m_strSubscriberId( p_strSubscriberId ),
	m_uiPeerDialect( p_uiPeerDialect ),
	m_vectUsageInfo( p_vectUsageInfo ),
	m_soUserEnvironment( p_coUserEnvironment ),
	m_coCalledStationId( p_coCalledStationId ),
	m_coIMEI( p_coIMEI ),
	m_vectActive( p_vectActive ),
	m_uiActionSet( p_uiActionSet ),
	m_listAbonRules( p_listAbonRules ),
	m_mapMonitInfo( p_mapMonitInfo )
{
}
