#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

/* ���������� ������ � ������ ������ */
int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* ���������� ������ � ������� ������ */
int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* ���������� ������ � ������� ����������� ������� */
int pcrf_db_session_usage(otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo);

/* ���������� ������ � ������� ������� ������� */
int pcrf_db_update_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SSessionPolicyInfo &p_soPoliciInfo);
/* ��������� ������ � ������� �������� ������� */
int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo);

int pcrf_server_DBstruct_init (struct SMsgDataForDB *p_psoMsgToDB)
{
	int iRetVal = 0;

	/* �������������� ��������� */
	memset (p_psoMsgToDB, 0, sizeof (*p_psoMsgToDB));

	/* �������� ������ ��� �������� ������ ������� */
	try {
		p_psoMsgToDB->m_psoSessInfo = new SSessionInfo;
		p_psoMsgToDB->m_psoReqInfo = new SRequestInfo;
	} catch (std::bad_alloc &coBadAlloc) {
		UTL_LOG_F(*g_pcoLog, "memory allocation error: '%s';", coBadAlloc.what());
		iRetVal = ENOMEM;
	}

	return iRetVal;
}

int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;
	int iFnRes = 0;

	do {
		/* �������� ���������� */
		if (NULL == p_psoMsgInfo->m_psoSessInfo
				|| NULL == p_psoMsgInfo->m_psoReqInfo) {
			iRetVal = EINVAL;
			break;
		}

		dict_avp_request_ex soAVPParam;
		char mcValue[0x10000];

		switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
		case 1: /* INITIAL_REQUEST */
			iFnRes = pcrf_db_insert_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			/* ��������� � �� ������ � ������� �������� */
			iFnRes = pcrf_server_db_user_location(p_coDBConn, (*p_psoMsgInfo));
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			break;
		case 3: /* TERMINATION_REQUEST */
			/* ��������� �������� ������ � �������� */
			{
				otl_nocommit_stream coStream;
				try {
					coStream.open(1, "update ps.sessionLocation set time_end = sysdate where time_end is null and session_id = :session_id/*char[255]*/", p_coDBConn);
					coStream
						<< p_psoMsgInfo->m_psoSessInfo->m_coSessionId;
					p_coDBConn.commit();
					if (coStream.good())
						coStream.close();
				} catch (otl_exception coExcept) {
					UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s';", coExcept.code, coExcept.msg, coExcept.stm_text);
					if (coStream.good())
						coStream.close();
					p_coDBConn.rollback();
				}
			}
		case 2: /* UPDATE_REQUEST */
		case 4: /* EVENT_REQUEST */
			/* ��� TERMINATION_REQUEST ���������� � �������� �� ��������� */
			if (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType != 3) {
				/* ��������� � �� ������ � ������� �������� */
				iFnRes = pcrf_server_db_user_location(p_coDBConn, (*p_psoMsgInfo));
				if (iFnRes) {
					iRetVal = iFnRes;
					break;
				}
			}
			/* ��������� ������ �� ���������� ������ */
			iFnRes = pcrf_db_update_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (iFnRes) {
				iRetVal = iFnRes;
				break;
			}
			/* ������������ ���������� � �������� ��������� */
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

		/* ��������� ���������� � ����������� �������, ��������� ���������� �� ���������� ������ */
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

	/* �������� ���������� */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
			|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case 3: /* TERMINATION_REQUEST */
		/* ������� ��������� ����������, ���������� � ������� */
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
		/* ����� ��������� ���������� ������ */
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
	/* ����������� ������� ������ */
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

	otl_nocommit_stream coStream;
	try {
		/* ��������� ������ �� ���������� ������ � ������ */
		coStream.open (
			1,
			"insert into ps.sessionList (session_id,subscriber_id,origin_host,origin_realm,end_user_imsi,end_user_e164,imeisv,time_start,time_last_req,framed_ip_address,called_station_id)"
			"values(:session_id/*char[64]*/,:subscriber_id/*char[64]*/,:origin_host/*char[255]*/,:origin_realm/*char[255]*/,:end_user_imsi/*char[32]*/,:end_user_e164/*char[16]*/,:imeisv/*char[20]*/,sysdate,sysdate,:framed_ip_address/*char[16]*/,:called_station_id/*char[255]*/)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_strSubscriberId
			<< p_soSessInfo.m_coOriginHost
			<< p_soSessInfo.m_coOriginRealm
			<< p_soSessInfo.m_coEndUserIMSI
			<< p_soSessInfo.m_coEndUserE164
			<< p_soSessInfo.m_coIMEI
			<< p_soSessInfo.m_coFramedIPAddress
			<< p_soSessInfo.m_coCalledStationId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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

	otl_nocommit_stream coStream;
	try {
		/* ������ �� ���������� ������ � ������ */
		coStream.open (
			1,
			"update ps.SessionList"
			" set time_end = :1<timestamp>, time_last_req = sysdate, termination_cause = :3<char[64]>"
			" where session_id = :4<char[255]>",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeEnd
			<< p_soSessInfo.m_coTermCause
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good()) {
			coStream.close();
		}
	}
}

int pcrf_db_session_usage(otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	/* ���� ������ ������ ������ ������� �� ������� */
	if (0 == p_soReqInfo.m_vectUsageInfo.size())
		return 0;

	otl_nocommit_stream coStream;
	try {
		std::vector<SSessionUsageInfo>::iterator iter;
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
			UTL_LOG_D(*g_pcoLog, "quota usage:%s;%s;%'lld;%'lld;%'lld;",
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.is_null() ? -1: iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.is_null() ? -1: iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.is_null() ? -1: iter->m_coCCTotalOctets.v);
			coStream
				>> iter->m_coCCInputOctets
				>> iter->m_coCCOutputOctets
				>> iter->m_coCCTotalOctets;
			UTL_LOG_D(*g_pcoLog, "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.is_null() ? -1: iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.is_null() ? -1: iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.is_null() ? -1: iter->m_coCCTotalOctets.v);
			/* ���������� ���������� ���������� ����� �� ��������� ������� � �� �� ����� ����� ����������� */
			{
				SDBMonitoringInfo soMonitInfo;
				if (!iter->m_coCCInputOctets.is_null())
					soMonitInfo.m_coDosageInputOctets = iter->m_coCCInputOctets.v;
				if (!iter->m_coCCOutputOctets.is_null())
					soMonitInfo.m_coDosageOutputOctets = iter->m_coCCOutputOctets.v;
				if (!iter->m_coCCTotalOctets.is_null())
					soMonitInfo.m_coDosageTotalOctets = iter->m_coCCTotalOctets.v;
				soMonitInfo.m_bDataLoaded = true;
				p_soSessInfo.m_mapMonitInfo.insert(std::make_pair(iter->m_coMonitoringKey.v, soMonitInfo));
			}
			++iter;
		}
		p_coDBConn.commit();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"insert into ps.sessionRule "
			"(session_id,time_start,rule_name) "
			"values (:session_id /*char[255]*/, sysdate, :rule_name /*char[255]*/)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soRule.m_coRuleName;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"update ps.sessionRule "
				"set time_end = sysdate, "
				"rule_failure_code = :rule_failure_code /* char[64]*/ "
			"where "
				"session_id = :session_id /* char[255]*/ "
				"and rule_name = :rule_name /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soPoliciInfo.m_coRuleFailureCode
			<< p_soSessInfo.m_coSessionId
			<< p_soPoliciInfo.m_coChargingRuleName;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"update "
				"ps.sessionRule "
			"set "
				"time_end = sysdate "
			"where "
				"session_id = :session_id /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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
	std::string &p_strRuleName)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"update "
				"ps.sessionRule "
			"set "
				"time_end = sysdate "
			"where "
				"session_id = :session_id /*char[255]*/ "
				"and rule_name = :rule_name /*char[100]*/ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_strRuleName;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		if (coStream.good())
			coStream.close();
	}

	return iRetVal;
}

/* ��������� ������������� �������� (subscriber_id) �� �� */
int pcrf_server_db_load_abon_id (otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo)
{
	if (NULL == p_pcoDBConn)
		return EINVAL;

	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		/* ��������� ������ � �� */
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
			*p_pcoDBConn);
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
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_db_look4stalledsession(otl_connect *p_pcoDBConn, SSessionInfo *p_psoSessInfo)
{
	if (NULL == p_pcoDBConn || NULL == p_psoSessInfo)
		return EINVAL;

	int iRetVal = 0;
	otl_nocommit_stream coStream;
	std::string strSessionId;
	std::map<std::string,int> mapSessList;

	try {
		/* ���� ������ �� IMSI */
		//if (!p_psoSessInfo->m_coEndUserIMSI.is_null()) {
		//	coStream.open(
		//		100,
		//		"select "
		//			"session_id "
		//		"from "
		//			"ps.sessionList ps "
		//		"where "
		//			"ps.end_user_imsi = :end_user_imsi/*char[16]*/ "
		//			"and ps.origin_host = :origin_host/*char[255]*/ "
		//			"and ps.session_id <> :session_id/*char[255]*/ "
		//			"and ps.time_end is null",
		//		*p_pcoDBConn);
		//	coStream
		//		<< p_psoSessInfo->m_coEndUserIMSI
		//		<< p_psoSessInfo->m_coOriginHost
		//		<< p_psoSessInfo->m_coSessionId;
		//	while (!coStream.eof()) {
		//		coStream
		//			>> strSessionId;
		//		UTL_LOG_N(*g_pcoLog, "it found potentially stalled session: session_id: '%s'; end_user_imsi: '%s'", strSessionId.c_str(), p_psoSessInfo->m_coEndUserIMSI.v.c_str());
		//		mapSessList.insert(std::make_pair(strSessionId,0));
		//	}
		//	if (coStream.good())
		//		coStream.close();
		//}
		/* ���� ������ �� ip-������ */
		if (!p_psoSessInfo->m_coFramedIPAddress.is_null()) {
			coStream.open(
				100,
				"select "
					"session_id "
				"from "
					"ps.sessionList ps "
				"where "
					"ps.framed_ip_address = :framed_ip_address/*char[16]*/ "
					"and ps.origin_host = :origin_host/*char[255]*/ "
					"and ps.session_id <> :session_id/*char[255]*/ "
					"and ps.time_end is null",
				*p_pcoDBConn);
			coStream
				<< p_psoSessInfo->m_coFramedIPAddress
				<< p_psoSessInfo->m_coOriginHost
				<< p_psoSessInfo->m_coSessionId;
			while (!coStream.eof()) {
				coStream
					>> strSessionId;
				UTL_LOG_D(*g_pcoLog, "it found potentially stalled session: session_id: '%s'; framed_ip_address: '%s'", strSessionId.c_str(), p_psoSessInfo->m_coFramedIPAddress.v.c_str());
				mapSessList.insert(std::make_pair(strSessionId, 0));
			}
			if (coStream.good())
				coStream.close();
		}
		/* ���������� ������ � ������� ������ ��� �������� */
		for (std::map<std::string,int>::iterator iterList = mapSessList.begin(); iterList != mapSessList.end(); ++iterList) {
			pcrf_server_db_insert_refqueue(*p_pcoDBConn, "session_id", iterList->first, NULL, "abort_session");
		}
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.get_error_state())
			coStream.clean();
		if (coStream.good())
			coStream.close();
		p_pcoDBConn->rollback();
	}

	return iRetVal;
}

int pcrf_server_db_load_active_rules (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		/* ��������� �������� �������� ������ */
		SDBAbonRule soRule;
		coStream.open (
			10,
			"select "
				"rule_name "
			"from "
				"ps.sessionRule sp "
			"where "
				"sp.session_id = :session_id /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soMsgInfoCache.m_psoSessInfo->m_coSessionId;
		while (! coStream.eof ()) {
			coStream
				>> soRule.m_coRuleName;
			soRule.m_bIsActivated = true;
			p_vectActive.push_back (soRule);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}
	return iRetVal;
}

/* ��������� ������ ��������������� ������ �������� �� �� */
int load_abon_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<std::string> &p_vectRuleList)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	otl_refcur_stream coRefCur;
	otl_value<otl_datetime> coRefreshTime;
	std::string strRuleName;
	try {
		coStream.open (
			1,
			"begin "
				":cur<refcur,out[32]> := ps.GetSubRules("
						":subscriber_id <char[64],in>,"
						":peer_proto <unsigned,in>,"
						":ip_can_type <char[20],in>,"
						":rat_type <char[20],in>,"
						":apn_name <char[255],in>,"
						":sgsn_node_ip_address <char[16],in>,"
						":IMEI <char[20],in>"
					");"
			"end;",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
			<< p_soMsgInfo.m_psoSessInfo->m_uiPeerProto
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType
			<< p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress
			<< p_soMsgInfo.m_psoSessInfo->m_coIMEI;
		while (! coStream.eof ()) {
			coStream
				>> coRefCur;
			while(!coRefCur.eof()) {
				coRefCur
					>> strRuleName
					>> coRefreshTime;
				p_vectRuleList.push_back (strRuleName);
				/* ���� �������� ���� �������� ������� */
				if (! coRefreshTime.is_null ()) {
					pcrf_server_db_insert_refqueue(p_coDBConn, "subscriber_id", p_soMsgInfo.m_psoSessInfo->m_strSubscriberId, &(coRefreshTime.v), NULL);
				}
			}
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* ���������� ������ ��������������� ������ �� ��������� */
int load_def_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<std::string> &p_vectRuleList)
{
	int iRetVal = 0;
	unsigned int uiProto;
	unsigned int uiRulePrecedence;
	otl_value<std::string> coRuleName;

	otl_nocommit_stream coStream;
	try {
		/* ��������� ������� �� ��������� */
		switch (p_soMsgInfo.m_psoSessInfo->m_uiPeerProto) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select "
					"r.rule_name "
				"from "
					"ps.rule r "
					"inner join ps.policy_Content pc on r.id = pc.rule_id "
					"inner join ps.policy p on pc.policy_id = p.id "
				"where "
					"p.default_policy_flag <> 0",
				p_coDBConn);
			while (! coStream.eof ()) {
				coStream
					>> coRuleName;
				if (!coRuleName.is_null())
					p_vectRuleList.push_back (coRuleName.v);
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select "
					"r.name "
				"from "
					"ps.SCE_rule r "
					"inner join ps.policy_Content pc on r.id = pc.rule_id "
					"inner join ps.policy p on pc.policy_id = p.id "
				"where "
					"p.default_policy_flag <> 0"
				"order by "
					"precedence_level",
					p_coDBConn);
			/* �������� �� �� ������ ����, ����� ������, ������. ������� ����������� �� precedence_level */
			if (! coStream.eof ()) {
				coStream
					>> coRuleName;
				if (!coRuleName.is_null())
					p_vectRuleList.push_back (coRuleName.v);
			} else {
				iRetVal = -1;
			}
			break;  /* Gx Cisco SCE */
		}
		if (coStream.good()) {
			coStream.close();
		}
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* ��������� ������ ������� ������� */
int load_rule_flows (otl_connect &p_coDBConn, SMsgDataForDB &p_soMsgInfo, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		std::string strFlowDescr;
		coStream.open (
			25,
			"select "
				"ft.flow_description "
			"from "
				"ps.rule_flows rf "
				"inner join ps.flow_template ft on rf.flow_template_id = ft.id "
			"where rf.rule_id = :rule_id/*unsigned*/",
			p_coDBConn);
		coStream
			<< p_uiRuleId;
		while (! coStream.eof ()) {
			coStream >> strFlowDescr;
			p_vectRuleFlows.push_back (strFlowDescr);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* ��������� �������� ������� */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::string &p_strRuleName,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;

	otl_nocommit_stream coStream;
	try {
		SDBMonitoringInfo soMonitInfo;
		SDBAbonRule soAbonRule;
		unsigned int uiRuleId;
		switch (p_soMsgInfo.m_psoSessInfo->m_uiPeerProto) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select "
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
					"left join ps.redirection_server rs on r.redirection_server_id = rs.id "
				"where "
					"r.rule_name = :rule_name/*char[100]*/",
				p_coDBConn);
			coStream
				<< p_strRuleName;
			coStream
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
				>> soAbonRule.m_coMonitKey
				>> soAbonRule.m_coRedirectAddressType
				>> soAbonRule.m_coRedirectServerAddress;
			CHECK_FCT (iFnRes = load_rule_flows (p_coDBConn, p_soMsgInfo, uiRuleId, soAbonRule.m_vectFlowDescr));
			if (0 == iFnRes) {
				/* ���������� ��� ������� */
				soAbonRule.m_coRuleName = p_strRuleName;
				/* ��������� � ������ �������� ������� */
				soAbonRule.m_bIsRelevant = true;
				p_vectAbonRules.push_back(soAbonRule);
				/* ���� ����� ���� ����������� */
				if (!soAbonRule.m_coMonitKey.is_null()) {
					/* ���������, ��� �� ��� � ������ ����� ����� ����������� */
					std::map<std::string, SDBMonitoringInfo>::iterator iterMK = p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.find(soAbonRule.m_coMonitKey.v);
					/* ���� � ������ ����� ���� �� ������ */
					if (iterMK == p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.end())
						p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.insert(std::make_pair(soAbonRule.m_coMonitKey.v, SDBMonitoringInfo()));
				}
			} else {
				iRetVal = iFnRes;
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select "
					"r.id,"
					"r.precedence_level,"
					"r.name,"
					"r.PACKAGE,"
					"r.REAL_TIME_MONITOR "
				"from "
					"ps.SCE_rule r "
				"where "
					"r.name = :rule_name /*char[100]*/",
				p_coDBConn);
			coStream
				<< p_strRuleName;
			coStream
				>> uiRuleId
				>> soAbonRule.m_coPrecedenceLevel
				>> soAbonRule.m_coRuleName
				>> soAbonRule.m_coSCE_PackageId
				>> soAbonRule.m_coSCE_RealTimeMonitor;
			soAbonRule.m_coRuleName = p_strRuleName;
			coStream.close ();
			/* TODO */
			/* ������ ��� �������� ����� ������� */
			if (!p_soMsgInfo.m_psoSessInfo->m_coCalledStationId.is_null () && 0 == p_soMsgInfo.m_psoSessInfo->m_coCalledStationId.v.compare("test.lte.ru")) {
				if (!p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI.is_null ()) {
					coStream.open(1, "select VLINKID from ps.SCE_VLINK where CGI = :location_id/*char[20]*/", p_coDBConn);
					coStream
						<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI;
				} else if (!p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI.is_null ()) {
					coStream.open (1, "select VLINKID from ps.SCE_VLINK where ECGI = :location_id/*char[20]*/", p_coDBConn);
					coStream
						<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI;
				} else
					goto continue_whithout_vlink;
				if(!coStream.eof()) {
					coStream
						>> soAbonRule.m_coSCE_UpVirtualLink;
				} else {
					soAbonRule.m_coSCE_UpVirtualLink.v = 0;
					soAbonRule.m_coSCE_UpVirtualLink.set_non_null();
				}
				soAbonRule.m_coSCE_DownVirtualLink = soAbonRule.m_coSCE_UpVirtualLink;
				coStream.close();
			}
			continue_whithout_vlink:
			/* TODO */
			coStream.open (
				10,
				"select "
					"mk.key_code "
				"from "
					"ps.sce_monitoring_key s "
					"inner join ps.monitoring_key mk on s.monitoring_key_id = mk.id "
				"where "
					"s.sce_rule_id = :rule_id /*unsigned*/",
				p_coDBConn);
			coStream
				<< uiRuleId;
			std::string strMonitKey;
			while (!coStream.eof()) {
				coStream
					>> strMonitKey;
				p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.insert(std::make_pair(strMonitKey, SDBMonitoringInfo()));
			}
			soAbonRule.m_bIsRelevant = true;
			p_vectAbonRules.push_back (soAbonRule);
			break; /* Gx Cisco SCE */
		}
		if (coStream.good()) {
			coStream.close();
		}
	} catch (otl_exception coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

/* ��������� �������� ������ */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<std::string> &p_vectRuleList,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;
	std::vector<std::string>::iterator iter = p_vectRuleList.begin ();

	for (; iter != p_vectRuleList.end (); ++iter) {
		load_rule_info (p_coDBConn, p_soMsgInfo, *iter, p_vectAbonRules);
	}

	return iRetVal;
}

int pcrf_server_find_ugw_session(otl_connect &p_coDBConn, std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strUGWSessionId)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"select "
				"session_id "
			"from "
				"ps.sessionList sl "
				"inner join ps.peer p on sl.origin_host = p.host_name "
			"where "
				"subscriber_id = :subscriber_id /*char[64]*/ "
				"and framed_ip_address = :framed_ip_address /*char[16]*/ "
				"and p.protocol_id = 1",
			p_coDBConn);
		coStream
			<< p_strSubscriberId
			<< p_strFramedIPAddress;
		if (!coStream.eof ()) {
			coStream
				>> p_strUGWSessionId;
		} else {
			UTL_LOG_E (
				*g_pcoLog,
				"subscriber_id: '%s'; framed_ip_address: '%s': ugw session not found",
				p_strSubscriberId.c_str (),
				p_strFramedIPAddress.c_str ());
			iRetVal = -1403;
		}
		coStream.close ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::string &p_strSessionId)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		otl_value<std::string> coIPCANType;
		otl_value<std::string> coSGSNAddress;
		otl_value<std::string> coRATType;
		otl_value<std::string> coOriginHost;
		otl_value<std::string> coOriginReal;
		otl_value<std::string> coCGI;
		otl_value<std::string> coECGI;

		/* ��������� ������ �� ������ �� �� */
		coStream.open (
			1,
			"select "
				"sl.subscriber_id,"
				"sl.framed_ip_address,"
				"sl.called_station_id,"
				"sloc.ip_can_type,"
				"sloc.sgsn_ip_address,"
				"sloc.rat_type,"
				"sl.origin_host,"
				"sl.origin_realm,"
				"sloc.cgi,"
				"sloc.ecgi "
			"from "
				"ps.sessionList sl "
				"left join ps.sessionLocation sloc on sl.session_id = sloc.session_id "
			"where "
				"sl.session_id = :session_id/*char[255]*/ "
				"and sloc.time_end is null",
			p_coDBConn);
		coStream
			<< p_strSessionId;
		if (!coStream.eof()) {
			coStream
				>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
				>> p_soMsgInfo.m_psoSessInfo->m_coFramedIPAddress
				>> p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
				>> coIPCANType
				>> coSGSNAddress
				>> coRATType
				>> coOriginHost
				>> coOriginReal
				>> coCGI
				>> coECGI;
			/* ���� �� �� �������� �������� IP-CAN-Type � ���������������� �������� �� ���� � ������� */
			if (!coIPCANType.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType.is_null ()) {
				/* �������� ��������, ���������� �� �� */
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType = coIPCANType;
				/* Find the enum value corresponding to the rescode string, this will give the class of error */
				struct dict_object * enum_obj = NULL;
				struct dict_enumval_request req;
				memset(&req, 0, sizeof(struct dict_enumval_request));

				/* First, get the enumerated type of the Result-Code AVP (this is fast, no need to cache the object) */
				CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictIPCANType, &(req.type_obj), ENOENT));

				/* Now search for the value given as parameter */
				req.search.enum_name = (char *) coIPCANType.v.c_str ();
				CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP));

				/* finally retrieve its data */
				CHECK_FCT_DO(fd_dict_getval(enum_obj, &(req.search)), return EINVAL);

				/* copy the found value, we're done */
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType = req.search.enum_value.i32;
			}
			/* ���� �� �� �������� �������� 3GPP-SGSN-Address � ���������������� �������� �� ���� � ������� */
			if (!coSGSNAddress.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress.is_null()) {
				/* �������� ��������, ���������� �� �� */
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress = coSGSNAddress;
			}
			/* �� �� ����� � RAT Type */
			if (!coRATType.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType = coRATType;
			}
			/* �� �� ����� � Origin-Host */
			if (!coOriginHost.is_null() && p_soMsgInfo.m_psoSessInfo->m_coOriginHost.is_null()) {
				p_soMsgInfo.m_psoSessInfo->m_coOriginHost = coOriginHost;
			}
			/* �� �� ����� � Origin-Realm */
			if (!coOriginReal.is_null() && p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.is_null()) {
				p_soMsgInfo.m_psoSessInfo->m_coOriginRealm = coOriginReal;
			}
			/* �� �� ����� � CGI */
			if (!coCGI.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI = coCGI;
			}
			/* �� �� ����� � ECGI */
			if (!coECGI.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI = coECGI;
			}
		} else {
			/* no data found */
			iRetVal = -1403;
			UTL_LOG_E (*g_pcoLog, "code: '%d'; message: '%s'; session_id: '%s'", -1403, "no data found", p_strSessionId.c_str());
		}
		coStream.close ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_db_user_location(
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo)
{
	/* ���� ������ ��������� � �� */
	if (!p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_bLoaded)
		return 0;

	int iRetVal = 0;

	otl_nocommit_stream coStream;

	try {
		coStream.open(1, "update ps.sessionLocation set time_end = sysdate where time_end is null and session_id = :session_id /*char[255]*/", p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coSessionId;
		p_coDBConn.commit();
		if (coStream.good())
			coStream.close();
		coStream.open(
			1,
			"insert into ps.sessionLocation "
				"(session_id, time_start, time_end, sgsn_mcc_mnc, sgsn_ip_address, sgsn_ipv6_address, rat_type, ip_can_type, cgi, ecgi, tai, rai) "
				"values "
				"(:session_id/*char[255]*/, sysdate, null, :sgsn_mcc_mnc/*char[10]*/, :sgsn_ip_address/*char[15]*/, :sgsn_ipv6_address/*char[50]*/, :rat_type/*char[50]*/, :ip_can_type/*char[20]*/, :cgi/*char[20]*/, :ecgi/*char[20]*/, :tai/*char[20]*/, :rai/*char[20]*/)",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coSessionId
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNIPv6Address
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coTAI
			<< p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRAI;
		p_coDBConn.commit();
		if (coStream.good())
			coStream.close();
	} catch (otl_exception coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		if (coStream.good())
			coStream.close();
		p_coDBConn.rollback();
	}

	return iRetVal;
}

int pcrf_server_db_abon_rule (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* ������� ������ ����� ����������� */
	p_vectAbonRules.clear ();

	do {
		/* ������ ��������������� ������ ������� */
		std::vector<std::string> vectRuleList;
		/* ���� �������������� ���������� ��������� */
		if (p_soMsgInfo.m_psoSessInfo->m_strSubscriberId.length ()) {
			/* ���� ������� ��������� */
			/* ��������� ������� �������� */
			load_abon_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		}
		if (0 == vectRuleList.size()) {
			/* ���� � �������� ��� ������� ������� */
			/* ��������� ������� �� ��������� */
			load_def_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		}
		/* ���� ������ ��������������� ������ �� ������ */
		if (vectRuleList.size ()) {
			load_rule_info (p_coDBConn, p_soMsgInfo, vectRuleList, p_vectAbonRules);
			/* � ������ � SCE ��� ���� �������� ���� ������� � ��������� ����������� */
			if (p_vectAbonRules.size() && 2 == p_soMsgInfo.m_psoSessInfo->m_uiPeerProto) {
				SDBAbonRule soAbonRule;
				std::vector<SDBAbonRule>::iterator iterList = p_vectAbonRules.begin();
				if (iterList != p_vectAbonRules.end()) {
					soAbonRule = *iterList;
					++iterList;
				}
				while (iterList != p_vectAbonRules.end()) {
					if (soAbonRule.m_coPrecedenceLevel.v > iterList->m_coPrecedenceLevel.v)
						soAbonRule = *iterList;
					++iterList;
				}
				p_vectAbonRules.clear();
				p_vectAbonRules.push_back(soAbonRule);
			}
		}
	} while (0);

	return iRetVal;
}

int pcrf_server_db_monit_key(
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;

	try {
		coStream.open(
			1,
			"begin ps.qm.ProcessQuota("
				":SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
				"null, null, null,"
				":GrantedInputOctets /*ubigint,out*/, :GrantedOutputOctets /*ubigint,out*/, :GrantedTotalOctets /*ubigint,out*/);"
			"end;",
			p_coDBConn);
		std::map<std::string, SDBMonitoringInfo>::iterator iterMonitList = p_soSessInfo.m_mapMonitInfo.begin();
		while (iterMonitList != p_soSessInfo.m_mapMonitInfo.end()) {
			/* ���� ������ �� �� ��� �� ��������� */
			if (!iterMonitList->second.m_bDataLoaded) {
				coStream
					<< p_soSessInfo.m_strSubscriberId
					<< iterMonitList->first;
				coStream
					>> iterMonitList->second.m_coDosageInputOctets
					>> iterMonitList->second.m_coDosageOutputOctets
					>> iterMonitList->second.m_coDosageTotalOctets;
				UTL_LOG_D(*g_pcoLog, "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
					p_soSessInfo.m_strSubscriberId.c_str(),
					iterMonitList->first.c_str(),
					iterMonitList->second.m_coDosageInputOctets.is_null() ? -1:  iterMonitList->second.m_coDosageInputOctets.v,
					iterMonitList->second.m_coDosageOutputOctets.is_null() ? -1: iterMonitList->second.m_coDosageOutputOctets.v,
					iterMonitList->second.m_coDosageTotalOctets.is_null() ? -1: iterMonitList->second.m_coDosageTotalOctets.v);
			}
			++iterMonitList;
		}
		p_coDBConn.commit();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
		p_coDBConn.rollback();
	}

	return iRetVal;
}

int pcrf_server_db_insert_refqueue (
	otl_connect &p_coDBConn,
	const char *p_pszIdentifierType,
	const std::string &p_strIdentifier,
	otl_datetime *p_coDateTime,
	const char *p_pszAction)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	otl_value<std::string> coAction;
	otl_value<otl_datetime> coRefreshDate;

	if (p_pszAction)
		coAction = p_pszAction;
	if (p_coDateTime)
		coRefreshDate = *p_coDateTime;
	try {
		coStream.open (
			1,
			"insert into ps.refreshQueue "
			"(identifier_type, identifier, module, refresh_date, action) "
			"values "
			"(:identifier_type /*char[64]*/, :identifier/*char[64]*/, 'pcrf', nvl(:refresh_date/*timestamp*/,sysdate), :action/*char[20]*/)",
			p_coDBConn);
		coStream
			<< p_pszIdentifierType
			<< p_strIdentifier
			<< coRefreshDate
			<< coAction;
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}
