#include <string.h>
#include <string>
#include <unordered_map>

#include "../pcrf_otl.h"
#include "pcrf_defaultRuleSelector.h"

extern CLog *g_pcoLog;

struct SDefaultRuleSelectorData {
	std::unordered_multimap<std::string, std::string> m_mmapCondition;
};

static std::unordered_map<std::string, SDefaultRuleSelectorData> g_mapDefaultRule;

SDefaultRuleSelectorData * pcrf_drs_create()
{
	return new SDefaultRuleSelectorData;
}

int pcrf_drs_add_selector( SDefaultRuleSelectorData *p_psoSelectorData, const char *p_pszParameter, const char *p_pszValue )
{
	p_psoSelectorData->m_mmapCondition.insert( std::pair<std::string, std::string>( p_pszParameter, p_pszValue ) );

	return 0;
}

int pcrf_drs_add_defaultRule( const SDefaultRuleSelectorData * p_psoSelectorData, const char *p_pszRuleName )
{
	std::pair<std::unordered_map<std::string, SDefaultRuleSelectorData>::iterator, bool> pariInsertResult;

	pariInsertResult = g_mapDefaultRule.insert( std::pair<std::string, SDefaultRuleSelectorData>( p_pszRuleName, *p_psoSelectorData ) );
	if( pariInsertResult.second ) {
	} else {
		std::unordered_multimap<std::string, std::string>::const_iterator iterData = p_psoSelectorData->m_mmapCondition.begin();
		for( ; iterData != p_psoSelectorData->m_mmapCondition.end(); ++iterData ) {
			pariInsertResult.first->second.m_mmapCondition.insert( std::pair<std::string, std::string>( iterData->first, iterData->second ) );
		}
	}
	delete p_psoSelectorData;

	return 0;
}

static bool pcrf_drs_check_parameter( const SDefaultRuleSelectorData &p_soDataSelector, const char *p_pszParam, const char *p_pstrValue )
{
	bool bRetVal = false;
	std::unordered_multimap<std::string, std::string>::const_iterator iterValues = p_soDataSelector.m_mmapCondition.find( p_pszParam );

	if( iterValues != p_soDataSelector.m_mmapCondition.end() ) {
		for( ; iterValues != p_soDataSelector.m_mmapCondition.end() && 0 == iterValues->first.compare( p_pszParam ); ++iterValues ) {
			UTL_LOG_D( *g_pcoLog, "parameter: '%s'; checking value: '%s'; checked value: '%s'", p_pszParam, iterValues->second.c_str(), p_pstrValue );
			if( 0 == iterValues->second.compare( p_pstrValue ) ) {
				bRetVal = true;
				UTL_LOG_D( *g_pcoLog, "parameter: '%s'; value '%s' fits for default rule", p_pszParam, p_pstrValue );
				break;
			} else {
				UTL_LOG_D( *g_pcoLog, "parameter: '%s'; value '%s' does not fit for default rule", p_pszParam, p_pstrValue );
			}
		}
	} else {
		bRetVal = true;
	}

	return bRetVal;
}

int pcrf_drs_get_defaultRuleList( SSubscriberData *p_psoSubscriberData, std::list<std::string> *p_plistRuleName )
{
	/* обходим весь список */
	std::unordered_map<std::string, SDefaultRuleSelectorData>::iterator iterRule = g_mapDefaultRule.begin();
	const char *pszParamName;
	const char *pszParamValue;

	for( ; iterRule != g_mapDefaultRule.end(); ++iterRule ) {
		UTL_LOG_D( *g_pcoLog, "default rule name: '%s'", iterRule->first.c_str() );
		if( 0 == p_psoSubscriberData->m_soUserEnvironment.m_coRATType.is_null() ) {
			pszParamName = "RAT_TYPE";
			pszParamValue = p_psoSubscriberData->m_soUserEnvironment.m_coRATType.v.c_str();
			if( pcrf_drs_check_parameter( iterRule->second, pszParamName, pszParamValue ) ) {
			} else {
				continue;
			}
		}
		if( 0 == p_psoSubscriberData->m_soUserEnvironment.m_coSGSNAddress.is_null() ) {
			pszParamName = "SGSN_ADDRESS";
			pszParamValue = p_psoSubscriberData->m_soUserEnvironment.m_coSGSNAddress.v.c_str();
			if( pcrf_drs_check_parameter( iterRule->second, pszParamName, pszParamValue ) ) {
			} else {
				continue;
			}
		}
		if( 0 == p_psoSubscriberData->m_soUserEnvironment.m_coIPCANType.is_null() ) {
			pszParamName = "IP_CAN_TYPE";
			pszParamValue = p_psoSubscriberData->m_soUserEnvironment.m_coIPCANType.v.c_str();
			if( pcrf_drs_check_parameter( iterRule->second, pszParamName, pszParamValue ) ) {
			} else {
				continue;
			}
		}
		if( 0 == p_psoSubscriberData->m_soUserEnvironment.m_coIPCANType.is_null() ) {
			pszParamName = "PEER_DIALECT";
			pszParamValue = PEER_DIALECT_NAME( p_psoSubscriberData->m_uiPeerDialect );
			if( pcrf_drs_check_parameter( iterRule->second, pszParamName, pszParamValue ) ) {
			} else {
				continue;
			}
		}
		UTL_LOG_D( *g_pcoLog, "default rule '%s' selected for '%s'", iterRule->first.c_str(), p_psoSubscriberData->m_strSubscriberId.c_str() );
		p_plistRuleName->push_back( iterRule->first );
	}
}
