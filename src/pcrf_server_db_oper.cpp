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

int pcrf_server_req_db_store( otl_connect *p_pcoDBConn, struct SMsgDataForDB *p_psoMsgInfo )
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
        pcrf_server_db_close_user_loc( p_psoMsgInfo->m_psoSessInfo->m_coSessionId );
      case UPDATE_REQUEST: /* UPDATE_REQUEST */
      case EVENT_REQUEST: /* EVENT_REQUEST */
        /* выполянем запрос на обновление записи */
      {
        otl_value<otl_datetime> coTimeLast;

        pcrf_fill_otl_datetime( coTimeLast, NULL );
        pcrf_db_update_session( p_psoMsgInfo->m_psoSessInfo->m_coSessionId, p_psoMsgInfo->m_psoSessInfo->m_coTimeEnd, coTimeLast, p_psoMsgInfo->m_psoSessInfo->m_coTermCause );
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
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
      pcrf_db_close_session_rule( *( p_psoMsgInfo->m_psoSessInfo ), iter->m_coChargingRuleName.v, &( iter->m_coRuleFailureCode.v ) );
    }
		/* потом закрываем оставшиеся правила */
    pcrf_db_close_session_rule_all( p_psoMsgInfo->m_psoSessInfo->m_coSessionId );
		break;
	case UPDATE_REQUEST: /* UPDATE_REQUEST */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
      pcrf_db_close_session_rule( *( p_psoMsgInfo->m_psoSessInfo ), iter->m_coChargingRuleName.v, &( iter->m_coRuleFailureCode.v ) );
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
  std::list<SSQLQueueParam> *plistParameters = new std::list<SSQLQueueParam>;
  otl_value<std::string> coSubscriberId( p_soSessInfo.m_strSubscriberId );
  otl_value<otl_datetime> coTime;

  pcrf_fill_otl_datetime( coTime, NULL );

  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coSessionId,       m_eSQLParamType_StdString);
  pcrf_sql_queue_add_param( plistParameters, coSubscriberId,                   m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coOriginHost,      m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coOriginRealm,     m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coEndUserIMSI,     m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coEndUserE164,     m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coIMEI,            m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, coTime,                           m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParameters, coTime,                           m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coFramedIPAddress, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coCalledStationId, m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionList (session_id,subscriber_id,origin_host,origin_realm,end_user_imsi,end_user_e164,imeisv,time_start,time_last_req,framed_ip_address,called_station_id)"
    "values(:session_id/*char[255]*/,:subscriber_id/*char[64]*/,:origin_host/*char[255]*/,:origin_realm/*char[255]*/,:end_user_imsi/*char[32]*/,:end_user_e164/*char[16]*/,:imeisv/*char[20]*/,:start_time/*timestamp*/,:time_last/*timestamp*/,:framed_ip_address/*char[16]*/,:called_station_id/*char[255]*/)",
    plistParameters,
    "insert session",
    &( p_soSessInfo.m_coSessionId.v ) );
}

void pcrf_db_update_session (
  otl_value<std::string> &p_coSessionId,
  otl_value<otl_datetime> &p_coTimeEnd,
  otl_value<otl_datetime> &p_coTimeLast,
  otl_value<std::string> &p_coTermCause )
{
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;

  pcrf_sql_queue_add_param( plistParam, p_coTimeEnd,   m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, p_coTimeLast,  m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, p_coTermCause, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_coSessionId, m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "update ps.sessionList"
    " set time_end = :time_end/*timestamp*/, time_last_req = nvl(:time_last/*timestamp*/,time_last_req), termination_cause = :term_cause/*char[64]*/"
    " where session_id = :session_id/*char[255]*/",
    plistParam,
    "update session",
    &( p_coSessionId.v ) );
}

int pcrf_db_session_usage( otl_connect *p_pcoDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo, int &p_iUpdateRule )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

  bool bNothingToDo = true;
  std::vector<SSessionUsageInfo>::iterator iter;

  /* проверяем, надо ли обращаться к БД */
  for ( iter = p_soReqInfo.m_vectUsageInfo.begin(); iter != p_soReqInfo.m_vectUsageInfo.end(); ++iter ) {
    if (     0 == iter->m_coCCInputOctets.is_null()  && 0 != iter->m_coCCInputOctets.v
          || 0 == iter->m_coCCOutputOctets.is_null() && 0 != iter->m_coCCOutputOctets.v
          || 0 == iter->m_coCCTotalOctets.is_null()  && 0 != iter->m_coCCTotalOctets.v ) {
      /* запись содержит ненулевые значения потребленного трафика */
      /* вектор содержит полезную информацию */
      bNothingToDo = false;
      break;
    }
  }

  if ( !bNothingToDo ) {
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
        ":SubscriberID /*char[255],in*/, :MonitoringKey /*char[32],in*/,"
        ":UsedInputOctets /*ubigint,in*/, :UsedOutputOctets /*ubigint,in*/, :UsedTotalOctets /*ubigint,in*/,"
        ":GrantedInputOctets /*ubigint,out*/, :GrantedOutputOctets /*ubigint,out*/, :GrantedTotalOctets /*ubigint,out*/,"
        ":UpdateRule /*int,out*/);"
      "end; ",
      *p_pcoDBConn );
    for ( iter = p_soReqInfo.m_vectUsageInfo.begin(); iter != p_soReqInfo.m_vectUsageInfo.end(); ++iter ) {
      if (   0 == iter->m_coCCInputOctets.is_null()  && 0 != iter->m_coCCInputOctets.v
          || 0 == iter->m_coCCOutputOctets.is_null() && 0 != iter->m_coCCOutputOctets.v
          || 0 == iter->m_coCCTotalOctets.is_null()  && 0 != iter->m_coCCTotalOctets.v ) {
        /* запись содержит полезную информацию */
      } else {
        /* запись не содержит полезную информацю, переходим к другой записи */
        continue;
      }
      coStream
        << p_soSessInfo.m_strSubscriberId
        << iter->m_coMonitoringKey
        << iter->m_coCCInputOctets
        << iter->m_coCCOutputOctets
        << iter->m_coCCTotalOctets;
      UTL_LOG_D( *g_pcoLog, "quota usage:%s;%s;%'lld;%'lld;%'lld;",
        p_soSessInfo.m_strSubscriberId.c_str(),
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
        p_soSessInfo.m_strSubscriberId.c_str(),
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
        p_soSessInfo.m_mapMonitInfo.insert( std::make_pair( iter->m_coMonitoringKey.v, soMonitInfo ) );
      }
    }
    p_pcoDBConn->commit();
    coStream.close();
    LOG_D( "Session-Id: %s: usage monitoring information is stored", p_soSessInfo.m_coSessionId.v.c_str() );
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    p_pcoDBConn->rollback();
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

  return iRetVal;
}

void pcrf_db_insert_rule (
	SSessionInfo &p_soSessInfo,
	SDBAbonRule &p_soRule)
{
  if ( 0 == p_soRule.m_coRuleName.is_null() && 0 < p_soRule.m_coRuleName.v.length() ) {
    pcrf_session_rule_cache_insert( p_soSessInfo.m_coSessionId.v, p_soRule.m_coRuleName.v );
  }

  std::list<SSQLQueueParam> *plistParameters = new std::list<SSQLQueueParam>;
  otl_value<otl_datetime> coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  pcrf_sql_queue_add_param( plistParameters, p_soSessInfo.m_coSessionId, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParameters, coDateTime,                 m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParameters, p_soRule.m_coRuleName,      m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionRule "
    "(session_id,time_start,rule_name) "
    "values (:session_id /*char[255]*/, :time_start/*timestamp*/, :rule_name /*char[255]*/)",
    plistParameters,
    "insert rule",
    &( p_soSessInfo.m_coSessionId.v ) );
}

void pcrf_db_close_session_rule_all ( otl_value<std::string> &p_coSessionId )
{
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;
  otl_value<otl_datetime> coTimeEnd;

  pcrf_fill_otl_datetime( coTimeEnd, NULL );

  pcrf_sql_queue_add_param( plistParam, coTimeEnd,     m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, p_coSessionId, m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "update ps.sessionRule set time_end = :time_end/*timestamp*/ where session_id = :session_id /* char[255] */ and time_end is null",
    plistParam,
    "close rule all",
    &( p_coSessionId.v ) );
}

void pcrf_db_close_session_rule (
	SSessionInfo &p_soSessInfo,
	std::string &p_strRuleName,
  std::string *p_pstrRuleFailureCode)
{
  pcrf_session_rule_cache_remove_rule(p_soSessInfo.m_coSessionId.v, p_strRuleName);

  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;
  otl_value<otl_datetime> coTimeEnd;
  otl_value<std::string>  coFailureCode;
  otl_value<std::string>  coRuleName( p_strRuleName );

  pcrf_fill_otl_datetime( coTimeEnd, NULL );

  if ( NULL == p_pstrRuleFailureCode ) {
  } else {
    coFailureCode = *p_pstrRuleFailureCode;
  }

  pcrf_sql_queue_add_param( plistParam, coTimeEnd,                  m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, coFailureCode,              m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soSessInfo.m_coSessionId, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, coRuleName,                 m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "update "
      "ps.sessionRule "
    "set "
      "time_end = :time_end/*timestamp*/,"
      "rule_failure_code = :rule_failure_code /*char[64]*/ "
    "where "
      "session_id = :session_id /*char[255]*/ "
      "and rule_name = :rule_name /*char[100]*/ "
      "and time_end is null",
    plistParam,
    "close rule",
    &( p_soSessInfo.m_coSessionId.v ) );
}

/* загружает идентификатор абонента (subscriber_id) из БД */
int pcrf_server_db_load_subscriber_id (otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo)
{
	if (NULL != p_pcoDBConn) {
  } else {
		return EINVAL;
  }

	int iRetVal = 0;
  int iRepeat = 1;
	CTimeMeasurer coTM;

  sql_repeat:

  try {
    otl_nocommit_stream coStream;

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
		if (0 == coStream.eof()) {
      coStream
        >> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId;
    } else {
      UTL_LOG_E(*g_pcoLog, "subscriber not found: imsi: '%s';", 0 == p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI.is_null() ? p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI.v.c_str() : "<null>");
      p_soMsgInfo.m_psoSessInfo->m_strSubscriberId = "";
      iRetVal = 1403;
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

	stat_measure (g_psoDBStat, __FUNCTION__, &coTM);

	return iRetVal;
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

void pcrf_parse_rule_row( std::string &p_strRuleRow, std::vector<std::string> &p_vectRuleList, std::string &p_strSubscriberId )
{
  size_t stEnd;

  stEnd = p_strRuleRow.find( 9 );
  if ( stEnd != std::string::npos && 0 != stEnd) {
    p_vectRuleList.push_back( p_strRuleRow.substr( 0, stEnd ) );
  } else {
    UTL_LOG_D( *g_pcoLog, "invalid rule row: length: '%d'; content: '%s'", stEnd, p_strRuleRow.c_str() );
    return;
  }

  ++stEnd;

  if ( p_strRuleRow.length() - stEnd == 19 ) {
    otl_value<otl_datetime> coRefreshTime;
    std::string strDateTime;

    strDateTime = p_strRuleRow.substr( stEnd );
    pcrf_parse_date_time( strDateTime, coRefreshTime );
    if ( 0 != coRefreshTime.is_null() ) {
    } else {
      pcrf_server_db_insert_refqueue( "subscriber_id", p_strSubscriberId, &( coRefreshTime.v ), NULL );
    }
  } else if ( p_strRuleRow.length() < stEnd ) {
    UTL_LOG_D( *g_pcoLog, "invalid date/time lentgth: '%d'; content: '%s'", p_strRuleRow.length() - stEnd, p_strRuleRow.substr( stEnd ).c_str() );
  }
}

void pcrf_parse_rule_list( std::string &p_strRuleList, std::vector<std::string> &p_vectRuleList, std::string &p_strSubscriberId )
{
  std::string strRuleRow;
  size_t stBeginRow = 0;
  size_t stEndRow;

  while ( std::string::npos != (stEndRow = p_strRuleList.find(10, stBeginRow )) ) {
    strRuleRow = p_strRuleList.substr( stBeginRow, stEndRow - stBeginRow );
    pcrf_parse_rule_row( strRuleRow, p_vectRuleList, p_strSubscriberId );
    stBeginRow = stEndRow;
    ++stBeginRow;
  }
}

/* загружает список идентификаторов правил абонента из БД */
int pcrf_load_abon_rule_list(
  otl_connect *p_pcoDBConn,
  SMsgDataForDB &p_soMsgInfo,
  std::vector<std::string> &p_vectRuleList )
{
  LOG_D(
    "enter: %s; db connection: %p; subscriber-id: %s; peer dialect: %d; ip-can-type: %s; rat-type: %s; apn: %s; mcc-mnc: %s; sgsn: %s; imei: %s",
    __FUNCTION__,
    p_pcoDBConn,
    p_soMsgInfo.m_psoSessInfo->m_strSubscriberId.c_str(),
    p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect,
    p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType.v.c_str(),
    p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType.v.c_str(),
    p_soMsgInfo.m_psoSessInfo->m_coCalledStationId.v.c_str(),
    p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC.v.c_str(),
    p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress.v.c_str(),
    p_soMsgInfo.m_psoSessInfo->m_coIMEI.v.c_str()
  );

  int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  if ( NULL != p_pcoDBConn ) {
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
      << p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
      << p_soMsgInfo.m_psoSessInfo->m_uiPeerDialect
      << p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType
      << p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType
      << p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
      << p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress
      << p_soMsgInfo.m_psoSessInfo->m_coIMEI;
    if ( ! coStream.eof() ) {
      coStream
        >> strSQLResult;
      LOG_D( "rule list: '%s'", strSQLResult.c_str() );
      pcrf_parse_rule_list( strSQLResult, p_vectRuleList, p_soMsgInfo.m_psoSessInfo->m_strSubscriberId );
    } else {
      iRetVal = 1403;
    }
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

  exit:

  LOG_D( "leave: %s; result code: %d", __FUNCTION__, iRetVal );

  return iRetVal;
}

int pcrf_server_find_core_session( otl_connect *p_pcoDBConn, std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strUGWSessionId )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  sql_repeat:

  try {
    otl_nocommit_stream coStream;

    coStream.open(
      1,
      "select "
        "session_id "
      "from "
        "ps.sessionList sl "
        "inner join ps.peer p on sl.origin_host = p.host_name "
      "where "
        "subscriber_id = :subscriber_id /*char[64]*/ "
        "and framed_ip_address = :framed_ip_address /*char[16]*/ "
        "and p.protocol_id in(1, 4) /* GX_HW_UGW, GX_ERICSSN */",
      *p_pcoDBConn );
    coStream
      << p_strSubscriberId
      << p_strFramedIPAddress;
    if ( !coStream.eof() ) {
      coStream
        >> p_strUGWSessionId;
    } else {
      UTL_LOG_E(
        *g_pcoLog,
        "subscriber_id: '%s'; framed_ip_address: '%s': ugw session not found",
        p_strSubscriberId.c_str(),
        p_strFramedIPAddress.c_str() );
      iRetVal = 1403;
    }
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

  return iRetVal;
}

void pcrf_server_db_close_user_loc(otl_value<std::string> &p_strSessionId)
{
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;
  otl_value<std::string>    coSessionId( p_strSessionId );
  otl_value<otl_datetime>   coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  /* закрываем все открытые записи */
  pcrf_sql_queue_add_param( plistParam, coDateTime,  m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, coSessionId, m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "update ps.sessionLocation set time_end = :time_end/*timestamp*/ where time_end is null and session_id = :session_id /*char[255]*/",
    plistParam,
    "close location",
    &( coSessionId.v ) );
}

void pcrf_server_db_user_location( SMsgDataForDB &p_soMsgInfo )
{
	/* если нечего сохранять в БД */
	if (p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_bLoaded) {
  } else {
		return;
  }

  /* закрываем открытые записи */
  pcrf_server_db_close_user_loc( p_soMsgInfo.m_psoSessInfo->m_coSessionId );

  /* добавляем новую запись */
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;

  otl_value<otl_datetime> coDateTime;

  pcrf_fill_otl_datetime( coDateTime, NULL );

  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coSessionId,                           m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, coDateTime,                                                         m_eSQLParamType_OTLDateTime);
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC,      m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress,     m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coSGSNIPv6Address, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRATType,         m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType,       m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coCGI,             m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coECGI,            m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coTAI,             m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_soUserLocationInfo.m_coRAI,             m_eSQLParamType_StdString);

  pcrf_sql_queue_enqueue(
    "insert into ps.sessionLocation "
    "(session_id, time_start, time_end, sgsn_mcc_mnc, sgsn_ip_address, sgsn_ipv6_address, rat_type, ip_can_type, cgi, ecgi, tai, rai) "
    "values "
    "(:session_id/*char[255]*/, :time_start/*timestamp*/, null, :sgsn_mcc_mnc/*char[10]*/, :sgsn_ip_address/*char[15]*/, :sgsn_ipv6_address/*char[50]*/, :rat_type/*char[50]*/, :ip_can_type/*char[20]*/, :cgi/*char[20]*/, :ecgi/*char[20]*/, :tai/*char[20]*/, :rai/*char[20]*/)",
    plistParam,
    "insert location",
    &( p_soMsgInfo.m_psoSessInfo->m_coSessionId.v ) );
}

int pcrf_server_db_monit_key( otl_connect *p_pcoDBConn, SSessionInfo &p_soSessInfo )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  /* если список ключей мониторинга пуст */
  if ( 0 != p_soSessInfo.m_mapMonitInfo.size() ) {
  } else {
    /* выходим ничего не делая */
    goto exit_from_function;
  }

  if ( NULL != p_pcoDBConn ) {
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
    for ( iterMonitList = p_soSessInfo.m_mapMonitInfo.begin(); iterMonitList != p_soSessInfo.m_mapMonitInfo.end(); ++iterMonitList ) {
      /* если данные из БД еще не загружены */
      if ( iterMonitList->second.m_bIsReported ) {
        continue;
      } else {
        LOG_D( "%s: subscriber-id: %s; monitoring-key: %s", __FUNCTION__, p_soSessInfo.m_strSubscriberId.c_str(), iterMonitList->first.c_str() );
        coStream
          << p_soSessInfo.m_strSubscriberId
          << iterMonitList->first;
        coStream
          >> iterMonitList->second.m_coDosageInputOctets
          >> iterMonitList->second.m_coDosageOutputOctets
          >> iterMonitList->second.m_coDosageTotalOctets;
        LOG_D( "quota remainder:%s;%s;%'lld;%'lld;%'lld;",
          p_soSessInfo.m_strSubscriberId.c_str(),
          iterMonitList->first.c_str(),
          iterMonitList->second.m_coDosageInputOctets.is_null() ? -1 : iterMonitList->second.m_coDosageInputOctets.v,
          iterMonitList->second.m_coDosageOutputOctets.is_null() ? -1 : iterMonitList->second.m_coDosageOutputOctets.v,
          iterMonitList->second.m_coDosageTotalOctets.is_null() ? -1 : iterMonitList->second.m_coDosageTotalOctets.v );
      }
    }
    p_pcoDBConn->commit();
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    p_pcoDBConn->rollback();
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
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
	otl_datetime *p_coDateTime,
	const char *p_pszAction)
{
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;
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
	if (NULL != p_coDateTime) {
		coRefreshDate = *p_coDateTime;
  } else {
    pcrf_fill_otl_datetime( coRefreshDate, NULL );
  }

  pcrf_sql_queue_add_param( plistParam, coIdenType,    m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, coIdent,       m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, coRefreshDate, m_eSQLParamType_OTLDateTime );
  pcrf_sql_queue_add_param( plistParam, coAction,      m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "insert into ps.refreshQueue "
    "(identifier_type, identifier, module, refresh_date, action) "
    "values "
    "(:identifier_type /*char[64]*/, :identifier/*char[64]*/, 'pcrf', :refresh_date/*timestamp*/, :action/*char[20]*/)",
    plistParam,
    "insert refresh record" );
}

int pcrf_procera_db_load_sess_list( otl_value<std::string> &p_coUGWSessionId, std::vector<SSessionInfo> &p_vectSessList )
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
      << p_coUGWSessionId
      << GX_PROCERA;
    while ( ! coStream.eof() ) {
      coStream
        >> soSessInfo.m_coSessionId
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

int pcrf_procera_db_load_location_rule (otl_connect *p_pcoDBConn, otl_value<std::string> &p_coSessionId, std::vector<SDBAbonRule> &p_vectRuleList)
{
  if (NULL != p_pcoDBConn && 0 == p_coSessionId.is_null()) {
  } else {
    return EINVAL;
  }

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
  std::list<SSQLQueueParam> *plistParam = new std::list<SSQLQueueParam>;
  otl_value<std::string> coSubscriber( p_soMsgInfo.m_psoSessInfo->m_strSubscriberId );

  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coSessionId,   m_eSQLParamType_StdString);
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI, m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, coSubscriber,                               m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoReqInfo->m_coTetheringFlag, m_eSQLParamType_UInt );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coOriginHost,  m_eSQLParamType_StdString );
  pcrf_sql_queue_add_param( plistParam, p_soMsgInfo.m_psoSessInfo->m_coOriginRealm, m_eSQLParamType_StdString );

  pcrf_sql_queue_enqueue(
    "insert into ps.tetheringdetection (session_id, end_user_imsi, subscriber_id, event_date, tethering_status, origin_host, origin_realm) "
    "values (:session_id/*char[255]*/, :imsi/*char[32]*/, :subscriber_id/*char[64]*/, sysdate, :tethering_status/*unsigned*/, :origin_host/*char[255]*/, :origin_realm/*char[255]*/)",
    plistParam,
    "insert tethering flag",
    &( p_soMsgInfo.m_psoSessInfo->m_coSessionId.v ) );
}
