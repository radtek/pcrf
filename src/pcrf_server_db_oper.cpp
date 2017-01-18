#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

/* добавление записи в список сессий */
int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* обновление записи в таблице сессий */
int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
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
	try {
		p_psoMsgToDB->m_psoSessInfo = new SSessionInfo;
		p_psoMsgToDB->m_psoReqInfo = new SRequestInfo;
	} catch (std::bad_alloc &coBadAlloc) {
		UTL_LOG_F(*g_pcoLog, "memory allocation error: '%s';", coBadAlloc.what());
		iRetVal = ENOMEM;
	}

	return iRetVal;
}

int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo, SStat *p_psoStat)
{
	/* проверка параметров */
	if (NULL != p_psoMsgInfo->m_psoSessInfo && NULL != p_psoMsgInfo->m_psoReqInfo) {
  } else {
		return EINVAL;
	}

  int iRetVal = 0;
	int iFnRes = 0;
	CTimeMeasurer coTM;

	do {

		switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
		case INITIAL_REQUEST: /* INITIAL_REQUEST */
			iFnRes = pcrf_db_insert_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (0 == iFnRes) {
      } else {
				iRetVal = iFnRes;
				break;
			}
			/* сохраняем в БД данные о локации абонента */
			iFnRes = pcrf_server_db_user_location(p_coDBConn, (*p_psoMsgInfo));
			if (0 == iFnRes) {
      } else {
				iRetVal = iFnRes;
				break;
			}
			break;
		case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
			/* закрываем открытые записи о локациях */
			{
				otl_nocommit_stream coStream;
				try {
					coStream.open(1, "update ps.sessionLocation set time_end = sysdate where time_end is null and session_id = :session_id/*char[255]*/", p_coDBConn);
					coStream
						<< p_psoMsgInfo->m_psoSessInfo->m_coSessionId;
					p_coDBConn.commit();
					if (coStream.good())
						coStream.close();
				} catch (otl_exception &coExcept) {
					UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s';", coExcept.code, coExcept.msg, coExcept.stm_text);
					if (coStream.good())
						coStream.close();
					p_coDBConn.rollback();
				}
			}
		case UPDATE_REQUEST: /* UPDATE_REQUEST */
		case EVENT_REQUEST: /* EVENT_REQUEST */
			/* выполянем запрос на обновление записи */
			iFnRes = pcrf_db_update_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
			if (0 == iFnRes) {
      } else {
				iRetVal = iFnRes;
				break;
			}
			/* для TERMINATION_REQUEST информацию о локациях не сохраняем */
			if (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType != TERMINATION_REQUEST) {
				/* сохраняем в БД данные о локации абонента */
				iFnRes = pcrf_server_db_user_location (p_coDBConn, (*p_psoMsgInfo));
				if (0 == iFnRes) {
        } else {
					iRetVal = iFnRes;
					break;
				}
			}
			/* обрабатываем информацию о выданных политиках */
			iFnRes = pcrf_server_policy_db_store (p_coDBConn, p_psoMsgInfo);
			if (0 == iFnRes) {
      } else {
				iRetVal = iFnRes;
				break;
			}
			break;
		default:
			break;
		}

		if (0 == iRetVal) {
    } else {
			break;
		}

		/* сохраняем информацию о потреблении трафика, загружаем информации об оставшихся квотах */
		iFnRes = pcrf_db_session_usage(p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *(p_psoMsgInfo->m_psoReqInfo));
		if (0 ==iFnRes) {
    } else {
			iRetVal = iFnRes;
			break;
		}
	} while (0);

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_server_policy_db_store (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoMsgInfo)
{
	/* проверка параметров */
	if (NULL != p_psoMsgInfo->m_psoSessInfo && NULL != p_psoMsgInfo->m_psoReqInfo) {
  } else {
		return EINVAL;
	}

	int iRetVal = 0;
	int iFnRes;

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
		/* сначала фиксируем информацию, полученную в запросе */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
		}
		if (0 == iRetVal) {
    } else {
			break;
		}
		/* потом закрываем оставшиеся сессии */
		iFnRes = pcrf_db_close_session_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
		if (0 == iFnRes) {
    } else {
			iRetVal = iFnRes;
			break;
		}
		break;
	case UPDATE_REQUEST: /* UPDATE_REQUEST */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
		}
		if (0 == iRetVal) {
    } else {
			break;
		}
		break;
	case EVENT_REQUEST: /* EVENT_REQUEST */
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
	otl_nocommit_stream coStream;

	try {
		/* выполняем запрос на добавление записи о сессии */
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
		/* запрос на обновление записи о сессии */
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
		/* если ни одна строка не обновлена, то это ошибка */
		if (0 < coStream.get_rpc()) {
    } else {
      UTL_LOG_E(*g_pcoLog, "no record is updated: session-id: '%s'", p_soSessInfo.m_coSessionId.v.c_str());
			iRetVal = -1;
    }
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

int pcrf_db_session_usage (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo)
{
	if (0 < p_soReqInfo.m_vectUsageInfo.size()) {
  } else {
  	/* если вектор пустой просто выходим из функции */
		return 0;
  }

	int iRetVal = 0;

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
		for (iter = p_soReqInfo.m_vectUsageInfo.begin(); iter != p_soReqInfo.m_vectUsageInfo.end(); ++iter) {
			coStream
				<< p_soSessInfo.m_strSubscriberId
				<< iter->m_coMonitoringKey
				<< iter->m_coCCInputOctets
				<< iter->m_coCCOutputOctets
				<< iter->m_coCCTotalOctets;
			UTL_LOG_D(*g_pcoLog, "quota usage:%s;%s;%'lld;%'lld;%'lld;",
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.is_null()   ? -1: iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.is_null()  ? -1: iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.is_null()   ? -1: iter->m_coCCTotalOctets.v);
			coStream
				>> iter->m_coCCInputOctets
				>> iter->m_coCCOutputOctets
				>> iter->m_coCCTotalOctets;
			UTL_LOG_D(*g_pcoLog, "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
				p_soSessInfo.m_strSubscriberId.c_str(),
				iter->m_coMonitoringKey.v.c_str(),
				iter->m_coCCInputOctets.is_null()   ? -1 : iter->m_coCCInputOctets.v,
				iter->m_coCCOutputOctets.is_null()  ? -1 : iter->m_coCCOutputOctets.v,
				iter->m_coCCTotalOctets.is_null()   ? -1 : iter->m_coCCTotalOctets.v);
			/* запоминаем полученную информацию чтобы не повторять запросы к БД по этому ключу мониторинга */
			{
				SDBMonitoringInfo soMonitInfo;
				soMonitInfo.m_coDosageInputOctets = iter->m_coCCInputOctets;
				soMonitInfo.m_coDosageOutputOctets = iter->m_coCCOutputOctets;
				soMonitInfo.m_coDosageTotalOctets = iter->m_coCCTotalOctets;
				soMonitInfo.m_bDataLoaded = true;
				p_soSessInfo.m_mapMonitInfo.insert (std::make_pair(iter->m_coMonitoringKey.v, soMonitInfo));
			}
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
				"and lower(rule_name) = lower(:rule_name /* char[255] */) "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soPoliciInfo.m_coRuleFailureCode
			<< p_soSessInfo.m_coSessionId
			<< p_soPoliciInfo.m_coChargingRuleName;
		p_coDBConn.commit ();
		UTL_LOG_D(*g_pcoLog, "session id: '%s'; '%u' rows processed; rule name: '%s'; failury code: '%s'", p_soSessInfo.m_coSessionId.v.c_str(), coStream.get_rpc(), p_soPoliciInfo.m_coChargingRuleName.v.c_str(), p_soPoliciInfo.m_coRuleFailureCode.v.c_str());
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

/* загружает идентификатор абонента (subscriber_id) из БД */
int pcrf_server_db_load_subscriber_id (otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo, SStat *p_psoStat)
{
	if (NULL != p_pcoDBConn) {
  } else {
		return EINVAL;
  }

	int iRetVal = 0;
	CTimeMeasurer coTM;

	otl_nocommit_stream coStream;
	try {
		/* выполняем запрос к БД */
		coStream.open (
			1,
/* тестирование быстродействия СУБД ----------------------------------------------------------------------------*/
			"select Subscriber_id from ps.Subscription_Data where end_user_imsi = :end_user_imsi /* char[100] */",
			//"select "
			//  "Subscriber_id "
			//"from "
			//  "ps.Subscription_Data "
			//"where "
			//  "(end_user_e164 is null or end_user_e164 = :end_user_e164 /* char[64] */) "
			//  "and (end_user_imsi is null or end_user_imsi = :end_user_imsi /* char[100] */) "
			//  "and (end_user_sip_uri is null or end_user_sip_uri = :end_user_sip_uri /* char[100] */) "
			//  "and (end_user_nai is null or end_user_nai = :end_user_nai /* char[100] */) "
			//  "and (end_user_private is null or end_user_private = :end_user_private /* char[100] */)",
			*p_pcoDBConn);
		coStream
/*			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserE164 */
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI
/*			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserSIPURI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserNAI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserPrivate*/;
/*--------------------------------------------------------------------------------------------------------------*/
		if (0 != coStream.eof()) {
			p_soMsgInfo.m_psoSessInfo->m_strSubscriberId = "";
      iRetVal = -1403;
    } else {
			coStream
				>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId;
    }
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_server_db_look4stalledsession(otl_connect *p_pcoDBConn, SSessionInfo *p_psoSessInfo, SStat *p_psoStat)
{
	if (NULL != p_pcoDBConn && NULL != p_psoSessInfo) {
  } else {
		return EINVAL;
  }

	int iRetVal = 0;
	CTimeMeasurer coTM;
	otl_nocommit_stream coStream;
	std::string strSessionId;
  SSessionInfo soSessInfo;

	try {
		/* ищем сессии по ip-адресу */
		if (!p_psoSessInfo->m_coFramedIPAddress.is_null() && p_psoSessInfo->m_uiPeerDialect == GX_3GPP) {
			coStream.open(
				10,
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
        soSessInfo.m_coSessionId = strSessionId;
        soSessInfo.m_coOriginHost = p_psoSessInfo->m_coOriginHost;
        soSessInfo.m_coOriginRealm = p_psoSessInfo->m_coOriginRealm;
        pcrf_client_RAR_W_SRCause (soSessInfo);
			}
			coStream.close();
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

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_server_db_load_active_rules (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive,
	SStat *p_psoStat)
{
  if (NULL != p_pcoDBConn) {
  } else {
    return EINVAL;
  }

	int iRetVal = 0;
	CTimeMeasurer coTM;

	otl_nocommit_stream coStream;
	try {
		/* загружаем активные политики сессии */
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
			*p_pcoDBConn);
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

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

/* загружает список идентификаторов правил абонента из БД */
int pcrf_load_abon_rule_list (
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
						":peer_dialect <unsigned,in>,"
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
			<< p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect
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
				/* если известна дата действия политик */
				if (coRefreshTime.is_null ()) {
        } else {
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

/* загружает список потоков правила */
int load_rule_flows (otl_connect &p_coDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows)
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

/* загружает описание правила */
int pcrf_db_load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::string &p_strRuleName,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		SDBMonitoringInfo soMonitInfo;
		SDBAbonRule soAbonRule;
		unsigned int uiRuleId;
		switch (p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect) {
		case GX_3GPP: /* Gx */
    case GX_PROCERA: /* Procera */
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
/*					"NULL PriorityLevel,"
					"NULL PreemptionCapability,"
					"NULL PreemptionVulnarability," */
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
/*				>> soAbonRule.m_soARP.m_coPriorityLevel
				>> soAbonRule.m_soARP.m_coPreemptionCapability
				>> soAbonRule.m_soARP.m_coPreemptionVulnerability */
				>> soAbonRule.m_coMaxRequestedBandwidthUl
				>> soAbonRule.m_coMaxRequestedBandwidthDl
				>> soAbonRule.m_coGuaranteedBitrateUl
				>> soAbonRule.m_coGuaranteedBitrateDl
				>> soAbonRule.m_coMonitKey
				>> soAbonRule.m_coRedirectAddressType
				>> soAbonRule.m_coRedirectServerAddress;
			CHECK_FCT (iRetVal = load_rule_flows (p_coDBConn, uiRuleId, soAbonRule.m_vectFlowDescr));
			if (0 == iRetVal) {
				/* запоминаем имя правила */
				soAbonRule.m_coRuleName = p_strRuleName;
				/* сохраняем в списке описание правила */
				soAbonRule.m_bIsRelevant = true;
				p_vectAbonRules.push_back(soAbonRule);
				/* если задан ключ мониторинга */
				if (! soAbonRule.m_coMonitKey.is_null()) {
					/* проверяем, нет ли уже в списке этого ключа мониторинга */
					std::map<std::string, SDBMonitoringInfo>::iterator iterMK = p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.find(soAbonRule.m_coMonitKey.v);
					/* если в списке такой ключ не найден */
					if (iterMK != p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.end()) {
          } else {
						p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.insert(std::make_pair(soAbonRule.m_coMonitKey.v, SDBMonitoringInfo()));
          }
				}
			}
			break; /* Gx */
		case GX_CISCO_SCE: /* Gx Cisco SCE */
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
			if (! coStream.eof()) {
				coStream
					>> uiRuleId
					>> soAbonRule.m_coPrecedenceLevel
					>> soAbonRule.m_coRuleName
					>> soAbonRule.m_coSCE_PackageId
					>> soAbonRule.m_coSCE_RealTimeMonitor;
				soAbonRule.m_coRuleName = p_strRuleName;
			}
			coStream.close ();
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
	} catch (otl_exception &coExcept) {
		UTL_LOG_E (*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'; var info: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text, coExcept.var_info);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_server_find_ugw_session(otl_connect &p_coDBConn, std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strUGWSessionId, SStat *p_psoStat)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;

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
				"and p.protocol_id = :dialect_id /*int*/",
			p_coDBConn);
		coStream
			<< p_strSubscriberId
			<< p_strFramedIPAddress
      << GX_3GPP;
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

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_server_find_ugw_session_byframedip (otl_connect &p_coDBConn, std::string &p_strFramedIPAddress, std::string &p_strUGWSessionId, SStat *p_psoStat)
{
  int iRetVal = 0;
  CTimeMeasurer coTM;

  otl_nocommit_stream coStream;
  try {
    coStream.open (
      1,
      "select "
        "sl.session_id "
      "from "
        "ps.sessionList sl "
        "inner join ps.peer p on sl.origin_host = p.host_name "
      "where "
        "sl.framed_ip_address = :framed_ip_address /*char[16]*/ "
        "and p.protocol_id = 1 "
      "order by sl.time_start desc",
      p_coDBConn);
    coStream
      << p_strFramedIPAddress;
    if (coStream >> p_strUGWSessionId) {
    } else {
      UTL_LOG_E (
        *g_pcoLog,
        "framed_ip_address: '%s': ugw session not found",
        p_strFramedIPAddress.c_str ());
      iRetVal = -1403;
    }
    coStream.close ();
  } catch (otl_exception &coExcept) {
    UTL_LOG_E (*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
    iRetVal = coExcept.code;
    if (coStream.good ()) {
      coStream.close ();
    }
  }

  stat_measure (p_psoStat, __FUNCTION__, &coTM);

  return iRetVal;
}

int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::string &p_strSessionId,
	SStat *p_psoStat)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;

	otl_nocommit_stream coStream;
	try {
		otl_value<std::string> coIPCANType;
		otl_value<std::string> coSGSNAddress;
		otl_value<std::string> coRATType;
		otl_value<std::string> coOriginHost;
		otl_value<std::string> coOriginReal;
		otl_value<std::string> coCGI;
		otl_value<std::string> coECGI;
		otl_value<std::string> coIMEI;

		/* загружаем данные по сессии из БД */
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
				"sloc.ecgi, "
				"sl.IMEISV,"
				"sl.end_user_imsi,"
				"sl.end_user_e164 "
			"from "
				"ps.sessionList sl "
				"left join ps.sessionLocation sloc on sl.session_id = sloc.session_id "
			"where "
				"sl.session_id = :session_id/*char[255]*/ "
				"and sloc.time_end is null",
			p_coDBConn);
		coStream
			<< p_strSessionId;
		if (! coStream.eof()) {
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
				>> coECGI
				>> coIMEI
				>> p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI
				>> p_soMsgInfo.m_psoSessInfo->m_coEndUserE164;
      /* если из БД получено значение IP-CAN-Type и соответствующего атрибута не было в запросе */
			if (! coIPCANType.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType.is_null ()) {
				/* копируем значение, полученное из БД */
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
			/* если из БД получено значение 3GPP-SGSN-Address и соответствующего атрибута не было в запросе */
			if (! coSGSNAddress.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress.is_null()) {
				/* копируем значение, полученное из БД */
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress = coSGSNAddress;
			}
			/* то же самое с RAT Type */
			if (! coRATType.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType = coRATType;
			}
			/* то же самое с Origin-Host */
			if (! coOriginHost.is_null() && p_soMsgInfo.m_psoSessInfo->m_coOriginHost.is_null()) {
				p_soMsgInfo.m_psoSessInfo->m_coOriginHost = coOriginHost;
			}
			/* то же самое с Origin-Realm */
			if (! coOriginReal.is_null() && p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.is_null()) {
				p_soMsgInfo.m_psoSessInfo->m_coOriginRealm = coOriginReal;
			}
			/* то же самое с CGI */
			if (! coCGI.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI = coCGI;
			}
			/* то же самое с ECGI */
			if (! coECGI.is_null() && p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI.is_null()) {
				p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI = coECGI;
			}
			/* то же самое с IMEI */
			if (! coIMEI.is_null() && p_soMsgInfo.m_psoSessInfo->m_coIMEI.is_null()) {
				p_soMsgInfo.m_psoSessInfo->m_coIMEI = coIMEI;
			}
		} else {
			/* no data found */
			iRetVal = -1403;
			UTL_LOG_E (*g_pcoLog, "session info: code: '%d'; message: '%s'; session_id: '%s'", -1403, "no data found", p_strSessionId.c_str());
		}
		coStream.close ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "session info: code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_server_db_user_location(
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo)
{
	/* если нечего сохранять в БД */
	if (p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_bLoaded) {
  } else {
		return 0;
  }

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
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		if (coStream.good())
			coStream.close();
		p_coDBConn.rollback();
	}

	return iRetVal;
}

int pcrf_server_db_monit_key(
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SStat *p_psoStat)
{
	int iRetVal = 0;
	CTimeMeasurer coTM;

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
		std::map<std::string, SDBMonitoringInfo>::iterator iterMonitList;
		for (iterMonitList = p_soSessInfo.m_mapMonitInfo.begin(); iterMonitList != p_soSessInfo.m_mapMonitInfo.end(); ++iterMonitList) {
			/* если данные из БД еще не загружены */
			if (! iterMonitList->second.m_bDataLoaded) {
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
					iterMonitList->second.m_coDosageInputOctets.is_null()   ? -1:  iterMonitList->second.m_coDosageInputOctets.v,
					iterMonitList->second.m_coDosageOutputOctets.is_null()  ? -1: iterMonitList->second.m_coDosageOutputOctets.v,
					iterMonitList->second.m_coDosageTotalOctets.is_null()   ? -1: iterMonitList->second.m_coDosageTotalOctets.v);
			}
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

	stat_measure (p_psoStat, __FUNCTION__, &coTM);

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

	if (p_pszAction) {
		coAction = p_pszAction;
  }
	if (p_coDateTime) {
		coRefreshDate = *p_coDateTime;
  } else {
		time_t tSecsSince1970;
		tm soTime;
		if ((time_t)-1 != time(&tSecsSince1970)) {
			if (localtime_r(&tSecsSince1970, &soTime)) {
				fill_otl_datetime(coRefreshDate.v, soTime);
				coRefreshDate.set_non_null();
			}
		}
	}

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

int pcrf_procera_db_load_sess_list (otl_connect &p_coDBConn, otl_value<std::string> &p_coUGWSessionId, std::vector<SSessionInfo> &p_vectSessList)
{
	int iRetVal = 0;
  SSessionInfo soSessInfo;
	otl_nocommit_stream coStream;

	try {
    soSessInfo.m_uiPeerDialect = 3;
		coStream.open (
			10,
      "select "
        "sl.session_id,"
        "sl.origin_host,"
        "sl.origin_realm "
      "from "
        "ps.sessionList sl,"
        "ps.sessionList sl2,"
        "ps.peer p "
      "where "
        "sl2.session_id = :session_id/*char[255]*/ "
        "and p.host_name = sl.origin_host and p.realm = sl.origin_realm "
        "and sl.framed_ip_address = sl2.framed_ip_address "
        "and p.protocol_id = 3 "
        "and sl.time_end is null",
			p_coDBConn);
		coStream
			<< p_coUGWSessionId;
    while (! coStream.eof()) {
      coStream
        >> soSessInfo.m_coSessionId
        >> soSessInfo.m_coOriginHost
        >> soSessInfo.m_coOriginRealm;
      p_vectSessList.push_back (soSessInfo);
    }
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

int pcrf_procera_db_load_location_rule (otl_connect *p_pcoDBConn, otl_value<std::string> &p_coSessionId, std::vector<SDBAbonRule> &p_vectRuleList)
{
  if (! p_coSessionId.is_null()) {
  } else {
    return EINVAL;
  }

	int iRetVal = 0;
	otl_nocommit_stream coStream;
  SDBAbonRule soRule;

	try {
    soRule.m_bIsActivated = true;
    soRule.m_bIsRelevant = false;
    soRule.m_coDynamicRuleFlag = 0;
    soRule.m_coRuleGroupFlag = 0;

		coStream.open (
			1,
      "select rule_name "
			"from ps.sessionRule "
      "where session_id = :session_id/*char[256]*/ and time_end is null and rule_name like '/User-Location/%'",
			*p_pcoDBConn);
		coStream
			<< p_coSessionId;
    while (! coStream.eof()) {
      coStream
        >> soRule.m_coRuleName;
      p_vectRuleList.push_back(soRule);
      UTL_LOG_D(*g_pcoLog, "rule name: '%s'; session-id: '%s'", soRule.m_coRuleName.v.c_str(), p_coSessionId.v.c_str());
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

int pcrf_server_db_insert_tetering_info(otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo)
{
  if (NULL != p_pcoDBConn) {
  } else {
    return EINVAL;
  }

  otl_nocommit_stream coStream;

  try {
    coStream.open(
      1,
      "insert into ps.tetheringdetection (session_id, end_user_imsi, subscriber_id, event_date, tethering_status, origin_host, origin_realm) "
      "values (:session_id/*char[255]*/, :imsi/*char[32]*/, :subscriber_id/*char[64]*/, sysdate, :tethering_status/*unsigned*/, :origin_host/*char[255]*/, :origin_realm/*char[255]*/)",
      *p_pcoDBConn);
    coStream
      << p_soMsgInfo.m_psoSessInfo->m_coSessionId
      << p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI
      << p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
      << p_soMsgInfo.m_psoReqInfo->m_coTeteringFlag
      << p_soMsgInfo.m_psoSessInfo->m_coOriginHost
      << p_soMsgInfo.m_psoSessInfo->m_coOriginRealm;
    p_pcoDBConn->commit();
  } catch (otl_exception &coExcept) {
    UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
    if (coStream.good()) {
      coStream.close();
    }
    return coExcept.code;
  }

  return 0;
}
