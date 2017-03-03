#include "app_pcrf.h"
#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

static SStat *g_psoSQLQueueStat;

/* мьютекс обработки очереди */
static pthread_mutex_t g_mutexSQLQueueTimer;
static volatile int g_iWork;
/* функция обработки очереди запросов */
static pthread_t g_threadSQLQueue;
static void * pcrf_sql_queue_oper(void *);
/* мьютекс доступа к очереди запросов */
static pthread_mutex_t g_mutexSQLQueue;

struct SSQLQueue
{
  std::string *m_pstrRequest;
  std::list<SSQLQueueParam> *m_plistParamList;
  SSQLQueue(std::string *p_pstrSQLReq, std::list<SSQLQueueParam> *p_plistParam) : m_pstrRequest(p_pstrSQLReq), m_plistParamList(p_plistParam) {}
};
static std::list<SSQLQueue> g_listSQLQueue;

int pcrf_sql_queue_init()
{
  g_psoSQLQueueStat = stat_get_branch( "sql queue" );
  CHECK_FCT(pthread_mutex_init(&g_mutexSQLQueue, NULL));
  CHECK_FCT(pthread_mutex_init(&g_mutexSQLQueueTimer, NULL));
  CHECK_FCT(pthread_mutex_lock(&g_mutexSQLQueueTimer));
  g_iWork = 1;
  CHECK_FCT(pthread_create(&g_threadSQLQueue, NULL, pcrf_sql_queue_oper, NULL));

  return 0;
}

void pcrf_sql_queue_fini()
{
  g_iWork = 0;
  pthread_mutex_unlock(&g_mutexSQLQueueTimer);
  pthread_mutex_destroy(&g_mutexSQLQueueTimer);
  pthread_mutex_destroy(&g_mutexSQLQueue);
  pthread_join(g_threadSQLQueue, NULL);
}

static int pcrf_sql_queue_oper_single( otl_connect *p_pcoDBConn, SSQLQueue *p_psoSQLQueue)
{
  /* проверка параметров */
  if ( NULL != p_pcoDBConn && NULL != p_psoSQLQueue && NULL != p_psoSQLQueue->m_pstrRequest && NULL != p_psoSQLQueue->m_plistParamList ) {
  } else {
    return EINVAL;
  }

  CTimeMeasurer coTM;
  int iRetVal = 0;

  try {
    otl_nocommit_stream coStream;

    /* открываем запрос */
    coStream.open( 1, p_psoSQLQueue->m_pstrRequest->c_str(), *p_pcoDBConn );
    /* передаем параметры зароса */
    std::list<SSQLQueueParam>::iterator iter = p_psoSQLQueue->m_plistParamList->begin();
    for ( ; iter != p_psoSQLQueue->m_plistParamList->end(); ++iter ) {
      if ( NULL != iter->m_pvParam ) {
      } else {
        iRetVal = EINVAL;
      }
      switch ( iter->m_eParamType ) {
        case m_eSQLParamType_Int:
          coStream << * reinterpret_cast<otl_value<int>*>(iter->m_pvParam);
          break;
        case m_eSQLParamType_UInt:
          coStream << * reinterpret_cast<otl_value<unsigned>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_StdString:
          coStream << * reinterpret_cast<otl_value<std::string>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_Char:
          coStream << * reinterpret_cast<otl_value<char*>*>( iter->m_pvParam );
          break;
        case m_eSQLParamType_OTLDateTime:
          coStream << * reinterpret_cast<otl_value<otl_datetime>*>( iter->m_pvParam );
          break;
      }
    }
    p_pcoDBConn->commit();

    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    iRetVal = coExcept.code;
  }

  stat_measure( g_psoSQLQueueStat, "operated", &coTM );

  return iRetVal;
}

void pcrf_sql_queue_clean_single( SSQLQueue *p_psoSQLQueue )
{
  if ( NULL != p_psoSQLQueue && NULL != p_psoSQLQueue->m_pstrRequest ) {
  } else {
    return;
  }

  delete p_psoSQLQueue->m_pstrRequest;
  p_psoSQLQueue->m_pstrRequest = NULL;

  std::list<SSQLQueueParam>::iterator iter = p_psoSQLQueue->m_plistParamList->begin();
  while ( iter != p_psoSQLQueue->m_plistParamList->end() ) {
    if ( NULL != iter->m_pvParam ) {
      switch ( iter->m_eParamType ) {
        case m_eSQLParamType_Invalid:
          break;
        case m_eSQLParamType_Int:
          delete reinterpret_cast<otl_value<int>*>(iter->m_pvParam);
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
    iter = p_psoSQLQueue->m_plistParamList->erase( iter );
  }
}

static void * pcrf_sql_queue_oper(void *)
{
  timespec soTS;
  int iFnRes;
  otl_connect *pcoDBConn;

  while (g_iWork) {
    CHECK_FCT_DO(pcrf_make_timespec_timeout(soTS, 1000), goto clean_and_exit);
    iFnRes = pthread_mutex_timedlock(&g_mutexSQLQueueTimer, &soTS);
    if (ETIMEDOUT == iFnRes && 0 != g_iWork) {
      if ( ! g_listSQLQueue.empty() ) {
        /* запрашиваем подключение к БД */
        if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 0 ) && NULL != pcoDBConn ) {
        } else {
          continue;
        }
        std::list<SSQLQueue>::iterator iter = g_listSQLQueue.begin();
        while ( iter != g_listSQLQueue.end() ) {
          pcrf_sql_queue_oper_single( pcoDBConn, &(*iter) );
          pcrf_sql_queue_clean_single( &(*iter) );
          pthread_mutex_lock( &g_mutexSQLQueue );
          iter = g_listSQLQueue.erase( iter );
          pthread_mutex_unlock( &g_mutexSQLQueue );
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

void pcrf_sql_queue_enqueue( std::string *p_pstrRequest, std::list<SSQLQueueParam> *p_plistParameters )
{
  if (NULL != p_pstrRequest) {
  } else {
    return;
  }

  CTimeMeasurer coTM;

  SSQLQueue soSQLQueue(p_pstrRequest, p_plistParameters);

  pthread_mutex_lock( &g_mutexSQLQueue );
  g_listSQLQueue.push_back( soSQLQueue );
  pthread_mutex_unlock( &g_mutexSQLQueue );

  stat_measure( g_psoSQLQueueStat, "enqueued", &coTM );
}

void pcrf_sql_queue_add_param( std::list<SSQLQueueParam> *p_plistParameters, void *p_pvParam, ESQLParamType p_eSQLParamType )
{
  SSQLQueueParam soParam( p_eSQLParamType, p_pvParam );
  p_plistParameters->push_back( soParam );
}
