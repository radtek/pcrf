#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

/* указатель на объект статистики */
static SStat *g_psoSQLQueueStat;

/* данные SQL запроса */
struct SSQLQueue
{
  const char *m_pszRequest;
  std::list<SSQLQueueParam> *m_plistParamList;
  const char *m_pszReqName;
  SSQLQueue(
    const char *p_pszSQLReq,
    std::list<SSQLQueueParam> *p_plistParam,
    const char *p_pszReqName )
    : m_pszRequest( p_pszSQLReq ), m_plistParamList( p_plistParam ), m_pszReqName( p_pszReqName )
  {
    std::list<SSQLQueueParam>::iterator iter = m_plistParamList->begin();
    while ( iter != m_plistParamList->end() ) {
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
            delete reinterpret_cast<otl_value<char>*>( iter->m_pvParam );
            break;
          case m_eSQLParamType_OTLDateTime:
            delete reinterpret_cast<otl_value<otl_datetime>*>( iter->m_pvParam );
            break;
        }
        iter->m_eParamType = m_eSQLParamType_Invalid;
        iter->m_pvParam = NULL;
      }
      iter = m_plistParamList->erase( iter );
    }
  }
};

struct SSQLQueueData {
  pthread_mutex_t      m_mutexSQLQueueTimer;   /* мьютекс обработки очереди */
  pthread_t            m_threadSQLQueue;       /* функция обработки очереди запросов */
  pthread_mutex_t      m_mutexSQLQueue;        /* мьютекс доступа к очереди запросов */
  std::list<SSQLQueue> m_listSQLQueue;         /* очередь запросов */
  std::map<std::string, otl_stream*> m_mapOTLStream; /* кэшированные запросы */
};

static unsigned int   g_uiQueueCount;          /* колпчество очередей */
static SSQLQueueData *g_pmsoSQLQueue;          /* указатель на массив очередей */

static volatile int g_iWork;
static void * pcrf_sql_queue_oper(void *);

int pcrf_sql_queue_init()
{
  /* инициализируем ветку модуля статистики */
  g_psoSQLQueueStat = stat_get_branch( "sql queue" );

  /* взводим флаг продолжения работы */
  g_iWork = 1;

  /* вычисляем количество очередей */
  g_uiQueueCount = ( 0 == g_psoConf->m_iDBPoolSize ) ? 1 : g_psoConf->m_iDBPoolSize / 2;
  if ( 1 <= g_uiQueueCount ) {
  } else {
    g_uiQueueCount = 1;
  }

  /* создаем массив очередей */
  g_pmsoSQLQueue = new SSQLQueueData[ g_uiQueueCount ];

  /* инициализируем очереди */
  for ( unsigned int i = 0; i < g_uiQueueCount; ++i ) {
    CHECK_FCT( pthread_mutex_init( &g_pmsoSQLQueue[ i ].m_mutexSQLQueue, NULL ) );
    CHECK_FCT( pthread_mutex_init( &g_pmsoSQLQueue[ i ].m_mutexSQLQueueTimer, NULL ) );
    CHECK_FCT( pthread_mutex_lock( &g_pmsoSQLQueue[ i ].m_mutexSQLQueueTimer ) );
    CHECK_FCT( pthread_create( &g_pmsoSQLQueue[ i ].m_threadSQLQueue, NULL, pcrf_sql_queue_oper, &g_pmsoSQLQueue[ i ] ) );
  }

  return 0;
}

void pcrf_sql_queue_fini()
{
  g_iWork = 0;

  if ( NULL != g_pmsoSQLQueue ) {
    for ( unsigned int i = 0; i < g_uiQueueCount; ++i ) {
      pthread_mutex_unlock( &g_pmsoSQLQueue[ i ].m_mutexSQLQueueTimer );
      pthread_mutex_destroy( &g_pmsoSQLQueue[ i ].m_mutexSQLQueueTimer );
      pthread_mutex_destroy( &g_pmsoSQLQueue[ i ].m_mutexSQLQueue );
      pthread_join( g_pmsoSQLQueue[ i ].m_threadSQLQueue, NULL );
      /* освобождаемся от OTL потоков */
      for ( std::map<std::string, otl_stream*>::iterator iter = g_pmsoSQLQueue[ i ].m_mapOTLStream.begin(); iter != g_pmsoSQLQueue[ i ].m_mapOTLStream.end(); ++iter ) {
        iter->second->close();
        delete iter->second;
      }
    }
    delete[ ] g_pmsoSQLQueue;
    g_pmsoSQLQueue = NULL;
  }
}

static int pcrf_sql_queue_oper_single( otl_connect *p_pcoDBConn, SSQLQueue *p_psoSQLQueue, std::map<std::string, otl_stream*> &p_mapOTLStream )
{
  /* проверка параметров */
  if ( NULL != p_pcoDBConn && NULL != p_psoSQLQueue && NULL != p_psoSQLQueue->m_pszRequest && NULL != p_psoSQLQueue->m_plistParamList ) {
  } else {
    return EINVAL;
  }

  CTimeMeasurer coTM;
  int iRetVal = 0;

  try {
    otl_stream *pcoStream;
    std::map<std::string, otl_stream*>::iterator iterStream;

    iterStream = p_mapOTLStream.find( p_psoSQLQueue->m_pszReqName );
    if ( iterStream != p_mapOTLStream.end() ) {
      pcoStream = &(*iterStream->second);
    } else {
      pcoStream = new otl_stream;

      pcoStream->set_commit( 0 );
      pcoStream->open( 1, p_psoSQLQueue->m_pszRequest, *p_pcoDBConn, NULL, p_psoSQLQueue->m_pszReqName );

      p_mapOTLStream.insert( std::pair<std::string, otl_stream*>( p_psoSQLQueue->m_pszReqName, pcoStream ) );
    }

    /* передаем параметры зароса */
    std::list<SSQLQueueParam>::iterator iter = p_psoSQLQueue->m_plistParamList->begin();
    for ( ; iter != p_psoSQLQueue->m_plistParamList->end(); ++iter ) {
      if ( NULL != iter->m_pvParam ) {
      } else {
        iRetVal = EINVAL;
      }
      switch ( iter->m_eParamType ) {
        case m_eSQLParamType_Int:
          *pcoStream << * reinterpret_cast<otl_value<int>*>(iter->m_pvParam);
          break;
        case m_eSQLParamType_UInt:
          *pcoStream << * reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_StdString:
          *pcoStream << * reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_Char:
          *pcoStream << * reinterpret_cast<otl_value<char*>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_OTLDateTime:
          *pcoStream << * reinterpret_cast<otl_value<otl_datetime>*>( iter->m_pvParam );
          break;
      }
    }
    p_pcoDBConn->commit();
  } catch ( otl_exception &coExcept ) {
    stat_measure( g_psoSQLQueueStat, "failed", &coTM );
    UTL_LOG_E( *g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoSQLQueueStat, p_psoSQLQueue->m_pszReqName, &coTM );

  return iRetVal;
}

static void * pcrf_sql_queue_oper(void *p_vParam)
{
  SSQLQueueData *psoSQLQueueData = reinterpret_cast<SSQLQueueData*>( p_vParam );
  timespec soTS;
  int iFnRes;
  otl_connect *pcoDBConn;

  while (g_iWork) {
    CHECK_FCT_DO(pcrf_make_timespec_timeout(soTS, 1000), goto clean_and_exit);
    iFnRes = pthread_mutex_timedlock(&psoSQLQueueData->m_mutexSQLQueueTimer, &soTS);
    if (ETIMEDOUT == iFnRes && 0 != g_iWork) {
      if ( ! psoSQLQueueData->m_listSQLQueue.empty() ) {
        /* запрашиваем подключение к БД */
        if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 0 ) && NULL != pcoDBConn ) {
        } else {
          continue;
        }
        std::list<SSQLQueue>::iterator iter = psoSQLQueueData->m_listSQLQueue.begin();
        while ( iter != psoSQLQueueData->m_listSQLQueue.end() ) {
          pcrf_sql_queue_oper_single( pcoDBConn, &(*iter), psoSQLQueueData->m_mapOTLStream );
          pthread_mutex_lock( &psoSQLQueueData->m_mutexSQLQueue );
          iter = psoSQLQueueData->m_listSQLQueue.erase( iter );
          pthread_mutex_unlock( &psoSQLQueueData->m_mutexSQLQueue );
        }
        pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
      }
    } else {
      goto clean_and_exit;
    }
  }

clean_and_exit:

  pthread_exit(NULL);
}

void pcrf_sql_queue_enqueue( const char *p_pszSQLRequest, std::list<SSQLQueueParam> *p_plistParameters, const char *p_pszReqName, std::string *p_pstrSessionId )
{
  CTimeMeasurer coTM;
  unsigned int uiThreadNum;
  static unsigned int uiSlide = 0;

  if ( NULL != p_pstrSessionId ) {
    uiThreadNum = uiSlide++ % g_uiQueueCount;
  } else {
    uiThreadNum = uiSlide++ % g_uiQueueCount;
  }

  SSQLQueue soSQLQueue( p_pszSQLRequest, p_plistParameters, p_pszReqName );

  pthread_mutex_lock( &g_pmsoSQLQueue[ uiThreadNum ].m_mutexSQLQueue );
  LOG_D( "sql queue thread number: %u", uiThreadNum );
  g_pmsoSQLQueue[uiThreadNum].m_listSQLQueue.push_back( soSQLQueue );
  pthread_mutex_unlock( &g_pmsoSQLQueue[ uiThreadNum ].m_mutexSQLQueue );

  stat_measure( g_psoSQLQueueStat, "enqueued", &coTM );
}
