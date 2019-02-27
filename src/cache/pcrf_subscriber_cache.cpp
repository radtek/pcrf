#include <errno.h>
#include <unordered_map>

#include "../app_pcrf.h"
#include "../pcrf_otl.h"
#include "utils/log/log.h"
#include "pcrf_cache.h"
#include "pcrf_subscriber_cache.h"

extern int g_iCacheWork;
extern CLog *g_pcoLog;
/* area of Subscription Data: key = [DIAM_END_USER_E164 | DIAM_END_USER_IMSI | DIAM_END_USER_SIP_URI | DIAM_END_USER_NAI | DIAM_END_USER_PRIVATE]; value = SubscriberId */
static std::unordered_map<std::string, std::string> *g_pmmapSubscriptionData;

static int  pcrf_subscriber_cache_load_list( std::unordered_map<std::string, std::string> *p_pmmapSubscrData );
static void pcrf_subscriber_cache_insert( std::unordered_map<std::string, std::string> *p_pmmapSubscrData, const int p_iSubscriberIdType, const otl_value<std::string> &p_coValue, const std::string &p_strSubscriberId );
static bool pcrf_subscriber_cache_check_data( const int p_iSubscriberIdType, const otl_value<std::string> &p_strData, std::string &p_strSubscriberId );

int pcrf_subscriber_cache_init()
{
	int iRetVal = 0;

	g_pmmapSubscriptionData = new std::unordered_map<std::string, std::string>[5];

	CHECK_FCT_DO(
		( iRetVal = pcrf_subscriber_cache_load_list( g_pmmapSubscriptionData ) ),
		delete [] g_pmmapSubscriptionData; g_pmmapSubscriptionData = NULL; return iRetVal );

	UTL_LOG_N( *g_pcoLog,
			   "subscriber cache is initialized successfully!\n"
			   "\tEND_USER_E164:'%u' records\n"
			   "\tEND_USER_IMSI:'%u' records\n"
			   "\tEND_USER_SIP_URI:'%u' records\n"
			   "\tEND_USER_NAI:'%u' records\n"
			   "\tEND_USER_PRIVATE:'%u' records",
			   g_pmmapSubscriptionData[DIAM_END_USER_E164].size(),
			   g_pmmapSubscriptionData[DIAM_END_USER_IMSI].size(),
			   g_pmmapSubscriptionData[DIAM_END_USER_SIP_URI].size(),
			   g_pmmapSubscriptionData[DIAM_END_USER_NAI].size(),
			   g_pmmapSubscriptionData[DIAM_END_USER_PRIVATE].size() );

	return iRetVal;
}

void pcrf_subscriber_cache_fini()
{
	if( NULL != g_pmmapSubscriptionData ) {
		delete [] g_pmmapSubscriptionData;
		g_pmmapSubscriptionData = NULL;
	}
}

int pcrf_subscriber_cache_reload()
{
	if( 0 != g_iCacheWork ) {
	} else {
		return ECANCELED;
	}

	int iRetVal = 0;
	std::unordered_map<std::string, std::string> *pmmapNew = new std::unordered_map<std::string, std::string>[5];

	CHECK_FCT_DO( ( iRetVal = pcrf_subscriber_cache_load_list( pmmapNew ) ), delete [] pmmapNew; return iRetVal );

	std::unordered_map<std::string, std::string> *pcoTmp;

	pcoTmp = g_pmmapSubscriptionData;

	CHECK_FCT_DO( ( iRetVal = pcrf_cache_rwlock_wrlock() ), delete pmmapNew; return iRetVal );
	g_pmmapSubscriptionData = pmmapNew;
	CHECK_FCT_DO( ( iRetVal = pcrf_cache_rwlock_unlock() ),/* continue */ );

	if( NULL != pcoTmp ) {
		delete [] pcoTmp;
	}

	return iRetVal;
}

int pcrf_subscriber_cache_get_subscriber_id( const SSubscriptionIdData &p_soSubscrData, std::string &p_strSubscriberId )
{
	int iRetVal = 0;

	p_strSubscriberId.clear();

	do {
		/* checking IMSI */
		if( pcrf_subscriber_cache_check_data( DIAM_END_USER_IMSI, p_soSubscrData.m_coEndUserIMSI.v, p_strSubscriberId ) ) {
		} else {
			iRetVal = EINVAL;
			break;
		}

		/* checking E164 */
		if( pcrf_subscriber_cache_check_data( DIAM_END_USER_E164, p_soSubscrData.m_coEndUserE164.v, p_strSubscriberId ) ) {
		} else {
			iRetVal = EINVAL;
			break;
		}

		/* checking SIP URI */
		if( pcrf_subscriber_cache_check_data( DIAM_END_USER_SIP_URI, p_soSubscrData.m_coEndUserSIPURI.v, p_strSubscriberId ) ) {
		} else {
			iRetVal = EINVAL;
			break;
		}

		/* checking NAI */
		if( pcrf_subscriber_cache_check_data( DIAM_END_USER_NAI, p_soSubscrData.m_coEndUserNAI.v, p_strSubscriberId ) ) {
		} else {
			iRetVal = EINVAL;
			break;
		}

		/* checking PRIVATE */
		if( pcrf_subscriber_cache_check_data( DIAM_END_USER_PRIVATE, p_soSubscrData.m_coEndUserPrivate.v, p_strSubscriberId ) ) {
		} else {
			iRetVal = EINVAL;
			break;
		}
	} while( 0 );

	if( 0 == iRetVal ) {
	} else {
		p_strSubscriberId.clear();
	}

	return iRetVal;
}

static int  pcrf_subscriber_cache_load_list( std::unordered_map<std::string, std::string> *p_pmmapSubscrData )
{
	if( 0 != g_iCacheWork ) {
	} else {
		return ECANCELED;
	}

	int iRetVal = 0;
	int iFnRes;
	otl_connect *pcoDBConn = NULL;

	iFnRes = pcrf_db_pool_get( &pcoDBConn, NULL, USEC_PER_SEC );
	if( 0 == iFnRes && NULL != pcoDBConn ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "%s: can not to get db connection", __FUNCTION__ );
		return EBUSY;
	}

	try {
		otl_stream coStream(
			1000,
			"select sd.subscriber_id, sd.end_user_e164, sd.end_user_imsi, sd.end_user_sip_uri, sd.end_user_nai, sd.end_user_private from ps.subscription_data sd",
			*pcoDBConn );
		std::string strSubscriberId;
		otl_value<std::string> coIMSI;
		otl_value<std::string> coE164;
		otl_value<std::string> coSIPURI;
		otl_value<std::string> coUserNAI;
		otl_value<std::string> coUserPrivate;

		while( 0 == coStream.eof() && 0 != g_iCacheWork ) {
			coStream
				>> strSubscriberId
				>> coIMSI
				>> coE164
				>> coSIPURI
				>> coUserNAI
				>> coUserPrivate;
			pcrf_subscriber_cache_insert( p_pmmapSubscrData, DIAM_END_USER_E164, coIMSI, strSubscriberId );
			pcrf_subscriber_cache_insert( p_pmmapSubscrData, DIAM_END_USER_IMSI, coE164, strSubscriberId );
			pcrf_subscriber_cache_insert( p_pmmapSubscrData, DIAM_END_USER_SIP_URI, coSIPURI, strSubscriberId );
			pcrf_subscriber_cache_insert( p_pmmapSubscrData, DIAM_END_USER_NAI, coUserNAI, strSubscriberId );
			pcrf_subscriber_cache_insert( p_pmmapSubscrData, DIAM_END_USER_PRIVATE, coUserPrivate, strSubscriberId );
		}
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
		iRetVal = coExcept.code;
	}

	if( NULL != pcoDBConn ) {
		pcrf_db_pool_rel( pcoDBConn, NULL );
	}

	return iRetVal;
}

static void pcrf_subscriber_cache_insert( std::unordered_map<std::string, std::string> *p_pmmapSubscrData, const int p_iSubscriberIdType, const otl_value<std::string> &p_coValue, const std::string &p_strSubscriberId )
{
	if( 0 <= p_iSubscriberIdType && p_iSubscriberIdType < 5 ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "%s: unsupported Subscriber-Id-Type: '%u'", __FUNCTION__, p_iSubscriberIdType );
		return;
	}

	if( 0 == p_coValue.is_null() && 0 != p_strSubscriberId.length() ) {
		std::pair<std::unordered_map<std::string, std::string>::iterator, bool> pairInsertResult;
		pairInsertResult = p_pmmapSubscrData[p_iSubscriberIdType].insert( std::pair<std::string, std::string>( p_coValue.v, p_strSubscriberId ) );
		if( pairInsertResult.second ) {
		} else {
			UTL_LOG_E( *g_pcoLog, "%s: duplication: type: '%d'; value: '%s'; subscriber-id: '%s'", __FUNCTION__, p_iSubscriberIdType, p_coValue.v.c_str(), p_strSubscriberId.c_str() );
			return;
		}
	} else {
		return;
	}
}

static bool pcrf_subscriber_cache_check_data( const int p_iSubscriberIdType, const otl_value<std::string> &p_coData, std::string &p_strSubscriberId )
{
	bool bRetVal = true;

	if( 0 == p_coData.is_null() ) {
	} else {
		return bRetVal;
	}

	if( 0 <= p_iSubscriberIdType && p_iSubscriberIdType < 5 ) {
	} else {
		UTL_LOG_E( *g_pcoLog, "%s: unsupported Subscriber-Id-Type: '%u'", __FUNCTION__, p_iSubscriberIdType );
		return bRetVal;
	}

	std::unordered_map<std::string, std::string>::iterator iterData;

	CHECK_FCT_DO( pcrf_cache_rwlock_rdlock(), return false );

	iterData = g_pmmapSubscriptionData[p_iSubscriberIdType].find( p_coData.v );
	if( iterData != g_pmmapSubscriptionData[p_iSubscriberIdType].end() ) {
	} else {
		return bRetVal;
	}

	if( 0 == p_strSubscriberId.length() ) {
		p_strSubscriberId.assign( iterData->second );
	} else {
		if( p_strSubscriberId == iterData->second ) {
		} else {
			return false;
		}
	}

	CHECK_FCT_DO( pcrf_cache_rwlock_unlock(), /* continue */ );

	return bRetVal;
}
