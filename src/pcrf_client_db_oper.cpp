#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <stdio.h>

extern CLog *g_pcoLog;
extern SStat *g_psoDBStat;

int pcrf_client_db_refqueue( otl_connect *p_pcoDBConn, std::vector<SRefQueue> &p_vectQueue )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;
  SRefQueue soQueueElem;
  CTimeMeasurer coTM;
  otl_value<std::string> coModule;

  coModule.v.insert( 0, ( const char* )fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len );
  coModule.set_non_null();

  sql_restore:

  try {
    otl_nocommit_stream coStream;

    /* создаем объект класса потока ДБ */
    coStream.open(
      1000,
      "select rowid, identifier, identifier_type, action from ps.refreshQueue where module = :module/*char[100]*/ and refresh_date < sysdate",
      *p_pcoDBConn );
    /* делаем выборку из БД */
    coStream << coModule;
    while ( ! coStream.eof() ) {
      coStream
        >> soQueueElem.m_strRowId
        >> soQueueElem.m_strIdentifier
        >> soQueueElem.m_strIdentifierType
        >> soQueueElem.m_coAction;
      p_vectQueue.push_back( soQueueElem );
    }
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_restore;
    }
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoDBStat, __FUNCTION__, &coTM );

  return iRetVal;
}

void pcrf_client_db_fix_staled_sess ( otl_value<std::string> &p_coSessionId )
{
	/* закрываем зависшую сессию */
  otl_value<otl_datetime> coSysdate;
  otl_value<std::string> coTermCause;

  pcrf_fill_otl_datetime( coSysdate, NULL );

  pcrf_db_update_session( p_coSessionId , coSysdate, coSysdate, coTermCause);
	/* фиксируем правила зависшей сессиии */
  pcrf_db_close_session_rule_all( p_coSessionId );
	/* фиксируем локации зависшей сессиии */
  pcrf_server_db_close_user_loc( p_coSessionId );
}

int pcrf_client_db_load_session_list( otl_connect *p_pcoDBConn, SRefQueue &p_soReqQueue, std::vector<std::string> &p_vectSessionList )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  int iRetVal = 0;
  int iRepeat = 1;

  /* очищаем список перед выполнением */
  p_vectSessionList.clear();

  if ( 0 == p_soReqQueue.m_strIdentifierType.compare( "subscriber_id" ) ) {
    if ( 0 == pcrf_session_cache_get_subscriber_session_id( p_soReqQueue.m_strIdentifier, p_vectSessionList ) ) {
    } else {
      if ( NULL != p_pcoDBConn ) {
      } else {
        return EINVAL;
      }

      sql_repeat:

      try {
        otl_nocommit_stream coStream;

        std::string strSessionId;
        coStream.open(
        10,
        "select session_id from ps.sessionList where subscriber_id = :subscriber_id/*char[64]*/ and time_end is null",
        *p_pcoDBConn );
        coStream
          << p_soReqQueue.m_strIdentifier;
        while ( 0 == coStream.eof() ) {
          coStream
            >> strSessionId;
          p_vectSessionList.push_back( strSessionId );
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
    }
  } else if ( 0 == p_soReqQueue.m_strIdentifierType.compare( "session_id" ) ) {
    p_vectSessionList.push_back( p_soReqQueue.m_strIdentifier );
  } else {
    UTL_LOG_F( *g_pcoLog, "unsupported identifier type: '%s'", p_soReqQueue.m_strIdentifierType.c_str() );
    iRetVal = -1;
  }

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}

int pcrf_client_db_delete_refqueue( otl_connect *p_pcoDBConn, SRefQueue &p_soRefQueue )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

  if ( 0 == p_soRefQueue.m_strRowId.length() ) {
    return 0;
  }

  int iRetVal = 0;
  int iRepeat = 1;
  CTimeMeasurer coTM;

  sql_repeat:

  try {
    otl_nocommit_stream coStream;

    coStream.open(
      1,
      "delete from ps.refreshQueue where rowid = :row_id /*char[256]*/",
      *p_pcoDBConn );
    coStream
      << p_soRefQueue.m_strRowId;
    p_pcoDBConn->commit();
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
