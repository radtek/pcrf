#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/ps_common.h"

extern CLog *g_pcoLog;

#include <list>

/* хранилище информации о правилах сессий */
static std::map<std::string, std::list<std::string> > g_mapSessRuleLst;
/* мьютекс доступа к хранилищу для операций с низким приоритетом */
static pthread_mutex_t g_mutexSRLLowPrior;
/* мьютекс доступа к хранилищу */
static pthread_mutex_t g_mutexSessRuleLst;

/* функция потока загрузки наального списка правил сессиий */
static void * pcrf_session_rule_load_list(void*);

/* указатель на объект статистики кэша правил сессий */
static SStat *g_psoSessionRuleCacheStat;

int pcrf_session_rule_list_init()
{
  /* запрашиваем адрес объекта статистики кэша правил сессий */
  g_psoSessionRuleCacheStat = stat_get_branch("session rule cache");

  pthread_t tLoadThread;

  /* запускаем поток загрузки первичного списка правил сессий */
  CHECK_FCT(pthread_create(&tLoadThread, NULL, pcrf_session_rule_load_list, NULL));
  /* открепляем запущенный поток нам его судьба не особо интересна */
  CHECK_FCT(pthread_detach(tLoadThread));

  /* инициализация мьютексов доступа к хранилищу кэша правил сессий */
  CHECK_FCT(pthread_mutex_init(&g_mutexSRLLowPrior, NULL));
  CHECK_FCT(pthread_mutex_init(&g_mutexSessRuleLst, NULL));

  return 0;
}

void pcrf_session_rule_list_fini()
{
  /* освобождаем занятые ресурсы */
  CHECK_FCT_DO(pthread_mutex_destroy(&g_mutexSessRuleLst), /* continue */);
  CHECK_FCT_DO(pthread_mutex_destroy(&g_mutexSRLLowPrior), /* continue */);
}

int pcrf_session_rule_cache_get(std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectActive)
{
  CTimeMeasurer coTM;
  int iRetVal = 0;
  SDBAbonRule soRule;
  std::map<std::string, std::list<std::string> >::iterator iter;

  soRule.m_bIsActivated = true;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSessRuleLst), goto clean_and_exit);

  /* ищем сессию */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  /* если сессия найдена */
  if (iter != g_mapSessRuleLst.end() && 0 < iter->second.size()) {
    /* обходим все активные правила */
    for (std::list<std::string>::iterator iterLst = iter->second.begin(); iterLst != iter->second.end(); ++iterLst) {
      soRule.m_coRuleName = *iterLst;
      /* сохраняем правилов в списке */
      p_vectActive.push_back(soRule);
      LOG_D("get session rule: session-id: '%s'; rule-name: '%s'", p_strSessionId.c_str(), iterLst->c_str());
    }
    LOG_D("target vector size: '%d'", p_vectActive.size());
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
    iRetVal = 1403;
  }

  /* освобождаем хранилище */
  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSessRuleLst), /* continue */);

  if (0 == iRetVal) {
    /* фиксируем в статистике успешное завершение */
    stat_measure(g_psoSessionRuleCacheStat, "hit", &coTM);
  } else {
    /* фиксируем в статистике промах */
    stat_measure(g_psoSessionRuleCacheStat, "miss", &coTM);
  }

clean_and_exit:

  return iRetVal;
}

void pcrf_session_rule_cache_insert_local(std::string &p_strSessionId, std::string &p_strRuleName, bool p_bLowPriority)
{
  LOG_D("enter to %s", __FUNCTION__);

  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;

  /* для операций с низким приоритетом проходим два уровня блокировки хранилища */
  /* блокируем мьютекс доступа к хранилищу с низким приотитетом */
  if (p_bLowPriority) {
    CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSRLLowPrior), goto clean_and_exit);
  }
  /* блокируем общий мьютекс доступа к хранилищу */
  CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSessRuleLst), goto unlock_low_prior);

  /* ищем сессию в хранилище */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    /* если нашли, то дополняем правил список ее */
    iter->second.push_back(p_strRuleName);
    LOG_D("rule inserted: session-id: %s; rune-name: %s", p_strSessionId.c_str(), p_strRuleName.c_str());
    stat_measure(g_psoSessionRuleCacheStat, "rule inserted", &coTM);
  } else {
    /* если сессия не найдена добавляем ее в хранилище */
    std::list<std::string> list;
    list.push_back(p_strRuleName);
    g_mapSessRuleLst.insert(std::pair<std::string, std::list<std::string> >(p_strSessionId, list));
    if (!p_bLowPriority) {
      LOG_D("session inserted: session-id: %s; rune-name: %s", p_strSessionId.c_str(), p_strRuleName.c_str());
    }
    stat_measure(g_psoSessionRuleCacheStat, "session inserted", &coTM);
  }

  /* освобождаем хранилище */
  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSessRuleLst), /* continue */);

unlock_low_prior:
  if (p_bLowPriority) {
    CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSRLLowPrior), /* continue */);
  }

clean_and_exit:

  LOG_D("leave %s", __FUNCTION__);

  return;
}

void pcrf_session_rule_cache_remove_sess_local(std::string &p_strSessionId)
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;

  /* блокируем доступ к хранилищу кэша правил сессий */
  CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSessRuleLst), goto clean_and_exit);

  iter = g_mapSessRuleLst.find(p_strSessionId);
  if (iter != g_mapSessRuleLst.end()) {
    g_mapSessRuleLst.erase(iter);
    stat_measure(g_psoSessionRuleCacheStat, "session removed", &coTM);
    LOG_D("%s: session removed: '%s'", __FUNCTION__, p_strSessionId.c_str());
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
  }

  /* освобождаем хранилище */
  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSessRuleLst), /* continue */);

clean_and_exit:
  return;
}

void pcrf_session_rule_cache_remove_rule_local(std::string &p_strSessionId, std::string &p_strRuleName)
{
  CTimeMeasurer coTM;
  std::map<std::string, std::list<std::string> >::iterator iter;
  std::list<std::string>::iterator iterLst;

  /* блокируем доступ к хранилищу */
  CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSessRuleLst), goto clean_and_exit);

  /* ищем сессию */
  iter = g_mapSessRuleLst.find(p_strSessionId);
  /* если сессия найдена */
  if (iter != g_mapSessRuleLst.end()) {
    /* обходим все правила */
    for (iterLst = iter->second.begin(); iterLst != iter->second.end(); ) {
      if (0 == iterLst->compare(p_strRuleName)) {
        /* и удаляем найденное */
        iterLst = iter->second.erase(iterLst);
        /* фиксируем в модуле статистики успешное удаление правила */
        stat_measure(g_psoSessionRuleCacheStat, "rule removed", &coTM);
        LOG_D("%s: rule removed: session-id: '%s'; rule-name: '%s'", __FUNCTION__, p_strSessionId.c_str(), p_strRuleName.c_str());
      } else {
        ++iterLst;
      }
    }
  } else {
    LOG_D("%s: session not found: '%s'", __FUNCTION__, p_strSessionId.c_str());
  }

  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSessRuleLst), /* continue */);

clean_and_exit:
  return;
}

static void * pcrf_session_rule_load_list(void*)
{
  CTimeMeasurer coTM;
  otl_connect *pcoDBConn;
  std::string strSessionId;
  std::string strRuleName;
  char mcTime[64];

  if (0 == pcrf_db_pool_get(&pcoDBConn, __FUNCTION__, 10) && NULL != pcoDBConn) {
  } else {
    goto clean_and_exit;
  }

  try {
    otl_nocommit_stream coStream;

    coStream.open(
      1000,
      "select session_id, rule_name from ps.sessionRule where time_end is null",
      *pcoDBConn);
    while(0 == coStream.eof()) {
      coStream
        >> strSessionId
        >> strRuleName;
      LOG_D("%s: session-id: %s; rule-name: %s", __FUNCTION__, strSessionId.c_str(), strRuleName.c_str());
      pcrf_session_rule_cache_insert_local(strSessionId, strRuleName, true);
    }
    coTM.GetDifference(NULL, mcTime, sizeof(mcTime));
    UTL_LOG_N(*g_pcoLog, "session rule list is initialized successfully in '%s'; rule session count: '%u'", mcTime, g_mapSessRuleLst.size());
  } catch (otl_exception &coExcept) {
    UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
  }

clean_and_exit:
  if (NULL != pcoDBConn) {
    pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
  }

  pthread_exit(NULL);
}

void pcrf_session_rule_cache_insert(std::string &p_strSessionId, std::string &p_strRuleName)
{
  /* проверяем параметры */
  if (0 < p_strSessionId.length() && 0 < p_strRuleName.length()) {
  } else {
    UTL_LOG_E(*g_pcoLog, "invalid parameter values: session-id length: '%d'; rule-name length: '%d'", p_strSessionId.length(), p_strRuleName.length());
    return;
  }

  pcrf_session_rule_cache_insert_local(p_strSessionId, p_strRuleName);
  pcrf_session_cache_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_INSERT_SESSRUL), &p_strRuleName);
}

void pcrf_session_rule_cache_remove_rule(std::string &p_strSessionId, std::string &p_strRuleName)
{
  pcrf_session_rule_cache_remove_rule_local(p_strSessionId, p_strRuleName);
  pcrf_session_cache_cmd2remote(p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSRUL), &p_strRuleName);
}
