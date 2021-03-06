#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;
extern SStat *g_psoDBStat;

/* добавление записи в список сессий */
void pcrf_db_insert_session (SSessionInfo &p_soSessInfo);

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
		UTL_LOG_F(*g_pcoLog, "memory allocation error: '%s'", coBadAlloc.what());
		iRetVal = ENOMEM;
	}

	return iRetVal;
}

int pcrf_server_req_db_store( struct SMsgDataForDB *p_psoMsgInfo )
{
  /* проверка параметров */
  if ( NULL != p_psoMsgInfo->m_psoSessInfo && NULL != p_psoMsgInfo->m_psoReqInfo ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;

  do {
    switch ( p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType ) {
      case INITIAL_REQUEST: /* INITIAL_REQUEST */
        pcrf_db_insert_session( *( p_psoMsgInfo->m_psoSessInfo ) );
        /* сохраняем в БД данные о локации абонента */
        pcrf_server_db_user_location( ( *p_psoMsgInfo ) );
        break;
      case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
        /* закрываем открытые записи о локациях */
        pcrf_server_db_close_user_loc( p_psoMsgInfo->m_psoSessInfo->m_strSessionId );
      case UPDATE_REQUEST: /* UPDATE_REQUEST */
      case EVENT_REQUEST: /* EVENT_REQUEST */
        /* выполянем запрос на обновление записи */
      {
        otl_value<otl_datetime> coTimeLast;

        pcrf_fill_otl_datetime( coTimeLast, NULL );
        pcrf_db_update_session( p_psoMsgInfo->m_psoSessInfo->m_strSessionId, p_psoMsgInfo->m_psoSessInfo->m_coTimeEnd, coTimeLast, p_psoMsgInfo->m_psoSessInfo->m_coTermCause );
        /* для TERMINATION_REQUEST информацию о локациях не сохраняем */
        if ( p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType != TERMINATION_REQUEST ) {
          /* сохраняем в БД данные о локации абонента */
          pcrf_server_db_user_location( ( *p_psoMsgInfo ) );
        }
        /* обрабатываем информацию о выданных политиках */
        pcrf_server_policy_db_store( p_psoMsgInfo );
      }
      break;
      default:
        break;
    }
  } while ( 0 );

  return iRetVal;
}

void pcrf_server_policy_db_store( SMsgDataForDB *p_psoMsgInfo )
{
	/* проверка параметров */
	if (NULL != p_psoMsgInfo->m_psoSessInfo && NULL != p_psoMsgInfo->m_psoReqInfo) {
  } else {
		return;
	}

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case TERMINATION_REQUEST: /* TERMINATION_REQUEST */
		/* сначала фиксируем информацию, полученную в запросе */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoReqInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoReqInfo->m_vectCRR.end (); ++ iter) {
      pcrf_db_close_session_rule( p_psoMsgInfo->m_psoSessInfo, iter->m_coChargingRuleName.v, &( iter->m_coRuleFailureCodeEnum.v ) );
    }
		/* потом закрываем оставшиеся правила */
    pcrf_db_close_session_rule_all( p_psoMsgInfo->m_psoSessInfo->m_strSessionId );
		break;
	case UPDATE_REQUEST: /* UPDATE_REQUEST */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoReqInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoReqInfo->m_vectCRR.end (); ++ iter) {
      pcrf_db_close_session_rule( p_psoMsgInfo->m_psoSessInfo, iter->m_coChargingRuleName.v, &( iter->m_coRuleFailureCodeEnum.v ) );
    }
		break;
	case EVENT_REQUEST: /* EVENT_REQUEST */
		break;
	default:
		break;
	}
}

void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo)
{
	/* освобождаем занятую память */
	if (p_psoMsgInfo->m_psoSessInfo) {
		delete p_psoMsgInfo->m_psoSessInfo;
		p_psoMsgInfo->m_psoSessInfo = NULL;
	}
	if (p_psoMsgInfo->m_psoReqInfo) {
		delete p_psoMsgInfo->m_psoReqInfo;
		p_psoMsgInfo->m_psoReqInfo = NULL;
	}
}

void pcrf_fill_otl_datetime( otl_value<otl_datetime> &p_coOtlDateTime, tm *p_psoTime )
{
  tm *psoTime, soTime;

  if ( NULL == p_psoTime ) {
    time_t tSecsSince1970;
    if ( (time_t)-1 != time( &tSecsSince1970 ) ) {
      if ( localtime_r( &tSecsSince1970, &soTime ) ) {
      }
    } else {
      return;
    }
    psoTime = &soTime;
  } else {
    psoTime = p_psoTime;
  }

  p_coOtlDateTime.v.year =   psoTime->tm_year + 1900;
  p_coOtlDateTime.v.month =  psoTime->tm_mon + 1;
  p_coOtlDateTime.v.day =    psoTime->tm_mday;
  p_coOtlDateTime.v.hour =   psoTime->tm_hour;
  p_coOtlDateTime.v.minute = psoTime->tm_min;
  p_coOtlDateTime.v.second = psoTime->tm_sec;

  p_coOtlDateTime.set_non_null();
}

void pcrf_db_insert_session (SSessionInfo &p_soSessInfo)
{
  std::list<SSQLQueueParam*> *plistParameters = new std::list<SSQLQueueParam*>;
  otl_value<std::string> coSubscriberId( p_soSessInfo.m_strSubscriberId );
  otl_value<otl_datetime> coTime;

  pcrf_fill_otl_datetime( coTime, NULL );

  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_strSessionId );
  pcrf_sql_queue_add_param( plistParameters, coSubscriberId );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coOriginHost );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coOriginRealm );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_soSubscriptionData.m_coEndUserIMSI );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_soSubscriptionData.m_coEndUserE164 );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coIMEI );
  pcrf_sql_queue_add_param( plistParameters, coTime );
  pcrf_sql_queue_add_param( plistParameters, coTime );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coFramedIPAddress );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coCalledStationId );

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionList (session_id,subscriber_id,origin_host,origin_realm,end_user_imsi,end_user_e164,imeisv,time_start,time_last_req,framed_ip_address,called_station_id)"
    "values(:session_id/*char[255]*/,:subscriber_id/*char[64]*/,:origin_host/*char[255]*/,:origin_realm/*char[255]*/,:end_user_imsi/*char[32]*/,:end_user_e164/*char[16]*/,:imeisv/*char[20]*/,:start_time/*timestamp*/,:time_last/*timestamp*/,:framed_ip_address/*char[16]*/,:called_station_id/*char[255]*/)",
    plistParameters,
    "insert session",
    &( p_soSessInfo.m_strSessionId ) );
}

void pcrf_db_update_session (
  std::string &p_strSessionId,
  otl_value<otl_datetime> &p_coTimeEnd,
  otl_value<otl_datetime> &p_coTimeLast,
  otl_value<std::string> &p_coTermCause )
{
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;

  pcrf_sql_queue_add_param( plistParam, p_coTimeEnd );
  pcrf_sql_queue_add_param( plistParam, p_coTimeLast );
  pcrf_sql_queue_add_param( plistParam, p_coTermCause );
  pcrf_sql_queue_add_param( plistParam, p_strSessionId );

  pcrf_sql_queue_enqueue(
    "update ps.sessionList"
    " set time_end = :time_end/*timestamp*/, time_last_req = nvl(:time_last/*timestamp*/,time_last_req), termination_cause = :term_cause/*char[64]*/"
    " where session_id = :session_id/*char[255]*/",
    plistParam,
    "update session",
    &p_strSessionId );
}

void pcrf_db_sessionUsage_offline( std::string &p_strSubscriberId, const std::vector<SSessionUsageInfo> &p_vectUsageInfo )
{
	std::vector<SSessionUsageInfo>::iterator iterSessionUsage;

	for( iterSessionUsage = p_vectUsageInfo.begin(); iterSessionUsage != p_vectUsageInfo.end(); ++iterSessionUsage ) {
		{
			std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;

			pcrf_sql_queue_add_param( plistParam, p_strSubscriberId );
			pcrf_sql_queue_add_param( plistParam, iterSessionUsage.m_coMonitoringKey );
			pcrf_sql_queue_add_param( plistParam, iterSessionUsage.m_coCCInputOctets );
			pcrf_sql_queue_add_param( plistParam, iterSessionUsage.m_coCCOutputOctets );
			pcrf_sql_queue_add_param( plistParam, iterSessionUsage.m_coCCTotalOctets );

			pcrf_sql_queue_enqueue(
				"begin"
				"	nInput number;"
				"	nOutput number;"
				"	nTotal number;"
				"	nRefresh number;"
				"ps.qm.ProcessQuota("
				"	:SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
				"	:UsedInputOctets /*ubigint,in*/, :UsedOutputOctets /*ubigint,in*/, :UsedTotalOctets /*ubigint,in*/,"
				"	nInput, nOutput, nTotal,"
				"	nRefresh );"
				"end;",
				plistParam,
				"store_usage_info_offline",
				NULL,
				true );
		}
	}
}

int pcrf_db_session_usage(
	otl_connect *p_pcoDBConn,
	std::string &p_strSubscriberId,
	std::map<std::string, SDBMonitoringInfo> &p_mapMonitInfo,
	const std::vector<SSessionUsageInfo> &p_vectUsageInfo,
	int &p_iUpdateRule )
{
	if( NULL != p_pcoDBConn ) {
	} else {
		return EINVAL;
	}

	bool bNothingToDo = true;
	std::vector<SSessionUsageInfo>::const_iterator iter;

	/* проверяем, надо ли обращаться к БД */
	for( iter = p_vectUsageInfo.begin(); iter != p_vectUsageInfo.end(); ++iter ) {
		if( 0 == iter->m_coCCInputOctets.is_null() && 0 != iter->m_coCCInputOctets.v
			|| 0 == iter->m_coCCOutputOctets.is_null() && 0 != iter->m_coCCOutputOctets.v
			|| 0 == iter->m_coCCTotalOctets.is_null() && 0 != iter->m_coCCTotalOctets.v ) {
		/* запись содержит ненулевые значения потребленного трафика */
		/* вектор содержит полезную информацию */
			bNothingToDo = false;
			break;
		}
	}

	if( !bNothingToDo ) {
	} else {
	  /* если вектор пустой просто выходим из функции */
		return 0;
	}

	int iRetVal = 0;
	int iRepeat = 1;
	CTimeMeasurer coTM;

sql_repeat:

	try {
		otl_nocommit_stream coStream;
		int iUpdateRule;

		coStream.open(
			1,
			"begin ps.qm.ProcessQuota("
			"	:SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
			"	:UsedInputOctets /*ubigint,in*/, :UsedOutputOctets /*ubigint,in*/, :UsedTotalOctets /*ubigint,in*/,"
			"	:GrantedInputOctets /*ubigint,out*/, :GrantedOutputOctets /*ubigint,out*/, :GrantedTotalOctets /*ubigint,out*/,"
			"	:UpdateRule /*int,out*/);"
			"end; ",
			*p_pcoDBConn );
		for( iter = p_vectUsageInfo.begin(); iter != p_vectUsageInfo.end(); ++iter ) {
			if( 0 == iter->m_coCCInputOctets.is_null() && 0 != iter->m_coCCInputOctets.v
				|| 0 == iter->m_coCCOutputOctets.is_null() && 0 != iter->m_coCCOutputOctets.v
				|| 0 == iter->m_coCCTotalOctets.is_null() && 0 != iter->m_coCCTotalOctets.v ) {
			  /* запись содержит полезную информацию */
			} else {
			  /* запись не содержит полезную информацю, переходим к другой записи */
				continue;
			}
			coStream
				<< p_strSubscriberId
				<< iter->m_coMonitoringKey
				<< iter->m_coCCInputOctets
				<< iter->m_coCCOutputOctets
				<< iter->m_coCCTotalOctets;
			UTL_LOG_D( *g_pcoLog, "quota usage:%s;%s;%'lld;%'lld;%'lld;",
					   p_strSubscriberId.c_str(),
					   iter->m_coMonitoringKey.v.c_str(),
					   iter->m_coCCInputOctets.is_null() ? -1 : iter->m_coCCInputOctets.v,
					   iter->m_coCCOutputOctets.is_null() ? -1 : iter->m_coCCOutputOctets.v,
					   iter->m_coCCTotalOctets.is_null() ? -1 : iter->m_coCCTotalOctets.v );
			coStream
				>> iter->m_coCCInputOctets
				>> iter->m_coCCOutputOctets
				>> iter->m_coCCTotalOctets
				>> iUpdateRule;

			( 0 == iUpdateRule ) ? ( p_iUpdateRule = p_iUpdateRule ) : ( p_iUpdateRule = 1 );

			UTL_LOG_D( *g_pcoLog, "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
					   p_strSubscriberId.c_str(),
					   iter->m_coMonitoringKey.v.c_str(),
					   iter->m_coCCInputOctets.is_null() ? -1 : iter->m_coCCInputOctets.v,
					   iter->m_coCCOutputOctets.is_null() ? -1 : iter->m_coCCOutputOctets.v,
					   iter->m_coCCTotalOctets.is_null() ? -1 : iter->m_coCCTotalOctets.v );
					 /* запоминаем полученную информацию чтобы не повторять запросы к БД по этому ключу мониторинга */
			{
				SDBMonitoringInfo soMonitInfo;
				soMonitInfo.m_coDosageInputOctets = iter->m_coCCInputOctets;
				soMonitInfo.m_coDosageOutputOctets = iter->m_coCCOutputOctets;
				soMonitInfo.m_coDosageTotalOctets = iter->m_coCCTotalOctets;
				soMonitInfo.m_bIsReported = true;
				p_mapMonitInfo.insert( std::make_pair( iter->m_coMonitoringKey.v, soMonitInfo ) );
			}
		}
		p_pcoDBConn->commit();
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
		p_pcoDBConn->rollback();
		if( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
			--iRepeat;
			goto sql_repeat;
		}
		iRetVal = coExcept.code;
	}

	stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

	return iRetVal;
}

void pcrf_db_insert_rule (
	const SSessionInfo &p_soSessInfo,
	const SDBAbonRule &p_soRule)
{
  if ( 0 < p_soRule.m_strRuleName.length() ) {
    pcrf_session_rule_cache_insert( p_soSessInfo.m_strSessionId, p_soRule.m_strRuleName );
  }

  std::list<SSQLQueueParam*> *plistParameters = new std::list<SSQLQueueParam*>;
  otl_value<otl_datetime> coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_strSessionId );
  pcrf_sql_queue_add_param( plistParameters, coDateTime );
  pcrf_sql_queue_add_param( plistParameters, p_soRule.m_strRuleName );

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionRule "
    "(session_id,time_start,rule_name) "
    "values (:session_id /*char[255]*/, :time_start/*timestamp*/, :rule_name /*char[255]*/)",
    plistParameters,
    "insert rule",
    &( p_soSessInfo.m_strSessionId ) );
}

void pcrf_db_close_session_rule_all ( std::string &p_strSessionId )
{
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;
  otl_value<otl_datetime> coTimeEnd;

  pcrf_fill_otl_datetime( coTimeEnd, NULL );

  pcrf_sql_queue_add_param( plistParam, coTimeEnd );
  pcrf_sql_queue_add_param( plistParam, p_strSessionId );

  pcrf_sql_queue_enqueue(
    "update /*+ index(sr IE1_SESSIONRULE_SESSION_ID)*/ ps.sessionRule sr set time_end = :time_end/*timestamp*/ where session_id = :session_id /* char[255] */ and time_end is null",
    plistParam,
    "close rule all",
    &p_strSessionId );
}

void pcrf_db_close_session_rule (
	const SSessionInfo *p_psoSessInfo,
	const std::string &p_strRuleName,
  const std::string *p_pstrRuleFailureCode)
{
  if ( NULL != p_psoSessInfo ) {
  } else {
    return;
  }

  pcrf_session_rule_cache_remove_rule(p_psoSessInfo->m_strSessionId, p_strRuleName);

  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;
  otl_value<otl_datetime> coTimeEnd;
  otl_value<std::string>  coFailureCode;
  otl_value<std::string>  coRuleName( p_strRuleName );

  pcrf_fill_otl_datetime( coTimeEnd, NULL );

  if ( NULL == p_pstrRuleFailureCode ) {
  } else {
    coFailureCode = *p_pstrRuleFailureCode;
  }

  pcrf_sql_queue_add_param( plistParam, coTimeEnd );
  pcrf_sql_queue_add_param( plistParam, coFailureCode );
  pcrf_sql_queue_add_param( plistParam, p_psoSessInfo->m_strSessionId );
  pcrf_sql_queue_add_param( plistParam, coRuleName );

  pcrf_sql_queue_enqueue(
    "update /*+ index(sr IE1_SESSIONRULE_SESSION_ID)*/ "
      "ps.sessionRule sr "
    "set "
      "time_end = :time_end/*timestamp*/,"
      "rule_failure_code = :rule_failure_code /*char[64]*/ "
    "where "
      "session_id = :session_id /*char[255]*/ "
      "and rule_name = :rule_name /*char[100]*/ "
      "and time_end is null",
    plistParam,
    "close rule",
    &p_psoSessInfo->m_strSessionId );
}

void pcrf_parse_date_time( std::string &p_strDateTime, otl_value<otl_datetime> &p_soDateTime )
{
  tm soTM;

  if ( 6 == sscanf( p_strDateTime.c_str(), "%u.%u.%u %u:%u:%u", &soTM.tm_mday, &soTM.tm_mon, &soTM.tm_year, &soTM.tm_hour, &soTM.tm_min, &soTM.tm_sec ) ) {
    soTM.tm_year -= 1900;
    --soTM.tm_mon;
    pcrf_fill_otl_datetime( p_soDateTime, &soTM );
  }
}

void pcrf_parse_rule_row( std::string &p_strRuleRow, std::list<std::string> &p_listRuleList, std::string &p_strSubscriberId )
{
	size_t stEnd;

	stEnd = p_strRuleRow.find( 9 );
	if( stEnd != std::string::npos && 0 != stEnd ) {
		p_listRuleList.push_back( p_strRuleRow.substr( 0, stEnd ) );
		UTL_LOG_D( *g_pcoLog, "Subscriber-Id: '%s'; rule: '%s'", p_strSubscriberId.c_str(), p_strRuleRow.substr( 0, stEnd ).c_str() );
	} else {
		UTL_LOG_D( *g_pcoLog, "invalid rule row: length: '%d'; content: '%s'", stEnd, p_strRuleRow.c_str() );
		return;
	}

	++stEnd;

	if( p_strRuleRow.length() - stEnd == 19 ) {
		otl_value<otl_datetime> coRefreshTime;
		std::string strDateTime;

		strDateTime = p_strRuleRow.substr( stEnd );
		pcrf_parse_date_time( strDateTime, coRefreshTime );
		if( 0 != coRefreshTime.is_null() ) {
		} else {
			pcrf_server_db_insert_refqueue( "subscriber_id", p_strSubscriberId, &( coRefreshTime.v ), NULL );
		}
	} else if( p_strRuleRow.length() < stEnd ) {
		UTL_LOG_D( *g_pcoLog, "invalid date/time lentgth: '%d'; content: '%s'", p_strRuleRow.length() - stEnd, p_strRuleRow.substr( stEnd ).c_str() );
	}
}

void pcrf_parse_rule_list( std::string &p_strRuleList, std::list<std::string> &p_listRuleList, std::string &p_strSubscriberId )
{
	std::string strRuleRow;
	size_t stBeginRow = 0;
	size_t stEndRow;

	while( std::string::npos != ( stEndRow = p_strRuleList.find( 10, stBeginRow ) ) ) {
		strRuleRow = p_strRuleList.substr( stBeginRow, stEndRow - stBeginRow );
		pcrf_parse_rule_row( strRuleRow, p_listRuleList, p_strSubscriberId );
		stBeginRow = stEndRow;
		++stBeginRow;
	}
}

/* загружает список идентификаторов правил абонента из БД */
int pcrf_db_load_abon_rule_list(
	otl_connect *p_pcoDBConn,
	std::string &p_strSubscriberId,
	unsigned int p_uiPeerDialect,
	otl_value<std::string> &p_coIPCANType,
	otl_value<std::string> &p_coRATType,
	otl_value<std::string> &p_coCalledStationId,
	otl_value<std::string> &p_coSGSNAddress,
	otl_value<std::string> &p_coIMEI,
	std::list<std::string> &p_listRuleList )
{
	UTL_LOG_D(
		*g_pcoLog,
		"enter: %s; db connection: %p; subscriber-id: %s; peer dialect: %d; ip-can-type: %s; rat-type: %s; apn: %s; sgsn: %s; imei: %s",
		__FUNCTION__,
		p_pcoDBConn,
		p_strSubscriberId.c_str(),
		p_uiPeerDialect,
		p_coIPCANType.v.c_str(),
		p_coRATType.v.c_str(),
		p_coCalledStationId.v.c_str(),
		p_coSGSNAddress.v.c_str(),
		p_coIMEI.v.c_str() );

	int iRetVal = 0;
	int iRepeat = 1;
	CTimeMeasurer coTM;

	if( NULL != p_pcoDBConn ) {
	} else {
		iRetVal = EINVAL;
		goto exit;
	}

sql_repeat:

	try {
		otl_nocommit_stream coStream;
		std::string strSQLResult;

		coStream.open(
			1,
			"begin "
			":rule_list/*char[4000],out*/ := ps.GetSubRules2("
			":subscriber_id/*char[64],in*/,"
			":peer_dialect/*unsigned,in*/,"
			":ip_can_type/*char[20],in*/,"
			":rat_type/*char[20],in*/,"
			":apn_name/*char[255],in*/,"
			":sgsn_node_ip_address/*char[16],in*/,"
			":IMEI/*char[20],in*/"
			");"
			"end;",
			*p_pcoDBConn );
		coStream
			<< p_strSubscriberId
			<< p_uiPeerDialect
			<< p_coIPCANType
			<< p_coRATType
			<< p_coCalledStationId
			<< p_coSGSNAddress
			<< p_coIMEI;
		if( ! coStream.eof() ) {
			coStream
				>> strSQLResult;
			UTL_LOG_D( *g_pcoLog, "rule list: '%s'", strSQLResult.c_str() );
			pcrf_parse_rule_list( strSQLResult, p_listRuleList, p_strSubscriberId );
		} else {
			iRetVal = 1403;
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

	stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

exit:

	UTL_LOG_D( *g_pcoLog, "leave: %s; result code: %d", __FUNCTION__, iRetVal );

	return iRetVal;
}

void pcrf_server_db_close_user_loc(std::string &p_strSessionId)
{
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;
  otl_value<otl_datetime>   coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  /* закрываем все открытые записи */
  pcrf_sql_queue_add_param( plistParam, coDateTime );
  pcrf_sql_queue_add_param( plistParam, p_strSessionId );

  pcrf_sql_queue_enqueue(
    "update /*+ index(loc ie1_sessionlocation_session_id)*/ ps.sessionLocation loc set time_end = :time_end/*timestamp*/ where time_end is null and session_id = :session_id /*char[255]*/",
    plistParam,
    "close location",
    &p_strSessionId );
}

void pcrf_server_db_user_location( SMsgDataForDB &p_soMsgInfo )
{
	/* если нечего сохранять в БД */
	if (p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_bLoaded) {
  } else {
		return;
  }

  /* закрываем открытые записи */
  pcrf_server_db_close_user_loc( p_soMsgInfo.m_psoSessInfo->m_strSessionId );

  /* добавляем новую запись */
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;

  otl_value<otl_datetime> coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_strSessionId );
  pcrf_sql_queue_add_param( plistParam, coDateTime);
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_coSGSNMCCMNC );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_coSGSNAddress );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_coSGSNIPv6Address );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_coRATType );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_coIPCANType );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coTAI );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserEnvironment.m_soUsrLoc.m_coRAI);

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionLocation "
    "(session_id, time_start, time_end, sgsn_mcc_mnc, sgsn_ip_address, sgsn_ipv6_address, rat_type, ip_can_type, cgi, ecgi, tai, rai) "
    "values "
    "(:session_id/*char[255]*/, :time_start/*timestamp*/, null, :sgsn_mcc_mnc/*char[10]*/, :sgsn_ip_address/*char[15]*/, :sgsn_ipv6_address/*char[50]*/, :rat_type/*char[50]*/, :ip_can_type/*char[20]*/, :cgi/*char[20]*/, :ecgi/*char[20]*/, :tai/*char[20]*/, :rai/*char[20]*/)",
    plistParam,
    "insert location",
    &( p_soMsgInfo.m_psoSessInfo->m_strSessionId ) );
}

int pcrf_server_db_monit_key( otl_connect *p_pcoDBConn, std::string &p_strSubscriberId, std::map<std::string, SDBMonitoringInfo > &p_mapMonitInfo )
{
	LOG_D( "enter: %s", __FUNCTION__ );

	int iRetVal = 0;
	int iRepeat = 1;
	CTimeMeasurer coTM;

	/* если список ключей мониторинга пуст */
	if( 0 != p_mapMonitInfo.size() ) {
	} else {
	  /* выходим ничего не делая */
		goto exit_from_function;
	}

	if( NULL != p_pcoDBConn ) {
	} else {
		iRetVal = EINVAL;
		goto exit_from_function;
	}

sql_repeat:

	try {
		otl_nocommit_stream coStream;

		coStream.open(
			1,
			"begin ps.qm.ProcessQuota("
			":SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
			"NULL, NULL, NULL,"
			":GrantedInputOctets /*ubigint,out*/, :GrantedOutputOctets /*ubigint,out*/, :GrantedTotalOctets /*ubigint,out*/);"
			"end;",
			*p_pcoDBConn );
		std::map<std::string, SDBMonitoringInfo>::iterator iterMonitList;
		for( iterMonitList = p_mapMonitInfo.begin(); iterMonitList != p_mapMonitInfo.end(); ++iterMonitList ) {
		  /* если данные из БД еще не загружены */
			if( iterMonitList->second.m_bIsReported ) {
				continue;
			} else {
				LOG_D( "%s: subscriber-id: %s; monitoring-key: %s", __FUNCTION__, p_strSubscriberId.c_str(), iterMonitList->first.c_str() );
				coStream
					<< p_strSubscriberId
					<< iterMonitList->first;
				coStream
					>> iterMonitList->second.m_coDosageInputOctets
					>> iterMonitList->second.m_coDosageOutputOctets
					>> iterMonitList->second.m_coDosageTotalOctets;
				LOG_D( "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
					   p_strSubscriberId.c_str(),
					   iterMonitList->first.c_str(),
					   iterMonitList->second.m_coDosageInputOctets.is_null() ? -1 : iterMonitList->second.m_coDosageInputOctets.v,
					   iterMonitList->second.m_coDosageOutputOctets.is_null() ? -1 : iterMonitList->second.m_coDosageOutputOctets.v,
					   iterMonitList->second.m_coDosageTotalOctets.is_null() ? -1 : iterMonitList->second.m_coDosageTotalOctets.v );
			}
		}
		p_pcoDBConn->commit();
		coStream.close();
	} catch( otl_exception &coExcept ) {
		UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
		p_pcoDBConn->rollback();
		if( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
			--iRepeat;
			goto sql_repeat;
		}
		iRetVal = coExcept.code;
	}

exit_from_function:
	stat_measure( g_psoDBStat, __FUNCTION__, &coTM );
	LOG_D( "leave: %s", __FUNCTION__ );

	return iRetVal;
}

void pcrf_server_db_insert_refqueue (
	const char *p_pszIdentifierType,
	const std::string &p_strIdentifier,
	otl_datetime *p_pcoDateTime,
	const char *p_pszAction)
{
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;
  otl_value<std::string> coIdenType;
  otl_value<std::string> coIdent( p_strIdentifier );
	otl_value<std::string> coAction;
	otl_value<otl_datetime> coRefreshDate;

  if ( NULL != p_pszIdentifierType ) {
    coIdenType = p_pszIdentifierType;
  }
	if (NULL != p_pszAction) {
		coAction = p_pszAction;
  }
	if (NULL != p_pcoDateTime) {
		coRefreshDate = *p_pcoDateTime;
  } else {
    pcrf_fill_otl_datetime( coRefreshDate, NULL );
  }

  pcrf_sql_queue_add_param( plistParam, coIdenType );
  pcrf_sql_queue_add_param( plistParam, coIdent );
  pcrf_sql_queue_add_param( plistParam, coRefreshDate );
  pcrf_sql_queue_add_param( plistParam, coAction );

  pcrf_sql_queue_enqueue(
    "insert into ps.refreshQueue "
    "(identifier_type, identifier, module, refresh_date, action) "
    "values "
    "(:identifier_type /*char[64]*/, :identifier/*char[64]*/, 'pcrf', :refresh_date/*timestamp*/, :action/*char[20]*/)",
    plistParam,
    "insert refresh record" );
}

int pcrf_procera_db_load_sess_list( std::string &p_strIPCANSessionId, std::vector<SSessionInfo> &p_vectSessList )
{
  otl_connect *pcoDBConn;

  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  sql_repeat:

  try {
    otl_nocommit_stream coStream;
    SSessionInfo soSessInfo;

    soSessInfo.m_uiPeerDialect = GX_PROCERA;
    coStream.open(
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
        "and p.protocol_id = :dialect_id /*int*/ "
        "and sl.time_end is null",
      *pcoDBConn );
    coStream
      << p_strIPCANSessionId
      << GX_PROCERA;
    while ( ! coStream.eof() ) {
      coStream
        >> soSessInfo.m_strSessionId
        >> soSessInfo.m_coOriginHost
        >> soSessInfo.m_coOriginRealm;
      p_vectSessList.push_back( soSessInfo );
    }
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

  if ( NULL != pcoDBConn ) {
    CHECK_POSIX_DO( pcrf_db_pool_rel( reinterpret_cast<void *>( pcoDBConn ), __FUNCTION__ ), /*continue*/ );
  }

  stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

  return iRetVal;
}

int pcrf_procera_db_load_location_rule (otl_connect *p_pcoDBConn, std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectRuleList)
{
	int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  sql_repeat:

	try {
    otl_nocommit_stream coStream;
    SDBAbonRule soRule;

    soRule.m_bIsActive = true;
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
			<< p_strSessionId;
    while (! coStream.eof()) {
      coStream
        >> soRule.m_strRuleName;
      p_vectRuleList.push_back(soRule);
      UTL_LOG_D(*g_pcoLog, "rule name: '%s'; session-id: '%s'", soRule.m_strRuleName.c_str(), p_strSessionId.c_str());
    }
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
		iRetVal = coExcept.code;
	}

  stat_measure(g_psoDBStat, __FUNCTION__, &coTM);

	return iRetVal;
}

void pcrf_server_db_insert_tethering_info( SMsgDataForDB &p_soMsgInfo )
{
  std::list<SSQLQueueParam*> *plistParam = new std::list<SSQLQueueParam*>;
  otl_value<std::string> coSubscriber( p_soMsgInfo.m_psoSessInfo->m_strSubscriberId );

  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_strSessionId);
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI );
  pcrf_sql_queue_add_param( plistParam, coSubscriber );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_coTetheringFlag );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coOriginHost );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coOriginRealm );

  pcrf_sql_queue_enqueue(
    "insert into ps.tetheringdetection (session_id, end_user_imsi, subscriber_id, event_date, tethering_status, origin_host, origin_realm) "
    "values (:session_id/*char[255]*/, :imsi/*char[32]*/, :subscriber_id/*char[64]*/, sysdate, :tethering_status/*unsigned*/, :origin_host/*char[255]*/, :origin_realm/*char[255]*/)",
    plistParam,
    "insert tethering flag",
    &( p_soMsgInfo.m_psoSessInfo->m_strSessionId ) );
}
