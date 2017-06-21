#include <pthread.h>

#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "pcrf_otl.h"
#include "utils/stat/stat.h"
#include "utils/log/log.h"

extern CLog *g_pcoLog;

/* локальное хранилище описания правил */
static std::map<std::string, SDBAbonRule> *g_pmapRule;
/* объект синхронизации доступа к хранилищу */
static pthread_mutex_t g_mutexRuleCache;
/* объект сбора статистики */
static SStat *g_psoRuleCahceStat;
/* флаг продолжения работы */
static volatile int g_iRuleCacheWork;
/* мьютекс, используемый в качестве таймера обновления локального хранилища */
static pthread_mutex_t g_mutexUpdateTimer;
/* дескриптор потока обновления локального хранилища */
static pthread_t g_threadUpdate;
/* функция потока обновления локального хранилища */
static void *pcrf_rule_cache_update(void *p_pvParam);

/* загрузка правил из БД */
static int pcrf_rule_cache_load_rule_list(std::map<std::string,SDBAbonRule> *p_pmapRule);
/* загрузка Flow-Description правила */
static int load_rule_flows(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows);
/* загрузка ключей мониторинга sce */
static int load_sce_rule_mk(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectMonitKey);

int pcrf_rule_cache_init()
{
  g_psoRuleCahceStat = stat_get_branch("rule cache");
  g_pmapRule = new std::map<std::string, SDBAbonRule>;
  CHECK_FCT(pcrf_rule_cache_load_rule_list(g_pmapRule));
  CHECK_FCT(pthread_mutex_init(&g_mutexRuleCache, NULL));
  g_iRuleCacheWork = 1;
  CHECK_FCT(pthread_mutex_init(&g_mutexUpdateTimer, NULL));
  CHECK_FCT(pthread_mutex_lock(&g_mutexUpdateTimer));
  CHECK_FCT(pthread_create(&g_threadUpdate, NULL, pcrf_rule_cache_update, NULL));

  UTL_LOG_N( *g_pcoLog,
    "rule cache is initialized successfully!\n"
    "\tstorage capasity is '%u' records",
    g_pmapRule->max_size() );

  return 0;
}

void pcrf_rule_cache_fini()
{
  g_iRuleCacheWork = 0;
  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexUpdateTimer), /* continue */ );
  if (0 != g_threadUpdate) {
    CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexUpdateTimer ), /* continue */ );
    CHECK_FCT_DO( pthread_join( g_threadUpdate, NULL ), /* continue */ );
  }
  CHECK_FCT_DO(pthread_mutex_destroy(&g_mutexUpdateTimer), /* continue */ );
  CHECK_FCT_DO(pthread_mutex_lock(&g_mutexRuleCache), /*continue*/ );
  CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexRuleCache), /* continue*/);
  CHECK_FCT_DO(pthread_mutex_destroy(&g_mutexRuleCache), /* continue */);
  if (NULL != g_pmapRule) {
    delete g_pmapRule;
    g_pmapRule = NULL;
  }
}

static int pcrf_rule_cache_load_rule_list(std::map<std::string,SDBAbonRule> *p_pmapRule)
{
  CTimeMeasurer coTM;
  int iRetVal = 0;
  int iRepeat = 1;

  otl_connect *pcoDBConn = NULL;

  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 10 * USEC_PER_SEC ) && NULL != pcoDBConn ) {
  } else {
    iRetVal = -1;
    goto clean_and_exit;
  }

  sql_repeat:
  p_pmapRule->clear();

  try {
    otl_nocommit_stream coStream;
    otl_value<std::string> coMonitKey;
    unsigned int uiRuleId;

    coStream.open(
      50,
      "select "
        "r.rule_name,"
        "r.id,"
        "r.dynamic_rule_flag,"
        "r.rule_group_flag,"
        "r.precedence_level,"
        "r.rating_group_id,"
        "r.service_id,"
        "r.metering_method,"
        "r.online_charging,"
        "r.offline_charging,"
        "qt.qos_class_identifier,"
        "qt.max_requested_bandwidth_ul,"
        "qt.max_requested_bandwidth_dl,"
        "qt.guaranteed_bitrate_ul,"
        "qt.guaranteed_bitrate_dl,"
        "mk.key_code,"
        "rs.redirect_address_type,"
        "rs.redirect_server_address "
      "from "
        "ps.rule r "
        "left join ps.qos_template qt on r.qos_template_id = qt.id "
        "left join ps.monitoring_key mk on r.monitoring_key_id = mk.id "
        "left join ps.redirection_server rs on r.redirection_server_id = rs.id",
      *pcoDBConn);
    while ( 0 == coStream.eof() && 0 != g_iRuleCacheWork ) {
      {
        SDBAbonRule soAbonRule( true );
        coStream
          >> soAbonRule.m_coRuleName
          >> uiRuleId
          >> soAbonRule.m_coDynamicRuleFlag
          >> soAbonRule.m_coRuleGroupFlag
          >> soAbonRule.m_coPrecedenceLevel
          >> soAbonRule.m_coRatingGroupId
          >> soAbonRule.m_coServiceId
          >> soAbonRule.m_coMeteringMethod
          >> soAbonRule.m_coOnlineCharging
          >> soAbonRule.m_coOfflineCharging
          >> soAbonRule.m_coQoSClassIdentifier
          >> soAbonRule.m_coMaxRequestedBandwidthUl
          >> soAbonRule.m_coMaxRequestedBandwidthDl
          >> soAbonRule.m_coGuaranteedBitrateUl
          >> soAbonRule.m_coGuaranteedBitrateDl
          >> coMonitKey
          >> soAbonRule.m_coRedirectAddressType
          >> soAbonRule.m_coRedirectServerAddress;
        /* обрабатываем ключ мониторинга */
        if ( 0 == coMonitKey.is_null() ) {
          soAbonRule.m_vectMonitKey.push_back( coMonitKey.v );
        }
        /* загружаем Flow-Description правила */
        CHECK_FCT_DO( load_rule_flows( pcoDBConn, uiRuleId, soAbonRule.m_vectFlowDescr ), /* continue */ );
        /* сохраняем правило в локальном хранилище */
        p_pmapRule->insert( std::pair<std::string, SDBAbonRule>( soAbonRule.m_coRuleName.v, soAbonRule ) );
      }
    }
    coStream.close();
    coStream.open(
      50,
      "select "
        "r.id,"
        "r.precedence_level,"
        "r.name,"
        "r.PACKAGE,"
        "r.REAL_TIME_MONITOR "
      "from "
        "ps.SCE_rule r",
      *pcoDBConn);
    while (0 == coStream.eof()) {
      {
        SDBAbonRule soAbonRule(true);
        coStream
          >> uiRuleId
          >> soAbonRule.m_coPrecedenceLevel
          >> soAbonRule.m_coRuleName
          >> soAbonRule.m_coSCE_PackageId
          >> soAbonRule.m_coSCE_RealTimeMonitor;
        CHECK_FCT_DO(load_sce_rule_mk(pcoDBConn, uiRuleId, soAbonRule.m_vectMonitKey), /* continue */);
        p_pmapRule->insert(std::pair<std::string, SDBAbonRule>(soAbonRule.m_coRuleName.v, soAbonRule));
      }
    }
    coStream.close();
  } catch (otl_exception &coExcept) {
    UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'; var info: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text, coExcept.var_info);
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    iRetVal = coExcept.code;
  }

  clean_and_exit:
  if (NULL != pcoDBConn) {
    pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
    pcoDBConn = NULL;
  }

  stat_measure(g_psoRuleCahceStat, __FUNCTION__, &coTM);

  return iRetVal;
}

static int load_rule_flows(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows)
{
  if (NULL != p_pcoDBConn) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;

  sql_repeat:
  p_vectRuleFlows.clear();

  otl_nocommit_stream coStream;
  try {
    std::string strFlowDescr;
    coStream.open(
      25,
      "select "
        "ft.flow_description "
      "from "
        "ps.rule_flows rf "
        "inner join ps.flow_template ft on rf.flow_template_id = ft.id "
      "where rf.rule_id = :rule_id/*unsigned*/",
      *p_pcoDBConn);
    coStream
      << p_uiRuleId;
    while (0 == coStream.eof()) {
      coStream >> strFlowDescr;
      p_vectRuleFlows.push_back(strFlowDescr);
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

  return iRetVal;
}

static int load_sce_rule_mk(otl_connect *p_pcoDBConn, unsigned int p_uiRuleId, std::vector<std::string> &p_vectMonitKey)
{
  if (NULL != p_pcoDBConn) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;

  sql_repeat:
  p_vectMonitKey.clear();

  try {
    otl_nocommit_stream coStream;
    std::string strMonitKey;

    coStream.open(
      25,
      "select "
        "mk.key_code "
      "from "
        "ps.sce_monitoring_key s "
        "inner join ps.monitoring_key mk on s.monitoring_key_id = mk.id "
      "where "
        "s.sce_rule_id = :rule_id /*unsigned*/",
      *p_pcoDBConn);
    coStream
      << p_uiRuleId;
    while (0 == coStream.eof()) {
      coStream >> strMonitKey;
      p_vectMonitKey.push_back(strMonitKey);
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

  return iRetVal;
}

/* загружает описание правила */
int pcrf_rule_cache_get_rule_info(
  SMsgDataForDB &p_soMsgInfo,
  std::string &p_strRuleName,
  std::vector<SDBAbonRule> &p_vectAbonRules)
{
  CTimeMeasurer coTM;

  int iRetVal = 0;
  std::map<std::string, SDBAbonRule>::iterator iter;
  std::vector<std::string>::iterator iterMonitKey;

  CHECK_FCT(pthread_mutex_lock(&g_mutexRuleCache));
  iter = g_pmapRule->find(p_strRuleName);
  if (iter != g_pmapRule->end()) {
    p_vectAbonRules.push_back(iter->second);
    for (iterMonitKey = iter->second.m_vectMonitKey.begin(); iterMonitKey != iter->second.m_vectMonitKey.end(); ++iterMonitKey) {
      p_soMsgInfo.m_psoSessInfo->m_mapMonitInfo.insert(std::pair<std::string, SDBMonitoringInfo>(*iterMonitKey, SDBMonitoringInfo()));
    }
  } else {
    iRetVal = 1403;
  }
  CHECK_FCT(pthread_mutex_unlock(&g_mutexRuleCache));

  stat_measure(g_psoRuleCahceStat, __FUNCTION__, &coTM);

  return iRetVal;
}

static void *pcrf_rule_cache_update(void *p_pvParam)
{
  timespec soTimeSpec;
  int iFnRes;
  std::map<std::string, SDBAbonRule> *pmapNew = NULL, *pmapTmp;

  while (0 != g_iRuleCacheWork) {
    if ( NULL != pmapNew ) {
      delete pmapNew;
      pmapNew = NULL;
    }
    CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 60, 0 ), goto clean_and_exit );
    iFnRes = pthread_mutex_timedlock( &g_mutexUpdateTimer, &soTimeSpec );
    if (0 == g_iRuleCacheWork) {
      break;
    }
    if (ETIMEDOUT == iFnRes) {
      pmapNew = new std::map<std::string, SDBAbonRule>;
      CHECK_FCT_DO( pcrf_rule_cache_load_rule_list( pmapNew ), continue );
      pmapTmp = g_pmapRule;
      CHECK_FCT_DO(pthread_mutex_lock(&g_mutexRuleCache), goto clean_and_exit);
      g_pmapRule = pmapNew;
      CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexRuleCache), goto clean_and_exit);
      delete pmapTmp;
      pmapNew = NULL;
    }
  }

  clean_and_exit:
  if (NULL != pmapNew) {
    delete pmapNew;
    pmapNew = NULL;
  }

  pthread_exit(NULL);
}
