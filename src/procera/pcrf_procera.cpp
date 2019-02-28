#include <unordered_map>
#include "../app_pcrf.h"
#include "pcrf_procera.h"

static std::unordered_map<std::string, int32_t> *g_pmapTethering = NULL;

static int pcrf_procera_make_uli_rule( otl_value<std::string> &p_coULI, SDBAbonRule &p_soAbonRule );

int pcrf_procera_terminate_session( std::string &p_strIPCANSessionId )
{
	if( 0 != pcrf_peer_is_dialect_used( GX_PROCERA ) ) {
	} else {
		return EINVAL;
	}

	int iRetVal = 0;
	std::vector<SSessionInfo> vectSessList;

	pcrf_procera_db_load_sess_list( p_strIPCANSessionId, vectSessList );
	for( std::vector<SSessionInfo>::iterator iter = vectSessList.begin(); iter != vectSessList.end(); ++iter ) {
		pcrf_local_refresh_queue_add( iter->m_strSessionId );
	}

	return iRetVal;
}

int pcrf_procera_additional_rules(
	otl_value<std::string> &p_coIMEI,
	otl_value<std::string> &p_coCalledStationId,
	otl_value<std::string> &p_coECGI,
	otl_value<std::string> &p_coCGI,
	std::list<SDBAbonRule> &p_listAbonRules )
{
	int iRetVal = 0;

	SDBAbonRule soAbonRule( false, true );
	std::string strValue;

	soAbonRule.m_coDynamicRuleFlag = 0;
	soAbonRule.m_coRuleGroupFlag = 0;

	/* добавляем IMEISV */
	if( !p_coIMEI.is_null() ) {
		strValue = "/IMEISV/" + p_coIMEI.v;
		soAbonRule.m_strRuleName = strValue;
		p_listAbonRules.push_back( soAbonRule );
	}

	/* добавляем APN */
	if( !p_coCalledStationId.is_null() ) {
		strValue = "/APN/" + p_coCalledStationId.v;
		soAbonRule.m_strRuleName = strValue;
		p_listAbonRules.push_back( soAbonRule );
	}

	/* user location */
	pcrf_procera_make_uli_rule( 0 == p_coECGI.is_null() ? p_coECGI : p_coCGI, soAbonRule );

	p_listAbonRules.push_back( soAbonRule );

	return iRetVal;
}

int pcrf_procera_change_uli(
	otl_connect *p_pcoDBConn,
	std::string &p_strSessionId,
	otl_value<std::string> &p_coECGI,
	otl_value<std::string> &p_coCGI )
{
	SDBAbonRule soAbonRule( false, true );
	std::vector<SDBAbonRule> vectOldRule;
	std::list<SDBAbonRule> listNewRule;
	std::vector<SSessionInfo> vectSessList;

	/* обрабатыаем новую локацию */
	soAbonRule.m_coDynamicRuleFlag = 0;
	soAbonRule.m_coRuleGroupFlag = 0;

	pcrf_procera_make_uli_rule( 0 == p_coECGI.is_null() ? p_coECGI : p_coCGI, soAbonRule );
	listNewRule.push_back( soAbonRule );

	CHECK_FCT_DO( pcrf_procera_db_load_sess_list( p_strSessionId, vectSessList ), return 0 );

	SMsgDataForDB soReqInfo;
	pcrf_server_DBstruct_init( &soReqInfo );
	for( std::vector<SSessionInfo>::iterator iter = vectSessList.begin(); iter != vectSessList.end(); ++iter ) {
		CHECK_FCT_DO( pcrf_procera_db_load_location_rule( p_pcoDBConn, iter->m_strSessionId, vectOldRule ), break );
		*soReqInfo.m_psoSessInfo = *iter;
		CHECK_FCT_DO( pcrf_client_gx_rar( soReqInfo.m_psoSessInfo, soReqInfo.m_psoReqInfo, &vectOldRule, &listNewRule, NULL, NULL, 0 ), break );
	}
	pcrf_server_DBStruct_cleanup( &soReqInfo );

	return 0;
}

int pcrf_procera_make_subscription_id( msg *p_soAns, otl_value<std::string> &p_coEndUserIMSI, otl_value<std::string> &p_coEndUserE164 )
{
	int iRetVal = 0;
	avp *psoAVPCI = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;

	/* Subscription-Id */
	/* IMSI */
	if( ! p_coEndUserIMSI.is_null() ) {
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionId, 0, &psoAVPCI ), return 0 );
		/* Subscription-Id-Type */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdType, 0, &psoAVPChild ), return 0 );
		soAVPVal.i32 = 1; /* END_USER_IMSI */
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
	  /* put Subscription-Id-Type into Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
	  /* Subscription-Id-Data */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdData, 0, &psoAVPChild ), return 0 );
		soAVPVal.os.data = ( uint8_t* ) p_coEndUserIMSI.v.c_str();
		soAVPVal.os.len = p_coEndUserIMSI.v.length();
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
	  /* put Subscription-Id-Data into Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
	  /* put Subscription-Id into answer */
		CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPCI ), return 0 );
	}
	/* E164 */
	if( ! p_coEndUserE164.is_null() ) {
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionId, 0, &psoAVPCI ), return 0 );
		/* Subscription-Id-Type */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdType, 0, &psoAVPChild ), return 0 );
		soAVPVal.i32 = 0; /* END_USER_E164 */
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
	  /* put Subscription-Id-Type into Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );
	  /* Subscription-Id-Data */
		CHECK_FCT_DO( fd_msg_avp_new( g_psoDictSubscriptionIdData, 0, &psoAVPChild ), return 0 );
		soAVPVal.os.data = ( uint8_t* ) p_coEndUserE164.v.c_str();
		soAVPVal.os.len = p_coEndUserE164.v.length();
		CHECK_FCT_DO( fd_msg_avp_setvalue( psoAVPChild, &soAVPVal ), return 0 );
	  /* put Subscription-Id-Data into Subscription-Id */
		CHECK_FCT_DO( fd_msg_avp_add( psoAVPCI, MSG_BRW_LAST_CHILD, psoAVPChild ), return 0 );

	  /* put Subscription-Id into answer */
		CHECK_FCT_DO( fd_msg_avp_add( p_soAns, MSG_BRW_LAST_CHILD, psoAVPCI ), return 0 );
	}

	return iRetVal;
}

int pcrf_procera_oper_thetering_report( SMsgDataForDB &p_soRequestInfo, std::list<SDBAbonRule> &p_listAbonRule, std::vector<SDBAbonRule> &p_vectActiveRule )
{
	SDBAbonRule soRule;
	std::unordered_map<std::string, int32_t>::iterator iter;

	soRule.m_coDynamicRuleFlag = 0;
	soRule.m_coRuleGroupFlag = 0;
	soRule.m_strRuleName = "/PSM/Policies/Redirect_blocked_device";

	if( NULL != g_pmapTethering && NULL != p_soRequestInfo.m_psoSessInfo ) {
	} else {
		g_pmapTethering = new std::unordered_map<std::string, int32_t>;
	}

	iter = g_pmapTethering->find( p_soRequestInfo.m_psoSessInfo->m_strSessionId );
	if( iter == g_pmapTethering->end() ) {
		if( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.is_null() ) {
		} else {
			switch( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.v ) {
				case 0:
					soRule.m_bIsActive = true;
					p_vectActiveRule.push_back( soRule );
				default:
					soRule.m_bIsRelevant = true;
					p_listAbonRule.push_back( soRule );
					g_pmapTethering->insert( std::make_pair( p_soRequestInfo.m_psoSessInfo->m_strSessionId, 1 ) );
			}
		}
	} else {
		if( p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.is_null() ) {
			switch( iter->second ) {
				case 0:
					g_pmapTethering->erase( iter );
					break;
				default:
					soRule.m_bIsRelevant = true;
					p_listAbonRule.push_back( soRule );
			}
		} else {
			if( 0 == p_soRequestInfo.m_psoReqInfo->m_coTetheringFlag.v ) {
				g_pmapTethering->erase( iter );
			} else {
				soRule.m_bIsRelevant = true;
				p_listAbonRule.push_back( soRule );
				iter->second = 1;
			}
		}
	}
}

static int pcrf_procera_make_uli_rule( otl_value<std::string> &p_coULI, SDBAbonRule &p_soAbonRule )
{
	int iRetVal = 0;

	if( p_coULI.is_null() ) {
	  /* если локация пользователя не задана */
		p_soAbonRule.m_strRuleName = "/User-Location/inRoaming";
	} else {
	  /* выбираем необходимые данные */
		std::size_t iBSPos = std::string::npos, iSectorPos;
		iSectorPos = p_coULI.v.find_last_of( '-' );
		if( std::string::npos != iSectorPos ) {
			iBSPos = p_coULI.v.find_last_of( '-', iSectorPos - 1 );
		} else {
			return EINVAL;
		}
		/* если найдены все необходимые разделители */
		if( std::string::npos != iBSPos ) {
			p_soAbonRule.m_strRuleName = "/User-Location/";
			if( iSectorPos - iBSPos > 1 ) {
				p_soAbonRule.m_strRuleName += p_coULI.v.substr( iBSPos + 1, iSectorPos - iBSPos - 1 );
			} else {
				return EINVAL;
			}
			p_soAbonRule.m_strRuleName += '/';
			p_soAbonRule.m_strRuleName += p_coULI.v.substr( iSectorPos + 1 );
		} else {
			return EINVAL;
		}
	}
	p_soAbonRule.m_coDynamicRuleFlag = 0;
	p_soAbonRule.m_coRuleGroupFlag = 0;

	return iRetVal;
}
