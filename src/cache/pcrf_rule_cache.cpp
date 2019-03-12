#include <errno.h>

#include "../app_pcrf.h"
#include "../app_pcrf_header.h"
#include "../pcrf_otl.h"
#include "utils/log/log.h"
#include "pcrf_cache.h"
#include "pcrf_rule_cache.h"

extern CLog *g_pcoLog;
extern int g_iCacheWork;

/* локальное хранилище описания правил */
static std::map<std::string, SDBAbonRule> *g_pmapRule;

/* загрузка правил из БД */
static int pcrf_rule_cache_load_rule_list(std::map<std::string,SDBAbonRule> *p_pmapRule);
/* загрузка Flow-Description правила */
static int load_rule_flows(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<SFlowInformation> &p_vectRuleInformation);
/* загрузка ключей мониторинга sce */
static int load_sce_rule_mk(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectMonitKey);

int pcrf_rule_cache_init()
{
	int iRetVal = 0;

	g_pmapRule = new std::map<std::string, SDBAbonRule>;

	CHECK_FCT_DO(
		( iRetVal = pcrf_rule_cache_load_rule_list( g_pmapRule ) ),
		delete g_pmapRule; g_pmapRule = NULL; return iRetVal );

	UTL_LOG_N( *g_pcoLog,
			   "rule cache is initialized successfully!\n"
			   "\trule count is '%u'\n"
			   "\tstorage capasity is '%u' records",
			   g_pmapRule->size(),
			   g_pmapRule->max_size() );

	return iRetVal;
}

void pcrf_rule_cache_fini()
{
	if( NULL != g_pmapRule ) {
		delete g_pmapRule;
		g_pmapRule = NULL;
	}
}

int pcrf_rule_cache_reload()
{
	if( 0 != g_iCacheWork ) {
	} else {
		return ECANCELED;
	}

	int iRetVal = 0;
	std::map<std::string, SDBAbonRule> *pmapNew = NULL;
	std::map<std::string, SDBAbonRule> *pmapTmp;

	pmapNew = new std::map<std::string, SDBAbonRule>;

	CHECK_FCT_DO( ( iRetVal = pcrf_rule_cache_load_rule_list( pmapNew ) ), delete pmapNew; return iRetVal );

	pmapTmp = g_pmapRule;

	/* lock for writing */
	CHECK_FCT_DO( ( iRetVal = pcrf_cache_rwlock_wrlock() ), delete pmapNew; return iRetVal );
	g_pmapRule = pmapNew;
	CHECK_FCT_DO( ( iRetVal = pcrf_cache_rwlock_unlock() ), /* continue */ );

	if( NULL != pmapTmp ) {
		delete pmapTmp;
	}

	return iRetVal;
}

static int pcrf_rule_cache_load_rule_list( std::map<std::string, SDBAbonRule> *p_pmapRule )
{
	if( 0 != g_iCacheWork ) {
	} else {
		return ECANCELED;
	}

	CTimeMeasurer coTM;
	int iRetVal = 0;
	int iRepeat = 1;

	otl_connect *pcoDBConn = NULL;

	if( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
	} else {
		iRetVal = -1;
		goto clean_and_exit;
	}

sql_repeat:
	p_pmapRule->clear();

	try {
		otl_nocommit_stream coStream;
		otl_value<std::string> coMonitKey;
		unsigned int uiRuleId;

		coStream.open(
			50,
			"select "
			"r.rule_name,"
			"r.id,"
			"r.dynamic_rule_flag,"
			"r.rule_group_flag,"
			"r.precedence_level,"
			"r.rating_group_id,"
			"r.service_id,"
			"r.metering_method,"
			"r.online_charging,"
			"r.offline_charging,"
			"qt.qos_class_identifier,"
			"qt.max_requested_bandwidth_ul,"
			"qt.max_requested_bandwidth_dl,"
			"qt.guaranteed_bitrate_ul,"
			"qt.guaranteed_bitrate_dl,"
			"mk.key_code,"
			"rs.redirect_address_type,"
			"rs.redirect_server_address "
			"from "
			"ps.rule r "
			"left join ps.qos_template qt on r.qos_template_id = qt.id "
			"left join ps.monitoring_key mk on r.monitoring_key_id = mk.id "
			"left join ps.redirection_server rs on r.redirection_server_id = rs.id",
			*pcoDBConn );
		while( 0 == coStream.eof() && 0 != g_iCacheWork ) {
			{
				SDBAbonRule soAbonRule( false, true );
				coStream
					>> soAbonRule.m_strRuleName
					>> uiRuleId
					>> soAbonRule.m_coDynamicRuleFlag
					>> soAbonRule.m_coRuleGroupFlag
					>> soAbonRule.m_coPrecedenceLevel
					>> soAbonRule.m_coRatingGroupId
					>> soAbonRule.m_coServiceId
					>> soAbonRule.m_coMeteringMethod
					>> soAbonRule.m_coOnlineCharging
					>> soAbonRule.m_coOfflineCharging
					>> soAbonRule.m_coQoSClassIdentifier
					>> soAbonRule.m_coMaxRequestedBandwidthUl
					>> soAbonRule.m_coMaxRequestedBandwidthDl
					>> soAbonRule.m_coGuaranteedBitrateUl
					>> soAbonRule.m_coGuaranteedBitrateDl
					>> coMonitKey
					>> soAbonRule.m_coRedirectAddressType
					>> soAbonRule.m_coRedirectServerAddress;
				  /* обрабатываем ключ мониторинга */
				if( 0 == coMonitKey.is_null() ) {
					soAbonRule.m_vectMonitKey.push_back( coMonitKey.v );
				}
				/* загружаем Flow-Description правила */
				CHECK_FCT_DO( load_rule_flows( pcoDBConn, uiRuleId, soAbonRule.m_vectFlowDescr ), /* continue */ );
				/* сохраняем правило в локальном хранилище */
				p_pmapRule->insert( std::pair<std::string, SDBAbonRule>( soAbonRule.m_strRuleName, soAbonRule ) );
			}
		}
		coStream.close();
		coStream.open(
			50,
			"select "
			"r.id,"
			"r.precedence_level,"
			"r.name,"
			"r.PACKAGE,"
			"r.REAL_TIME_MONITOR "
			"from "
			"ps.SCE_rule r",
			*pcoDBConn );
		while( 0 == coStream.eof() && 0 != g_iCacheWork ) {
			{
				SDBAbonRule soAbonRule( false, true );
				coStream
					>> uiRuleId
					>> soAbonRule.m_coPrecedenceLevel
					>> soAbonRule.m_strRuleName
					>> soAbonRule.m_coSCE_PackageId
					>> soAbonRule.m_coSCE_RealTimeMonitor;
				CHECK_FCT_DO( load_sce_rule_mk( pcoDBConn, uiRuleId, soAbonRule.m_vectMonitKey ), /* continue */ );
				p_pmapRule->insert( std::pair<std::string, SDBAbonRule>( soAbonRule.m_strRuleName, soAbonRule ) );
			}
		}
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'; var info: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text, coExcept.var_info );
		if( 0 != iRepeat && 1 == pcrf_db_pool_restore( pcoDBConn ) ) {
			--iRepeat;
			goto sql_repeat;
		}
		iRetVal = coExcept.code;
	}

clean_and_exit:
	if( NULL != pcoDBConn ) {
		pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
		pcoDBConn = NULL;
	}

	return iRetVal;
}

static int load_rule_flows( otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<SFlowInformation> &p_vectRuleInfo )
{
	if( NULL != p_pcoDBConn ) {
	} else {
		return EINVAL;
	}

	int iRetVal = 0;
	int iRepeat = 1;

sql_repeat:
	p_vectRuleInfo.clear();

	otl_nocommit_stream coStream;
	try {
		SFlowInformation soFlowInfo;
		coStream.open(
			25,
			"select "
			"ft.flow_description "
			"from "
			"ps.rule_flows rf "
			"inner join ps.flow_template ft on rf.flow_template_id = ft.id "
			"where rf.rule_id = :rule_id/*unsigned*/",
			*p_pcoDBConn );
		coStream
			<< p_uiRuleId;
		while( 0 == coStream.eof() ) {
			coStream >> soFlowInfo.m_coFlowDescription;
			p_vectRuleInfo.push_back( soFlowInfo );
		}
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
		if( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
			--iRepeat;
			goto sql_repeat;
		}
		iRetVal = coExcept.code;
	}

	return iRetVal;
}

static int load_sce_rule_mk( otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectMonitKey )
{
	if( NULL != p_pcoDBConn ) {
	} else {
		return EINVAL;
	}

	int iRetVal = 0;
	int iRepeat = 1;

sql_repeat:
	p_vectMonitKey.clear();

	try {
		otl_nocommit_stream coStream;
		std::string strMonitKey;

		coStream.open(
			25,
			"select "
			"mk.key_code "
			"from "
			"ps.sce_monitoring_key s "
			"inner join ps.monitoring_key mk on s.monitoring_key_id = mk.id "
			"where "
			"s.sce_rule_id = :rule_id /*unsigned*/",
			*p_pcoDBConn );
		coStream
			<< p_uiRuleId;
		while( 0 == coStream.eof() ) {
			coStream >> strMonitKey;
			p_vectMonitKey.push_back( strMonitKey );
		}
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
		if( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
			--iRepeat;
			goto sql_repeat;
		}
		iRetVal = coExcept.code;
	}

	return iRetVal;
}

/* загружает описание правила */
int pcrf_rule_cache_get_rule_info( const std::string &p_strRuleName, SDBAbonRule &p_soRule )
{
	CTimeMeasurer coTM;

	int iRetVal = 0;
	std::map<std::string, SDBAbonRule>::iterator iter;

	CHECK_FCT_DO( ( iRetVal = pcrf_cache_rwlock_rdlock() ), return iRetVal );
	iter = g_pmapRule->find( p_strRuleName );
	if( iter != g_pmapRule->end() ) {
		p_soRule = iter->second;
	} else {
		UTL_LOG_E( *g_pcoLog, "rule '%s' was not found in rule cache", p_strRuleName.c_str() );
		iRetVal = ENODATA;
	}
	CHECK_FCT( pcrf_cache_rwlock_unlock() );

	return iRetVal;
}
