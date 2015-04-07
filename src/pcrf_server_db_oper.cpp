#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <string.h>
#include <time.h>
#include <vector>

/* добавление записи в список сессий */
int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* обновление записи в таблице сессий */
int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* добавление записи в таблицу запросов */
int pcrf_db_insert_request (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo);
/* добавление записи в таблицу потребления трафика */
int pcrf_db_session_usage(otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo);

/* обновление записи в таблице выданых политик */
int pcrf_db_update_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SSessionPolicyInfo &p_soPoliciInfo);
/* закрываем записи в таблице выданных политик */
int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo);

int pcrf_server_DBstruct_init (struct SMsgDataForDB *p_psoMsgToDB)
{
	int iRetVal = 0;

	/* инициализируем структуру */
	memset (p_psoMsgToDB, 0, sizeof (*p_psoMsgToDB));

	/* виделяем память для хранения данных запроса */
	p_psoMsgToDB->m_psoSessInfo = new SSessionInfo;
	p_psoMsgToDB->m_psoReqInfo = new SRequestInfo;

	return iRetVal;
}

int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;
	int iFnRes = 0;

	do {
		/* проверка параметров */
		if (NULL == p_psoMsgInfo->m_psoSessInfo
				|| NULL == p_psoMsgInfo->m_psoReqInfo) {
			iRetVal = EINVAL;
			break;
		}

		dict_avp_request_ex soAVPParam;
		char mcValue[0x10000];

		switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
		case 1: /* INITIAL_REQUEST */
			/* фиксируем дату начала сессии */
			p_psoMsgInfo->m_psoSessInfo->m_coTimeStart = p_psoMsgInfo->m_psoSessInfo->m_coTimeLast;
			iFnRes = pcrf_db_insert_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			break;
		case 3: /* TERMINATION_REQUEST */
			/* фиксируем дату завершения сессии */
			p_psoMsgInfo->m_psoSessInfo->m_coTimeEnd = p_psoMsgInfo->m_psoSessInfo->m_coTimeLast;
		case 2: /* UPDATE_REQUEST */
		case 4: /* EVENT_REQUEST */
			/* выполянем запрос на обновление записи */
			iFnRes = pcrf_db_update_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			/* обрабатываем информацию о выданных политиках */
			iFnRes = pcrf_server_policy_db_store (p_coDBConn, p_psoMsgInfo);
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			break;
		default:
			break;
		}

		if (iRetVal) {
			break;
		}

		/* сохраняем запрос в таблице запросов */
		iFnRes = pcrf_db_insert_request (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *(p_psoMsgInfo->m_psoReqInfo));
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		/* сохраняем информацию о потреблении трафика, загружаем информации об оставшихся квотах */
		iFnRes = pcrf_db_session_usage(p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *(p_psoMsgInfo->m_psoReqInfo));
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
	} while (0);

	return iRetVal;
}

int pcrf_server_policy_db_store (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;
	int iFnRes;

	/* проверка параметров */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
			|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case 3: /* TERMINATION_REQUEST */
		/* сначала фиксируем информацию, полученную в запросе */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			iFnRes = pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
		}
		if (iRetVal) {
			break;
		}
		/* потом закрываем оставшиеся сессии */
		iFnRes = pcrf_db_close_session_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		break;
	case 2: /* UPDATE_REQUEST */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			iFnRes = pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
		}
		if (iRetVal) {
			break;
		}
		break;
	case 4: /* EVENT_REQUEST */
		break;
	default:
		break;
	}

	return iRetVal;
}

void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo)
{
	/* освобождаем занятую память */
	if (p_psoMsgInfo->m_psoSessInfo) {
		p_psoMsgInfo->m_psoSessInfo->m_vectCRR.clear ();
		delete p_psoMsgInfo->m_psoSessInfo;
		p_psoMsgInfo->m_psoSessInfo = NULL;
	}
	if (p_psoMsgInfo->m_psoReqInfo) {
		delete p_psoMsgInfo->m_psoReqInfo;
		p_psoMsgInfo->m_psoReqInfo = NULL;
	}
}

void fill_otl_datetime (otl_datetime &p_coOtlDateTime, tm &p_soTime)
{
	p_coOtlDateTime.year = p_soTime.tm_year + 1900;
	p_coOtlDateTime.month = p_soTime.tm_mon + 1;
	p_coOtlDateTime.day = p_soTime.tm_mday;
	p_coOtlDateTime.hour = p_soTime.tm_hour;
	p_coOtlDateTime.minute = p_soTime.tm_min;
	p_coOtlDateTime.second = p_soTime.tm_sec;
}

int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		/* выполняем запрос на добавление записи о сессии */
		coStream.open (
			1,
			"insert into ps.SessionList (session_id,subscriber_id,origin_host,origin_realm,end_user_imsi,end_user_e164,imeisv,time_start,time_last_req,framed_ip_address,called_station_id,sgsn_address,ip_can_type)"
			"values(:session_id/*char[64]*/,:subscriber_id/*char[64]*/,:origin_host/*char[255]*/,:origin_realm/*char[255]*/,:end_user_imsi/*char[16]*/,:end_user_e164/*char[16]*/,:imeisv/*char[20]*/,:time_start/*timestamp*/,:time_last_req/*timestamp*/,:framed_ip_address/*char[16]*/,:called_station_id/*char[255]*/,:sgsn_address/*char[16]*/,:ip_can_type/*char[64]*/)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_strSubscriberId
			<< p_soSessInfo.m_coOriginHost
			<< p_soSessInfo.m_coOriginRealm
			<< p_soSessInfo.m_coEndUserIMSI
			<< p_soSessInfo.m_coEndUserE164
			<< p_soSessInfo.m_coIMEI
			<< p_soSessInfo.m_coTimeStart
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coFramedIPAddress
			<< p_soSessInfo.m_coCalledStationId
			<< p_soSessInfo.m_coSGSNAddress
			<< p_soSessInfo.m_coIPCANType;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		/* запрос на обновление записи о сессии */
		coStream.open (
			1,
			"update ps.SessionList"
			" set time_end = :1<timestamp>, time_last_req = :2<timestamp>, termination_cause = :3<char[64]>"
			" where session_id = :4<char[255]>",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeEnd
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coTermCause
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}
}

int pcrf_db_insert_request (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		std::vector<SSessionUsageInfo>::iterator iter;
		coStream.set_commit (0);
		/* запрос на добавление записи о запросе */
		coStream.open (
			1,
			"insert into ps.RequestList"
			" (session_id,time_event,cc_request_type,cc_request_number,bearer_identifier,online_charging,offline_charging,qos_upgrade,qos_class_identifier,max_requested_bandwidth_ul,max_requested_bandwidth_dl,guaranteed_bitrate_ul,guaranteed_bitrate_dl,rat_type,qos_negotiation,sgsn_mcc_mnc,user_location_info,rai,bearer_usage,bearer_operation)"
			" values (:1<char[255]>,:2<timestamp>,:3<char[64]>,:4<unsigned>,:5<char[255]>,:6<char[64]>,:7<char[64]>,:8<char[64]>,:9<char[64]>,:10<unsigned>,:11<unsigned>,:12<unsigned>,:13<unsigned>,:14<char[64]>,:15<char[64]>,:16<char[16]>,:17<char[255]>,:18<char[255]>,:19<char[64]>,:20<char[64]>)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_coTimeLast
			<< p_soReqInfo.m_coCCRequestType
			<< p_soReqInfo.m_coCCRequestNumber
			<< p_soReqInfo.m_coBearerIdentifier
			<< p_soReqInfo.m_coOnlineCharging
			<< p_soReqInfo.m_coOfflineCharging
			<< p_soReqInfo.m_coQoSUpgrade
			<< p_soReqInfo.m_coQoSClassIdentifier
			<< p_soReqInfo.m_coMaxRequestedBandwidthUl
			<< p_soReqInfo.m_coMaxRequestedBandwidthDl
			<< p_soReqInfo.m_coGuaranteedBitrateUl
			<< p_soReqInfo.m_coGuaranteedBitrateDl
			<< p_soReqInfo.m_coRATType
			<< p_soReqInfo.m_coQoSNegotiation
			<< p_soReqInfo.m_coSGSNMCCMNC
			<< p_soReqInfo.m_coUserLocationInfo
			<< p_soReqInfo.m_coRAI
			<< p_soReqInfo.m_coBearerUsage
			<< p_soReqInfo.m_coBearerOperation;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_db_session_usage(otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		std::vector<SSessionUsageInfo>::iterator iter;
		coStream.set_commit(0);
		coStream.open(
			1,
			"begin ps.qm.ProcessQuota("
				":SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
				":UsedInputOctets /*ubigint,in*/, :UsedOutputOctets /*ubigint,in*/, :UsedTotalOctets /*ubigint,in*/,"
				":GrantedInputOctets /*ubigint,out*/, :GrantedOutputOctets /*ubigint,out*/, :GrantedTotalOctets /*ubigint,out*/);"
			"end; ",
			p_coDBConn);
		iter = p_soReqInfo.m_vectUsageInfo.begin();
		while (iter != p_soReqInfo.m_vectUsageInfo.end()) {
			coStream
				<< p_soSessInfo.m_strSubscriberId
				<< iter->m_coMonitoringKey
				<< iter->m_coCCInputOctets
				<< iter->m_coCCOutputOctets
				<< iter->m_coCCTotalOctets;
			LOG_N("%s: %s;%s;%'llu;%'llu;%'llu;",
				__func__,
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.v);
			coStream
				>> iter->m_coCCInputOctets
				>> iter->m_coCCOutputOctets
				>> iter->m_coCCTotalOctets;
			LOG_N("%s: %s;%s;%'llu;%'llu;%'llu;",
				__func__,
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.v);
			++iter;
		}
		p_coDBConn.commit();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_db_insert_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SDBAbonRule &p_soRule)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		char mcRuleName[255];
		size_t stStrLen;
		switch (p_soRule.m_soRuleId.m_uiProtocol) {
		case 1: /* Gx */
			stStrLen = p_soRule.m_coRuleName.v.length () > sizeof (mcRuleName) - 1 ? sizeof (mcRuleName) - 1 : p_soRule.m_coRuleName.v.length ();
			p_soRule.m_coRuleName.v.copy (
				mcRuleName,
				stStrLen);
			mcRuleName[stStrLen] = '\0';
			break; /* Gx */
		case 2: /* Cisco SCE Gx */
			sprintf (mcRuleName, "%u", p_soRule.m_coSCE_PackageId.v);
			break; /* Cisco SCE Gx */
		}
		coStream.open (
			1,
			"insert into ps.SessionPolicy "
			"(session_id,time_start,rule_id,rule_name,protocol_id) "
			"values (:session_id /* char[255] */, :time_start /* timestamp */, :rule_id /* unsigned */, :rule_name /* char[255] */, :protocol_id /* unsigned */)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_coTimeLast
			<< p_soRule.m_soRuleId.m_uiRuleId
			<< mcRuleName
			<< p_soRule.m_soRuleId.m_uiProtocol;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_db_update_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SSessionPolicyInfo &p_soPoliciInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		coStream.open (
			1,
			"update ps.SessionPolicy "
				"set time_end = :time_end /* timestamp */, "
				"charging_rule_name = :charging_rule_name /* char[255] */, "
				"pcc_rule_status = :pcc_rule_status /* char[64] */, "
				"rule_failure_code = :rule_failure_code /* char[64]*/ "
			"where "
				"session_id = :session_id /* char[255]*/ "
				"and rule_name = :rule_name /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soPoliciInfo.m_coChargingRuleName
			<< p_soPoliciInfo.m_coPCCRuleStatus
			<< p_soPoliciInfo.m_coRuleFailureCode
			<< p_soSessInfo.m_coSessionId
			<< p_soPoliciInfo.m_coChargingRuleName;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		coStream.open (
			1,
			"update "
				"ps.SessionPolicy "
			"set "
				"time_end = :time_end /* timestamp */ "
			"where "
				"session_id = :session_id /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.clean();
		}
	}

	return iRetVal;
}

int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SRuleId &p_soRuleId)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		coStream.open (
			1,
			"update "
				"ps.SessionPolicy "
			"set "
				"time_end = :time_end /* timestamp */ "
			"where "
				"session_id = :session_id /* char[255] */ "
				"and rule_id = :rule_id /* unsigned */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coSessionId
			<< p_soRuleId.m_uiRuleId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* загружает идентификатор абонента (subscriber_id) из БД */
int pcrf_server_db_load_abon_id (otl_connect &p_coDBConn, SMsgDataForDB &p_soMsgInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		/* выполняем запрос к БД */
		coStream.open (
			1,
			"select "
			  "Subscriber_id "
			"from "
			  "ps.Subscription_Data "
			"where "
			  "(end_user_e164 is null or end_user_e164 = :end_user_e164 /* char[64] */) "
			  "and (end_user_imsi is null or end_user_imsi = :end_user_imsi /* char[100] */) "
			  "and (end_user_sip_uri is null or end_user_sip_uri = :end_user_sip_uri /* char[100] */) "
			  "and (end_user_nai is null or end_user_nai = :end_user_nai /* char[100] */) "
			  "and (end_user_private is null or end_user_private = :end_user_private /* char[100] */)",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserE164
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserSIPURI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserNAI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserPrivate;
		coStream
			>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId;
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_db_load_active_rules (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		/* определяем по какому протоколу работает пир */
		unsigned int uiProtocol;
		coStream.open (
			1,
			"select "
				"protocol_id "
			"from "
				"ps.peer "
			"where "
				"host_name = :host_name /* char[100] */ "
				"and realm = :realm /* char[100] */",
			p_coDBConn);
		coStream
			<< p_soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v;
		coStream
			>> uiProtocol;
		coStream.close ();
		/* загружаем активные политики сессии */
		SDBAbonRule soRule;
		coStream.open (
			10,
			"select "
				"rule_id,"
				"rule_name,"
				"protocol_id "
			"from "
				"ps.sessionpolicy sp "
			"where "
				"sp.session_id = :session_id /* char[255] */ "
				"and sp.protocol_id = :protocol_id /* unsigned */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soMsgInfoCache.m_psoSessInfo->m_coSessionId
			<< uiProtocol;
		while (! coStream.eof ()) {
			coStream
				>> soRule.m_soRuleId.m_uiRuleId
				>> soRule.m_coRuleName
				>> soRule.m_soRuleId.m_uiProtocol;
			p_vectActive.push_back (soRule);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}
	return iRetVal;
}

/* загружает список идентификаторов правил абонента из БД */
int load_abon_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		otl_value<otl_datetime> coValidUntil;
		otl_value<otl_datetime> coRuleTimeEnd;
		otl_null coNULL;
		SRuleId soRuleId;
		coStream.open (
			10,
			"select "
				"pc.rule_id,"
				"pc.protocol_id,"
				"sp.valid_until,"
				"case when trunc(sysdate,'dd') + pc.end_time > sysdate then trunc(sysdate,'dd') + pc.end_time else trunc(sysdate,'dd') + pc.end_time + 1 end rule_time_end "
			"from "
				"ps.subscriberpolicy sp "
				"inner join ps.policy p on sp.policy_id = p.id "
				"inner join ps.policy_content pc on p.id = pc.policy_id "
				"inner join ps.protocol pp on pc.protocol_id = pp.id "
				"inner join ps.peer peer on pp.id = peer.protocol_id "
				"left join ps.apn apn on pc.apn_id = apn.id "
				"left join (ps.location l inner join ps.sgsn_node n on n.location = l.id) on pc.location_id = l.id "
				"left join ps.device_type dt on pc.device_type_id = dt.id "
			"where "
				"sp.subscriber_id = :subscriber_id /* char[64] */  "
				"and peer.host_name = :host_name /* char[100] */ "
				"and peer.realm = :realm /* char[100] */ "
				"and (p.default_policy_flag is NULL or p.default_policy_flag = 0) "
				"and nvl(sp.blocked_until,sysdate - 1) < sysdate "
				"and nvl(sp.valid_until,sysdate + 1) > sysdate "
				"and nvl(sp.ignore_flag,0) = 0 "
				"and (pc.ip_can_type is null or pc.ip_can_type = :ip_can_type /* char[64] */ ) "
				"and (apn.access_point_name is null or apn.access_point_name = :apn_name /* char[255] */ ) "
				"and (n.ip_address is null or n.ip_address = :sgsn_node_ip_address /* char[16] */ ) "
				"and (dt.id is null or dt.id = nvl(:device_type_id /* unsigned */ ,dt.id)) "
				"and "
					"((nvl(pc.start_time,0) < nvl(pc.end_time,1) and sysdate between trunc(sysdate,'dd') + nvl(pc.start_time,0) and trunc(sysdate,'dd') + nvl(pc.end_time,1)) "
					"or (nvl(pc.start_time,0) > nvl(pc.end_time,1) and sysdate between trunc(sysdate,'dd') + nvl(pc.start_time,0) and trunc(sysdate,'dd') + nvl(pc.end_time,1) + 1))",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.v
			<< p_soMsgInfo.m_psoSessInfo->m_coIPCANType
			<< p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
			<< p_soMsgInfo.m_psoSessInfo->m_coSGSNAddress
			<< coNULL;
		while (! coStream.eof ()) {
			coStream
				>> soRuleId.m_uiRuleId
				>> soRuleId.m_uiProtocol
				>> coValidUntil
				>> coRuleTimeEnd;
			p_vectRuleList.push_back (soRuleId);
			/* если известна дата действия политик */
			if (! coValidUntil.is_null ()) {
				pcrf_server_db_insert_refqueue (p_coDBConn, coValidUntil.v, p_soMsgInfo.m_psoSessInfo->m_strSubscriberId);
			}
			/* если известна дата действия правила */
			if (! coRuleTimeEnd.is_null ()) {
				pcrf_server_db_insert_refqueue (p_coDBConn, coRuleTimeEnd.v, p_soMsgInfo.m_psoSessInfo->m_strSubscriberId);
			}
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* загрузжает список идентификаторов правил по умолчанию */
int load_def_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList)
{
	int iRetVal = 0;
	unsigned int uiProto;
	SRuleId soRuleId;

	otl_stream coStream;
	try {
		/* определяем по какому протоколу работает peer */
		coStream.open (
			1,
			"select protocol_id from ps.peer where host_name = :host_name /* char[100] */ and peer.realm = :realm /* char[100] */",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.v;
		coStream
			>> uiProto;
		coStream.close ();
		/* загружаем правила по умолчанию */
		switch (uiProto) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select "
					"r.id,"
					"1 "
				"from "
					"ps.rule r "
					"inner join ps.policy_Content pc on r.id = pc.rule_id "
					"inner join ps.policy p on pc.policy_id = p.id "
				"where "
					"p.default_policy_flag <> 0",
				p_coDBConn);
			while (! coStream.eof ()) {
				coStream
					>> soRuleId.m_uiRuleId
					>> soRuleId.m_uiProtocol;
				p_vectRuleList.push_back (soRuleId);
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select "
					"r.id, "
					"2 "
				"from "
					"ps.SCE_rule r "
					"inner join ps.policy_Content pc on r.id = pc.rule_id "
					"inner join ps.policy p on pc.policy_id = p.id "
				"where "
					"p.default_policy_flag <> 0",
					p_coDBConn);
			while (! coStream.eof ()) {
				coStream
					>> soRuleId.m_uiRuleId
					>> soRuleId.m_uiProtocol;
				p_vectRuleList.push_back (soRuleId);
			} 
			break;  /* Gx Cisco SCE */
		}
		if (coStream.good()) {
			coStream.close();
		}
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* загружает список потоков правила */
int load_rule_flows (otl_connect &p_coDBConn, SMsgDataForDB &p_soMsgInfo, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		std::string strFlowDescr;
		coStream.open (
			25,
			"select "
				"ft.flow_description "
			"from "
				"ps.rule_flows rf "
				"inner join ps.flow_template ft on rf.flow_template_id = ft.id "
			"where rf.rule_id = :rule_id<unsigned>",
			p_coDBConn);
		coStream << p_uiRuleId;
		while (! coStream.eof ()) {
			coStream >> strFlowDescr;
			p_vectRuleFlows.push_back (strFlowDescr);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* загружает описание правила */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	SRuleId &p_soRuleId,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;

	otl_stream coStream;
	try {
		SDBMonitoringInfo soMonitInfo;
		SDBAbonRule soAbonRule;
		switch (p_soRuleId.m_uiProtocol) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select "
					"r.rule_name,"
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
					"mk.dosage_total_octets,"
					"mk.dosage_output_octets,"
					"mk.dosage_input_octets,"
					"rs.redirect_address_type,"
					"rs.redirect_server_address "
				"from "
					"ps.rule r "
					"left join ps.qos_template qt on r.qos_template_id = qt.id "
					"left join ps.monitoring_key mk on r.monitoring_key_id = mk.id "
					"left join ps.redirection_server rs on r.redirection_server_id = rs.id "
				"where "
					"r.id = :rule_id<unsigned>",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			coStream
				>> soAbonRule.m_coRuleName
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
				>> soMonitInfo.m_coKeyName
				>> soMonitInfo.m_coDosageTotalOctets
				>> soMonitInfo.m_coDosageOutputOctets
				>> soMonitInfo.m_coDosageInputOctets
				>> soAbonRule.m_coRedirectAddressType
				>> soAbonRule.m_coRedirectServerAddress;
			CHECK_FCT (iFnRes = load_rule_flows (p_coDBConn, p_soMsgInfo, p_soRuleId.m_uiRuleId, soAbonRule.m_vectFlowDescr));
			if (0 == iFnRes) {
				soAbonRule.m_soRuleId = p_soRuleId;
				soAbonRule.m_vectMonitInfo.push_back (soMonitInfo);
				p_vectAbonRules.push_back (soAbonRule);
			} else {
				iRetVal = iFnRes;
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select "
					"r.name,"
					"r.PACKAGE,"
					"r.REAL_TIME_MONITOR,"
					"r.VLINK_UPSTREAM,"
					"r.VLINK_DOWNSTREAM "
				"from "
					"ps.SCE_rule r "
				"where "
					"r.id = :rule_id /* unsigned */",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			coStream
				>> soAbonRule.m_coRuleName
				>> soAbonRule.m_coSCE_PackageId
				>> soAbonRule.m_coSCE_RealTimeMonitor
				>> soAbonRule.m_coSCE_UpVirtualLink
				>> soAbonRule.m_coSCE_DownVirtualLink;
			soAbonRule.m_soRuleId = p_soRuleId;
			coStream.close ();
			coStream.open (
				10,
				"select "
					"mk.key_code,"
					"mk.dosage_total_octets,"
					"mk.dosage_output_octets,"
					"mk.dosage_input_octets "
				"from "
					"ps.sce_monitoring_key s "
					"inner join ps.monitoring_key mk on s.monitoring_key_id = mk.id "
				"where "
					"s.sce_rule_id = :rule_id /* unsigned */",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			while (! coStream.eof ()) {
				coStream
					>> soMonitInfo.m_coKeyName
					>> soMonitInfo.m_coDosageTotalOctets
					>> soMonitInfo.m_coDosageOutputOctets
					>> soMonitInfo.m_coDosageInputOctets;
				soAbonRule.m_vectMonitInfo.push_back (soMonitInfo);
			}
			p_vectAbonRules.push_back (soAbonRule);
			break; /* Gx Cisco SCE */
		}
		if (coStream.good()) {
			coStream.close();
		}
	} catch (otl_exception coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* загружает описание правил */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;
	std::vector<SRuleId>::iterator iter = p_vectRuleList.begin ();

	for (; iter != p_vectRuleList.end (); ++iter) {
		load_rule_info (p_coDBConn, p_soMsgInfo, *iter, p_vectAbonRules);
	}

	return iRetVal;
}

int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.open (
			1,
			"select "
				"subscriber_id,"
				"called_station_id,"
				"ip_can_type,"
				"sgsn_address,"
				"sysdate,"
				"origin_host,"
				"origin_realm "
			"from "
				"ps.sessionList "
			"where "
				"session_id = :session_id<char[255]>",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coSessionId;
		coStream
			>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
			>> p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
			>> p_soMsgInfo.m_psoSessInfo->m_coIPCANType
			>> p_soMsgInfo.m_psoSessInfo->m_coSGSNAddress
			>> p_soMsgInfo.m_psoSessInfo->m_coTimeLast
			>> p_soMsgInfo.m_psoSessInfo->m_coOriginHost
			>> p_soMsgInfo.m_psoSessInfo->m_coOriginRealm;
		coStream.close();
		if (! p_soMsgInfo.m_psoSessInfo->m_coIPCANType.is_null ()) {
			/* Find the enum value corresponding to the rescode string, this will give the class of error */
			struct dict_object * enum_obj = NULL;
			struct dict_enumval_request req;
			memset(&req, 0, sizeof(struct dict_enumval_request));

			/* First, get the enumerated type of the Result-Code AVP (this is fast, no need to cache the object) */
			CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictIPCANType, &(req.type_obj), ENOENT));

			/* Now search for the value given as parameter */
			req.search.enum_name = (char *) p_soMsgInfo.m_psoSessInfo->m_coIPCANType.v.c_str ();
			CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP));

			/* finally retrieve its data */
			CHECK_FCT_DO(fd_dict_getval(enum_obj, &(req.search)), return EINVAL);

			/* copy the found value, we're done */
			p_soMsgInfo.m_psoSessInfo->m_iIPCANType = req.search.enum_value.i32;
		}
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_db_abon_rule (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* очищаем список перед выполнением */
	p_vectAbonRules.clear ();

	do {
		/* список идентификаторов правил абонент */
		std::vector<SRuleId> vectRuleList;
		/* если идентификатора подписчика определен */
		if (p_soMsgInfo.m_psoSessInfo->m_strSubscriberId.length ()) {
			/* если абонент определен */
			/* загружаем правила абонента */
			load_abon_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		} else {
			/* если абонент не найден */
			/* загружаем правила по умолчанию */
			load_def_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		}
		/* если список идентификаторов правил не пустой */
		if (vectRuleList.size ()) {
			load_rule_info (p_coDBConn, p_soMsgInfo, vectRuleList, p_vectAbonRules);
		}
	} while (0);

	return iRetVal;
}

int pcrf_server_db_insert_refqueue (otl_connect &p_coDBConn, otl_datetime &p_coDateTime, std::string &p_strSubscriberId)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
		coStream.open (
			1,
			"insert into ps.refreshQueue (module, subscriber_id, refresh_date) values ('pcrf', :subscriber_id /* char[64] */, :refresh_date /* timestamp */)",
			p_coDBConn);
		coStream
			<< p_strSubscriberId
			<< p_coDateTime;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}
