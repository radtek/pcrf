#include "app_pcrf_header.h"
#include "utils/timemeasurer/timemeasurer.h"

#include <unordered_map>
#include <poll.h>

static void * pcrf_stat_send_stat( void * p_pvArg );

/* структура для хранения данных статистики одной метрики */
struct SPCRFStatElement {
  uint64_t          m_ui64Counter;    /* счетчик количества */
  uint64_t          m_ui64USec;       /* счетчик продолжительности */
  bool              m_bIsInitialized; /* признак успешной инициализации */
  pthread_mutex_t   m_mutexLock;      /* блокировка доступа к элементу */
  /* конструктор по умолчанию */
  SPCRFStatElement() : m_ui64Counter( 0 ), m_ui64USec( 0 ), m_bIsInitialized( false )
  {
    if ( 0 == pthread_mutex_init( &m_mutexLock, NULL ) ) {
      m_bIsInitialized = true;
    }
  }
  /* деструктор */
  ~SPCRFStatElement()
  {
    if ( m_bIsInitialized ) {
      pthread_mutex_destroy( &m_mutexLock );
    }
  }
  /* устанавливает новое значение */
  void Add( const uint64_t &p_ui64USec )
  {
    ++m_ui64Counter;
    m_ui64USec += p_ui64USec;
  }
};

/* структура для управления всеми элементами статистики */
struct SPCRFStat {
  volatile bool     m_bIsInitialized;  /* признак успешной инициализации */
  volatile bool     m_bListIsUpdated;  /* признак изменения состава ключей */
  std::unordered_map<std::string, SPCRFStatElement*> m_mapPCRFStat;  /* хранилище для метрик */
  pthread_rwlock_t  m_rwLock;          /* блокировка по чтению/записи */
  pthread_mutex_t   m_mutexStatTimer;  /* мьютекс-таймер отправки статистики */
  pthread_t         m_threadSendStat;  /* дескриптор потока передачи данных в zabbix */
  SPCRFStat() : m_bIsInitialized( false ), m_bListIsUpdated( false )
  {
    if ( 0 == pthread_rwlock_init( &m_rwLock, NULL ) ) {
      m_bIsInitialized = true;
    }
    if ( 0 == pthread_mutex_init( &m_mutexStatTimer, NULL ) ) {
      if ( 0 == pthread_mutex_lock( &m_mutexStatTimer ) ) {
      } else {
        pthread_mutex_destroy( &m_mutexStatTimer );
        pthread_rwlock_destroy( &m_rwLock );
        m_bIsInitialized = false;
      }
    } else {
      pthread_rwlock_destroy( &m_rwLock );
      m_bIsInitialized = false;
    }
    if ( 0 == pthread_create( &m_threadSendStat, NULL, pcrf_stat_send_stat, NULL ) ) {
      m_bIsInitialized = true;
    }
  }
  ~SPCRFStat()
  {
    if ( m_bIsInitialized ) {
      pthread_rwlock_destroy( &m_rwLock );
    }
  }
};

static SPCRFStat g_soPCRFStat;

extern "C"
int pcrf_stat_init()
{
  if ( g_soPCRFStat.m_bIsInitialized ) {
    LOG_N( "STAT module is initialized successfully" );
  } else {
    return 1;
  }

  return 0;
}

extern "C"
void pcrf_stat_fini()
{
  pthread_mutex_unlock( &g_soPCRFStat.m_mutexStatTimer );

  LOG_N( "STAT module is stopped successfully" );
}

void pcrf_stat_add( const char *p_pszMetricsName, const char *p_pszKey, const uint64_t &p_ui64USec, const char *p_pszParameterName, const char *p_pszParameterValue )
{
  /* итератор для поиска ключа */
  std::unordered_map<std::string, SPCRFStatElement*>::iterator iter;
  int iFnRes;

  /* формируем ключ с учетом параметра */
  char mcKey[ 512 ];
  const char *pszKey;

  if ( NULL != p_pszParameterValue ) {
    iFnRes = snprintf( mcKey, sizeof( mcKey ), p_pszMetricsName, p_pszParameterValue );
    if ( 0 < iFnRes ) {
      if ( iFnRes < sizeof( mcKey ) ) {
        pszKey = mcKey;
      } else {
        return;
      }
    } else {
      return;
    }
  } else {
    pszKey = p_pszMetricsName;
  }

  /* включаем блокировку по чтению */
  CHECK_FCT_DO( pthread_rwlock_rdlock( &g_soPCRFStat.m_rwLock ), return );

  /* ищем заданный в параметрах процедуры ключ */
  iter = g_soPCRFStat.m_mapPCRFStat.find( pszKey );

  /* если нужный ключ найден */
  if ( iter != g_soPCRFStat.m_mapPCRFStat.end() ) {
    /* блокируем элемент */
    CHECK_FCT_DO( pthread_mutex_lock( &iter->second->m_mutexLock ), goto unlock_and_exit );

    /* обрабатываем данные */
    iter->second->Add( p_ui64USec );

    /* снимаем блокировку элемента */
    CHECK_FCT_DO( pthread_mutex_unlock( &iter->second->m_mutexLock ), goto unlock_and_exit );

    /* все что надо было сделали - уходим */
    goto unlock_and_exit;
  }

  /* снимаем блокировку по чтению */
  CHECK_FCT_DO( pthread_rwlock_unlock( &g_soPCRFStat.m_rwLock ), return );

  /* устанавливаем блокировку на запись */
  CHECK_FCT_DO( pthread_rwlock_wrlock( &g_soPCRFStat.m_rwLock ), return );

  {
    /* добавляем новый элемент */
    std::pair<std::unordered_map<std::string, SPCRFStatElement*>::iterator, bool> pairResult;
    SPCRFStatElement *psoStatElement = new SPCRFStatElement();
    pairResult = g_soPCRFStat.m_mapPCRFStat.insert( std::pair<std::string, SPCRFStatElement*>( pszKey, psoStatElement ) );

    /* проверяем результат */
    if ( !pairResult.second ) {
      /* если нас кто-то успел таки опередить */
      /* удаляем невостребованный объект */
      delete psoStatElement;
    } else {
      pcrf_zabbix_set_parameter( "test_host", p_pszKey, p_pszParameterName, p_pszParameterValue );
    }
    /* обновляем данные */
    pairResult.first->second->Add( p_ui64USec );
  }


  unlock_and_exit:

  /* снимаем блокировку */
  CHECK_FCT_DO( pthread_rwlock_unlock( &g_soPCRFStat.m_rwLock ), /* nothing to do*/ );
}

static void * pcrf_stat_send_stat( void * p_pvArg )
{
  timespec soTimeSpec;
  std::unordered_map<std::string, SPCRFStatElement*>::iterator iterStat;
  time_t tmTime;

  CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 60, 0 ), goto clean_and_exit );

  while ( ETIMEDOUT == pthread_mutex_timedlock( &g_soPCRFStat.m_mutexStatTimer, &soTimeSpec ) ) {
    CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 60, 0 ), goto clean_and_exit );

    tmTime = time( NULL );

    /* включаем блокировку по чтению */
    CHECK_FCT_DO( pthread_rwlock_rdlock( &g_soPCRFStat.m_rwLock ), goto clean_and_exit );
    for ( iterStat = g_soPCRFStat.m_mapPCRFStat.begin(); iterStat != g_soPCRFStat.m_mapPCRFStat.end(); ++iterStat ) {
      pcrf_zabbix_enqueue_data( "test_host", iterStat->first.c_str(), iterStat->second->m_ui64Counter, tmTime );
    }
    /* снимаем блокировку */
    CHECK_FCT_DO( pthread_rwlock_unlock( &g_soPCRFStat.m_rwLock ), /* nothing to do*/ );
  }

  clean_and_exit:

  pthread_exit( NULL );
}
