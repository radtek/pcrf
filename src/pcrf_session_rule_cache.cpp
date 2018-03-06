#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/ps_common.h"
#include "pcrf_lock.h"

extern CLog *g_pcoLog;

static volatile bool g_bSessionRuleCacheWork = true;

#include <list>

/* хранилище информации о правилах сессий */
static std::map<std::string, std::list<std::string> > g_mapSessRuleLst;
/* мьютекс доступа к хранилищу */
static pthread_mutex_t g_mmutexSessRuleLst[3];

/* функция потока загрузки наального списка правил сессиий */
static int pcrf_session_rule_load_list();

/* указатель на объект статистики кэша правил сессий */
static SStat *g_psoSessionRuleCacheStat;

int pcrf_session_rule_list_init()
{
  /* запрашиваем адрес объекта статистики кэша правил сессий */
  g_psoSessionRuleCacheStat = stat_get_branch("session rule cache");

  /* загружаем первичный список правил сессий */
  CHECK_FCT( pcrf_session_rule_load_list() );

  /* инициализация мьютексов доступа к хранилищу кэша правил сессий */
  CHECK_FCT( pcrf_lock_init( g_mmutexSessRuleLst, sizeof( g_mmutexSessRuleLst ) / sizeof( *g_mmutexSessRuleLst ) ) );

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
  pcrf_lock_fini( g_mmutexSessRuleLst, sizeof( g_mmutexSessRuleLst ) / sizeof( *g_mmutexSessRuleLst ) );
}

int pcrf_session_rule_cache_get(std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectActive)
{
  CTimeMeasurer coTM;
  int iRetVal = 0;
  SDBAbonRule soRule(true,false);
  std::map<std::string, std::list<std::string> >::iterator iter;
  int iPrio = 1;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO(pcrf_lock(g_mmutexSessRuleLst, iPrio), goto cleanup_and_exit);

  /* ищем сессию */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  /* если сессия найдена */
  if (iter != g_mapSessRuleLst.end() && 0 < iter->second.size()) {
    /* обходим все активные правила */
    for (std::list<std::string>::iterator iterLst = iter->second.begin(); iterLst != iter->second.end(); ++iterLst) {
      soRule.m_coRuleName = *iterLst;
      /* сохраняем правилов в списке */
      p_vectActive.push_back(soRule);
    }
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
    iRetVal = 1403;
  }

  cleanup_and_exit:
  /* освобождаем хранилище */
  pcrf_unlock( g_mmutexSessRuleLst, iPrio );

  if (0 == iRetVal) {
    /* фиксируем в статистике успешное завершение */
    stat_measure(g_psoSessionRuleCacheStat, "hit", &coTM);
  } else {
    /* фиксируем в статистике промах */
    stat_measure(g_psoSessionRuleCacheStat, "miss", &coTM);
  }

  return iRetVal;
}

void pcrf_session_rule_cache_insert_local( std::string &p_strSessionId, std::string &p_strRuleName, bool p_bLowPriority )
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;
  int iPrio = 1;

  /* для операций с низким приоритетом проходим два уровня блокировки хранилища */
  /* блокируем мьютекс доступа к хранилищу с низким приотитетом */
  if ( ! p_bLowPriority ) {
  } else {
    iPrio = 2;
  }
  /* блокируем общий мьютекс доступа к хранилищу */
  CHECK_FCT_DO( pcrf_lock( g_mmutexSessRuleLst, iPrio ), goto cleanup_and_exit );

  /* ищем сессию в хранилище */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    /* если нашли, то дополняем правил список ее */
    iter->second.push_back( p_strRuleName );
    stat_measure(g_psoSessionRuleCacheStat, "rule inserted", &coTM);
    if ( !p_bLowPriority ) {
      LOG_D( "%s: rule inserted: %s; %s", __FUNCTION__, p_strSessionId.c_str(), p_strRuleName.c_str() );
    }
  } else {
    /* если сессия не найдена добавляем ее в хранилище */
    std::list<std::string> list;
    list.push_back( p_strRuleName );
    g_mapSessRuleLst.insert( std::pair<std::string, std::list<std::string> >( p_strSessionId, list ) );
    stat_measure(g_psoSessionRuleCacheStat, "session inserted", &coTM);
    if ( !p_bLowPriority ) {
      LOG_D( "%s: session inserted: %s; %s", __FUNCTION__, p_strSessionId.c_str(), p_strRuleName.c_str() );
    }
  }

cleanup_and_exit:
  pcrf_unlock( g_mmutexSessRuleLst, iPrio );

  return;
}

void pcrf_session_rule_cache_remove_sess_local(std::string &p_strSessionId)
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;
  int iPrio = 1;

  /* блокируем доступ к хранилищу кэша правил сессий */
  CHECK_FCT_DO(pcrf_lock(g_mmutexSessRuleLst, iPrio), goto cleanup_and_exit);

  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    g_mapSessRuleLst.erase(iter);
    stat_measure(g_psoSessionRuleCacheStat, "session removed", &coTM);
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
  }

  cleanup_and_exit:
  /* освобождаем хранилище */
  pcrf_unlock( g_mmutexSessRuleLst, iPrio );

  return;
}

void pcrf_session_rule_cache_remove_rule_local(std::string &p_strSessionId, std::string &p_strRuleName)
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;
  std::list<std::string>::iterator iterLst;
  int iPrio = 1;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO( pcrf_lock( g_mmutexSessRuleLst, iPrio ), goto cleanup_and_exit );

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
  pcrf_unlock( g_mmutexSessRuleLst, iPrio );

  return;
}

static int pcrf_session_rule_load_list()
{
  int iRetVal = 0;
  CTimeMeasurer coTM;
  int iRepeat = 1;
  otl_connect *pcoDBConn;
  std::string strSessionId;
  std::string strRuleName;
  char mcTime[64];

  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 10 * USEC_PER_SEC ) && NULL != pcoDBConn ) {
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
      pcrf_session_rule_cache_insert_local( strSessionId, strRuleName, true );
    }
    coTM.GetDifference(NULL, mcTime, sizeof(mcTime));
    coStream.close();
    UTL_LOG_N( *g_pcoLog, "session rule list is initialized successfully in '%s'; session rule count: '%u'", mcTime, g_mapSessRuleLst.size() );
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

  return iRetVal;
}

void pcrf_session_rule_cache_insert( std::string &p_strSessionId, std::string &p_soRuleName )
{
  /* проверяем параметры */
  if ( 0 < p_strSessionId.length() && 0 < p_soRuleName.length() ) {
  } else {
    LOG_D( "%s: session-id: %s; rule-name: %s", __FUNCTION__, p_strSessionId.c_str(), p_soRuleName.c_str() );
    return;
  }

  pcrf_session_rule_cache_insert_local( p_strSessionId, p_soRuleName );
  pcrf_session_cache_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_INSERT_SESSRUL), &p_soRuleName );
}

void pcrf_session_rule_cache_remove_rule(std::string &p_strSessionId, std::string &p_strRuleName)
{
  pcrf_session_rule_cache_remove_rule_local(p_strSessionId, p_strRuleName);
  pcrf_session_cache_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSRUL), &p_strRuleName);
}
