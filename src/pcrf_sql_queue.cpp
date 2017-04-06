#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

static SStat *g_psoSQLQueueStat;

static volatile int g_iWork;
/* функция обработки очереди запросов */
static void * pcrf_sql_queue_oper(void *);

struct SSQLRequestInfo
{
  const char *m_pszRequest;
  std::list<SSQLQueueParam> *m_plistParamList;
  const char *m_pszReqName;
  SSQLRequestInfo(
    const char *p_pszSQLReq,
    std::list<SSQLQueueParam> *p_plistParam,
    const char *p_pszReqName )
    : m_pszRequest( p_pszSQLReq ), m_plistParamList( p_plistParam ), m_pszReqName( p_pszReqName )
  { }
};

struct SSQLQueue {
  std::list<SSQLRequestInfo>         m_listSQLQueue;   /* очередь SQL-запросов */
  pthread_t                          m_threadSQLQueue; /* дескриптор потока обработки очереди */
  pthread_mutex_t                    m_mutexSQLQueue;  /* мьютекс доступа к очереди запросов */
};

static unsigned  g_uiQueueCount;     /* количество очередей */
static SSQLQueue *g_pmsoSQLQueue;    /* указатель на массив очередей */

int pcrf_sql_queue_init()
{
  LOG_D( "enter: %s", __FUNCTION__ );

  /* определяем количество очередей */
  if ( 2 < g_psoConf->m_iDBPoolSize ) {
    g_uiQueueCount = 2;
  } else {
    g_uiQueueCount = 1;
  }

  /* выделяем память под массив очередей */
  g_pmsoSQLQueue = new SSQLQueue[ g_uiQueueCount ];

  /* инициализация ветки статистики */
  g_psoSQLQueueStat = stat_get_branch( "sql queue" );

  g_iWork = 1;

  for ( unsigned i = 0; i < g_uiQueueCount; ++i ) {
    CHECK_FCT( pthread_mutex_init( &( g_pmsoSQLQueue[ i ].m_mutexSQLQueue ), NULL ) );
    CHECK_FCT( pthread_create( &( g_pmsoSQLQueue[ i ].m_threadSQLQueue ), NULL, pcrf_sql_queue_oper, &g_pmsoSQLQueue[ i ] ) );
  }

  LOG_D( "leave: %s", __FUNCTION__ );

  return 0;
}

void pcrf_sql_queue_fini()
{
  LOG_D( "enter: %s", __FUNCTION__ );

  g_iWork = 0;

  if ( NULL != g_pmsoSQLQueue ) {
    for ( unsigned i = 0; i < g_uiQueueCount; ++i ) {
      pthread_join( g_pmsoSQLQueue[ i ].m_threadSQLQueue, NULL );
      pthread_mutex_destroy( &( g_pmsoSQLQueue[ i ].m_mutexSQLQueue ) );
    }
    delete[ ] g_pmsoSQLQueue;
  }

  LOG_D( "leave: %s", __FUNCTION__ );
}

static void pcrf_sql_queue_dump_request( const char *p_pszStatementText, const SSQLRequestInfo *p_psoSQLReqInfo )
{
  std::string strReqData;
  std::list<SSQLQueueParam>::iterator iter = p_psoSQLReqInfo->m_plistParamList->begin();
  char mcValue[256];
  int iFnRes;

  strReqData = "statement name: ";
  strReqData += p_pszStatementText;

  for ( ; iter != p_psoSQLReqInfo->m_plistParamList->end(); ++iter ) {
    strReqData += '\t';
    if ( NULL != iter->m_pvParam ) {
    } else {
      strReqData += "<null data pointed>";
    }
    switch ( iter->m_eParamType ) {
      case m_eSQLParamType_Int:
        iFnRes = snprintf( mcValue, sizeof(mcValue), "'%d'", reinterpret_cast<otl_value<int>*>( iter->m_pvParam )->v );
        if ( 0 < iFnRes ) {
          if ( iFnRes < sizeof( mcValue ) ) {
          } else {
            mcValue[ sizeof( mcValue ) - 1 ] = '\0';
          }
          strReqData += mcValue;
        } else {
          strReqData += "<snprintf error>";
        }
        break;
      case m_eSQLParamType_UInt:
        iFnRes = snprintf( mcValue, sizeof( mcValue ), "'%u'", reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam )->v );
        if ( 0 < iFnRes ) {
          if ( iFnRes < sizeof( mcValue ) ) {
          } else {
            mcValue[ sizeof( mcValue ) - 1 ] = '\0';
          }
          strReqData += mcValue;
        } else {
          strReqData += "<snprintf error>";
        }
        break;
      case m_eSQLParamType_StdString:
        strReqData += '\'';
        strReqData += reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam )->v.c_str();
        strReqData += '\'';
        break;
      case m_eSQLParamType_Char:
        strReqData += '\'';
        strReqData += reinterpret_cast<otl_value<char*>*>( iter->m_pvParam )->v;
        strReqData += '\'';
        break;
      case m_eSQLParamType_OTLDateTime:
        strReqData += "<datetime>";
        break;
      default:
        iFnRes = snprintf( mcValue, sizeof( mcValue ), "<unsupported parameter type: %u>", iter->m_eParamType );
        if ( 0 < iFnRes ) {
          if ( iFnRes < sizeof( mcValue ) ) {
          } else {
            mcValue[ sizeof( mcValue ) - 1 ] = '\0';
          }
          strReqData += mcValue;
        } else {
          strReqData += "<snprintf error>";
        }
    }
  }

  g_pcoLog->Dump( "noti", strReqData.c_str() );
}

/* 
 * функция возвращает ненулевое значение если при выполнении возникла ошибка, связанная с подключением к БД
 * в остальных случаях (успешное выполнение запроса или ошибка в самом запросе) функция возвращает 0
 */
static int pcrf_sql_queue_oper_single( otl_connect *p_pcoDBConn, SSQLRequestInfo *p_psoSQLReqInfo )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  /* проверка параметров */
  if ( NULL != p_pcoDBConn && NULL != p_psoSQLReqInfo && NULL != p_psoSQLReqInfo->m_pszRequest && NULL != p_psoSQLReqInfo->m_plistParamList ) {
  } else {
    return EINVAL;
  }

  CTimeMeasurer coTM;
  int iRetVal = 0;

  try {
    otl_stream coStream( 1, p_psoSQLReqInfo->m_pszRequest, *p_pcoDBConn, NULL, p_psoSQLReqInfo->m_pszReqName );
    coStream.set_commit( 0 );

    /* передаем параметры зароса */
    std::list<SSQLQueueParam>::iterator iter = p_psoSQLReqInfo->m_plistParamList->begin();
    LOG_D( "%s: %s: parameter list size: %u", __FUNCTION__, p_psoSQLReqInfo->m_pszReqName, p_psoSQLReqInfo->m_plistParamList->size() );
    for ( ; iter != p_psoSQLReqInfo->m_plistParamList->end(); ++iter ) {
      if ( NULL != iter->m_pvParam ) {
      } else {
        iRetVal = EINVAL;
      }
      switch ( iter->m_eParamType ) {
        case m_eSQLParamType_Int:
          coStream << * reinterpret_cast<otl_value<int>*>(iter->m_pvParam);
          LOG_D( "%s: %d", __FUNCTION__, reinterpret_cast<otl_value<int>*>( iter->m_pvParam )->v );
          break;
        case m_eSQLParamType_UInt:
          coStream << * reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam );
          LOG_D( "%s: %u", __FUNCTION__, reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam )->v );
          break;
        case m_eSQLParamType_StdString:
          coStream << * reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam );
          LOG_D( "%s: %s", __FUNCTION__, reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam )->v.c_str() );
          break;
        case m_eSQLParamType_Char:
          coStream << * reinterpret_cast<otl_value<char*>*>( iter->m_pvParam );
          LOG_D( "%s: %s", __FUNCTION__, reinterpret_cast<otl_value<char*>*>( iter->m_pvParam )->v );
          break;
        case m_eSQLParamType_OTLDateTime:
          coStream << * reinterpret_cast<otl_value<otl_datetime>*>( iter->m_pvParam );
          LOG_D( "%s: datetime", __FUNCTION__ );
          break;
        default:
          LOG_D( "%s: unsupported parameter type: %u", iter->m_eParamType );
      }
    }
    coStream.check_end_of_row();
    p_pcoDBConn->commit();
    LOG_D( "commited: %u", coStream.get_rpc() );
    stat_measure( g_psoSQLQueueStat, "operated", &coTM );
#ifdef _DEBUG
    stat_measure( g_psoSQLQueueStat, p_psoSQLReqInfo->m_pszReqName, &coTM );
#endif
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    pcrf_sql_queue_dump_request( coExcept.stm_text, p_psoSQLReqInfo );
    iRetVal = coExcept.code;
    p_pcoDBConn->rollback();
    stat_measure( g_psoSQLQueueStat, "failed", &coTM );
  }

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}

void pcrf_sql_queue_clean_single( SSQLRequestInfo *p_psoSQLReqInfo )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  if ( NULL != p_psoSQLReqInfo && NULL != p_psoSQLReqInfo->m_plistParamList ) {
  } else {
    return;
  }

  std::list<SSQLQueueParam>::iterator iter = p_psoSQLReqInfo->m_plistParamList->begin();
  for ( ; iter != p_psoSQLReqInfo->m_plistParamList->end(); ++iter ) {
    if ( NULL != iter->m_pvParam ) {
      switch ( iter->m_eParamType ) {
        case m_eSQLParamType_Invalid:
          break;
        case m_eSQLParamType_Int:
          delete reinterpret_cast<otl_value<int>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_UInt:
          delete reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_StdString:
          delete reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_Char:
          delete reinterpret_cast<otl_value<char*>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_OTLDateTime:
          delete reinterpret_cast<otl_value<otl_datetime>*>( iter->m_pvParam );
          break;
        default:
          UTL_LOG_D( *g_pcoLog, "unsupported parameter type: '%u'", iter->m_eParamType );
          break;
      }
      iter->m_eParamType = m_eSQLParamType_Invalid;
      iter->m_pvParam = NULL;
    }
  }

  delete p_psoSQLReqInfo->m_plistParamList;
  p_psoSQLReqInfo->m_plistParamList = NULL;

  LOG_D( "leave: %s", __FUNCTION__ );
}

static void * pcrf_sql_queue_oper(void *p_pvArg )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  timespec soTS;
  SSQLQueue *psoSQLQueue = reinterpret_cast<SSQLQueue*>( p_pvArg );
  int iFnRes;
  int iRestoreConnection = 0;
  otl_connect *pcoDBConn = NULL;
  pthread_mutex_t mutexSQLQueueTimer;

  CHECK_FCT_DO( pthread_mutex_init( &mutexSQLQueueTimer, NULL ), pthread_exit( NULL ) );
  CHECK_FCT_DO( pthread_mutex_lock( &mutexSQLQueueTimer ), pthread_mutex_destroy( &mutexSQLQueueTimer ); pthread_exit( NULL ) );

  while ( g_iWork ) {
    /* ждем некоторое время */
    CHECK_FCT_DO( pcrf_make_timespec_timeout( soTS, 0, 100000 ), goto clean_and_exit );
    iFnRes = pthread_mutex_timedlock( &mutexSQLQueueTimer, &soTS );
    /* проверка указателя на наличие объекта подключения к БД */
    if ( NULL != pcoDBConn ) {
      /* проверим, надо ли восстанавливать подключение */
      if ( -1 == iRestoreConnection ) {
        /* пытаемся восстановить подключение */
        if ( -1 == (iRestoreConnection = pcrf_db_pool_restore( pcoDBConn ) ) ) {
          /* если восстановить подключение не удалось */
          continue;
        }
      }
    } else {
      /* запрашиваем подключение из пула если это необходимо */
      if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 10 * USEC_PER_SEC ) && NULL != pcoDBConn ) {
      } else {
        /* не удалось получить подключение из пула, повторим попытку в следующей итерации */
        continue;
      }
    }
    if (ETIMEDOUT == iFnRes && 0 != g_iWork) {
      std::list<SSQLRequestInfo>::iterator iter = psoSQLQueue->m_listSQLQueue.begin();
      while ( iter != psoSQLQueue->m_listSQLQueue.end() ) {
        if ( 0 == pcrf_sql_queue_oper_single( pcoDBConn, &( *iter ) ) ) {
        } else {
          /* при обработке возникла ошибка,
           * если она связана с подключением к БД, то будет предпринята попытка восстановления соединения 
           * если проблем с подключением к БД не было, то обработка очереди продолжится
           * если подключение к БД было неработоспособно и было  (или не было) восстановлено,
           * то операция будет выполнена повторно
           */
          if ( 0 == ( iRestoreConnection = pcrf_db_pool_restore( pcoDBConn ) )) {
          } else {
            break;
          }
        }
        pcrf_sql_queue_clean_single( &( *iter ) );
        pthread_mutex_lock( &( psoSQLQueue->m_mutexSQLQueue ) );
        iter = psoSQLQueue->m_listSQLQueue.erase( iter );
        pthread_mutex_unlock( &( psoSQLQueue->m_mutexSQLQueue ) );
      }
    } else {
      goto clean_and_exit;
    }
  }

clean_and_exit:
  pthread_mutex_unlock( &mutexSQLQueueTimer );
  pthread_mutex_destroy( &mutexSQLQueueTimer );

  if(NULL != pcoDBConn) {
    pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
  }

  LOG_D( "leave: %s", __FUNCTION__ );

  pthread_exit(NULL);
}

void pcrf_sql_queue_enqueue( const char *p_pszSQLRequest, std::list<SSQLQueueParam> *p_plistParameters, const char *p_pszReqName, std::string *p_pstrSessionId )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  CTimeMeasurer coTM;
  static unsigned uiRoundIndex = 0;
  unsigned uiThreadIndex;

  if ( NULL != p_pstrSessionId ) {
    size_t stInd;

    stInd = p_pstrSessionId->rfind( ';' );
    if ( stInd != std::string::npos ) {
      std::string strSubstr;

      strSubstr = p_pstrSessionId->substr( ++stInd );
      LOG_D( "%s: substring: %s", __FUNCTION__, strSubstr.c_str() );
      uiThreadIndex = strtol( strSubstr.c_str(), NULL, 10 ) % g_uiQueueCount;
    } else {
      uiRoundIndex %= g_uiQueueCount;
      uiThreadIndex = uiRoundIndex++;
    }
  } else {
    uiRoundIndex %= g_uiQueueCount;
    uiThreadIndex = uiRoundIndex++;
  }

  SSQLRequestInfo soSQLQueue( p_pszSQLRequest, p_plistParameters, p_pszReqName );

  LOG_D( "%s: thread index: %u", __FUNCTION__, uiThreadIndex );

  pthread_mutex_lock( &( g_pmsoSQLQueue[ uiThreadIndex ].m_mutexSQLQueue) );
  g_pmsoSQLQueue[ uiThreadIndex ].m_listSQLQueue.push_back( soSQLQueue );
  pthread_mutex_unlock( &( g_pmsoSQLQueue[ uiThreadIndex ].m_mutexSQLQueue) );

  stat_measure( g_psoSQLQueueStat, "enqueued", &coTM );

  LOG_D( "leave: %s", __FUNCTION__ );
}
