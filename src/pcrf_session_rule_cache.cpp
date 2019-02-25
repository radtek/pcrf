#include <list>

#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/ps_common.h"
#include "pcrf_session_cache.h"
#include "pcrf_ipc.h"

extern CLog *g_pcoLog;

static volatile bool g_bSessionRuleCacheWork = true;

/* хранилище информации о правилах сессий */
static std::map<std::string, std::list<std::string> > g_mapSessRuleLst;
/* мьютекс доступа к хранилищу */
static pthread_mutex_t g_mutexSessRuleLst;

/* функция потока загрузки наального списка правил сессиий */
static void * pcrf_session_rule_cache_load_list( void * );

/* указатель на объект статистики кэша правил сессий */
static SStat *g_psoSessionRuleCacheStat;

static void pcrf_session_rule_cache_provide_stat_cb( char **p_ppszStat );

int pcrf_session_rule_list_init( pthread_t *p_ptThread )
{
  /* запрашиваем адрес объекта статистики кэша правил сессий */
  g_psoSessionRuleCacheStat = stat_get_branch("session rule cache");

  /* инициализация мьютексов доступа к хранилищу кэша правил сессий */
  CHECK_FCT( pthread_mutex_init( &g_mutexSessRuleLst, NULL ) );

  /* загружаем первичный список правил сессий */
  CHECK_FCT( pthread_create( p_ptThread, NULL, pcrf_session_rule_cache_load_list, NULL ) );

  stat_register_cb( pcrf_session_rule_cache_provide_stat_cb );

  stat_register_cb( pcrf_session_rule_cache_provide_stat_cb );

  UTL_LOG_N( *g_pcoLog,
    "session rule cache is initialized successfully!\n"
    "\tstorage capasity is '%u' records",
    g_mapSessRuleLst.max_size() );

  return 0;
}

void pcrf_session_rule_list_fini()
{
  g_bSessionRuleCacheWork = false;

  /* освобождаем занятые ресурсы */
  pthread_mutex_destroy( &g_mutexSessRuleLst );
}

int pcrf_session_rule_cache_get( const std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectActive )
{
  CTimeMeasurer coTM;
  int iRetVal = 0;
  SDBAbonRule soRule(true,false);
  std::map<std::string, std::list<std::string> >::iterator iter;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessRuleLst ), goto cleanup_and_exit );

  /* ищем сессию */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  /* если сессия найдена */
  if (iter != g_mapSessRuleLst.end() && 0 < iter->second.size()) {
    /* обходим все активные правила */
    for (std::list<std::string>::iterator iterLst = iter->second.begin(); iterLst != iter->second.end(); ++iterLst) {
      soRule.m_strRuleName = *iterLst;
      /* сохраняем правилов в списке */
      p_vectActive.push_back(soRule);
    }
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
    iRetVal = ENODATA;
  }

  cleanup_and_exit:
  /* освобождаем хранилище */
  pthread_mutex_unlock( &g_mutexSessRuleLst );

  if (0 == iRetVal) {
    /* фиксируем в статистике успешное завершение */
    stat_measure(g_psoSessionRuleCacheStat, "hit", &coTM);
  } else {
    /* фиксируем в статистике промах */
    stat_measure(g_psoSessionRuleCacheStat, "miss", &coTM);
  }

  return iRetVal;
}

void pcrf_session_rule_cache_insert_local( const std::string &p_strSessionId, const std::string &p_strRuleName )
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;

  /* блокируем общий мьютекс доступа к хранилищу */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessRuleLst ), goto cleanup_and_exit );

  /* ищем сессию в хранилище */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    /* если нашли, то дополняем правил список ее */
    iter->second.push_back( p_strRuleName );
    stat_measure(g_psoSessionRuleCacheStat, "rule inserted", &coTM);
  } else {
    /* если сессия не найдена добавляем ее в хранилище */
    std::list<std::string> list;
    list.push_back( p_strRuleName );
    g_mapSessRuleLst.insert( std::pair<std::string, std::list<std::string> >( p_strSessionId, list ) );
    stat_measure(g_psoSessionRuleCacheStat, "session inserted", &coTM);
  }

cleanup_and_exit:
  pthread_mutex_unlock( &g_mutexSessRuleLst );

  return;
}

void pcrf_session_rule_cache_remove_sess_local( const std::string &p_strSessionId )
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;

  /* блокируем доступ к хранилищу кэша правил сессий */
  CHECK_FCT_DO(pthread_mutex_lock( &g_mutexSessRuleLst ), goto cleanup_and_exit);

  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    g_mapSessRuleLst.erase(iter);
    stat_measure(g_psoSessionRuleCacheStat, "session removed", &coTM);
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
  }

  cleanup_and_exit:
  /* освобождаем хранилище */
  pthread_mutex_unlock( &g_mutexSessRuleLst );

  return;
}

void pcrf_session_rule_cache_remove_rule_local( const std::string &p_strSessionId, const std::string &p_strRuleName )
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;
  std::list<std::string>::iterator iterLst;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessRuleLst ), goto cleanup_and_exit );

  /* ищем сессию */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  /* если сессия найдена */
  if (iter != g_mapSessRuleLst.end()) {
    /* обходим все правила */
    for (iterLst = iter->second.begin(); iterLst != iter->second.end(); ) {
      if ( 0 == iterLst->compare( p_strRuleName ) ) {
        /* и удаляем найденное */
        iterLst = iter->second.erase( iterLst );
        /* фиксируем в модуле статистики успешное удаление правила */
        stat_measure( g_psoSessionRuleCacheStat, "rule removed", &coTM );
      } else {
        ++iterLst;
      }
    }
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
  }

  cleanup_and_exit:
  pthread_mutex_unlock( &g_mutexSessRuleLst );

  return;
}

static void * pcrf_session_rule_cache_load_list( void * )
{
  pthread_exit( 0 );

  int iRetVal = 0;
  CTimeMeasurer coTM;
  int iRepeat = 1;
  otl_connect *pcoDBConn;
  std::string strSessionId;
  std::string strRuleName;
  char mcTime[64];

  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
  } else {
    goto clean_and_exit;
  }

  sql_repeat:

  try {
    otl_nocommit_stream coStream;

    coStream.open (
      1000,
      "select session_id, rule_name from ps.sessionRule where time_end is null",
      *pcoDBConn );

    std::vector<SDBAbonRule> vectAbonRules;

    while ( 0 == coStream.eof() && g_bSessionRuleCacheWork ) {
      coStream
        >> strSessionId
        >> strRuleName;
      pcrf_session_rule_cache_insert_local( strSessionId, strRuleName );
    }
    coTM.GetDifference(NULL, mcTime, sizeof(mcTime));
    coStream.close();
    UTL_LOG_N( *g_pcoLog, "session rule list is loaded in '%s'; session rule count: '%u'", mcTime, g_mapSessRuleLst.size() );
  } catch (otl_exception &coExcept) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

clean_and_exit:
  if ( NULL != pcoDBConn ) {
    pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
  }

  int *piRetVal = reinterpret_cast< int* >( malloc( sizeof( int ) ) );

  *piRetVal = iRetVal;

  pthread_exit( piRetVal );
}

void pcrf_session_rule_cache_insert( const std::string &p_strSessionId, const std::string &p_strRuleName )
{
  /* проверяем параметры */
  if ( 0 < p_strSessionId.length() && 0 < p_strRuleName.length() ) {
  } else {
    LOG_D( "%s: session-id: %s; rule-name: %s", __FUNCTION__, p_strSessionId.c_str(), p_strRuleName.c_str() );
    return;
  }

  pcrf_session_rule_cache_insert_local( p_strSessionId, p_strRuleName );
  pcrf_ipc_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_INSERT_SESSRUL), &p_strRuleName );
}

void pcrf_session_rule_cache_remove_rule( const std::string &p_strSessionId, const std::string &p_strRuleName )
{
  /* проверяем параметры */
  if ( 0 < p_strSessionId.length() && 0 < p_strRuleName.length() ) {
  } else {
    LOG_D( "%s: session-id: %s; rule-name: %s", __FUNCTION__, p_strSessionId.c_str(), p_strRuleName.c_str() );
    return;
  }

  pcrf_session_rule_cache_remove_rule_local(p_strSessionId, p_strRuleName);
  pcrf_ipc_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSRUL), &p_strRuleName);
}

static void pcrf_session_rule_cache_provide_stat_cb( char **p_ppszStat )
{
  int iFnRes;

  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessRuleLst ), return );
  iFnRes = asprintf( p_ppszStat, "session rule cache has %u members", g_mapSessRuleLst.size() );
  if ( 0 < iFnRes ) {
  } else {
    *p_ppszStat = NULL;
  }
  pthread_mutex_unlock( &g_mutexSessRuleLst );
}
