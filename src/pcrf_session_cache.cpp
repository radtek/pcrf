#include <pthread.h>
#include <errno.h>
#include <list>
#include <unordered_map>

#include <signal.h>

#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/ps_common.h"
#include "utils/pspacket/pspacket.h"
#include "pcrf_session_cache.h"
#include "pcrf_session_cache_index.h"
#include "utils/log/log.h"
#include "pcrf_ipc.h"

extern CLog *g_pcoLog;

#include "utils/stat/stat.h"
static SStat *g_psoSessionCacheStat;

/* хранилище для информации о сессиях */
static std::unordered_map<std::string,SSessionCache*> g_umapSessionCache;
static volatile bool g_bSessionListWork = true;

/* массив мьютексов для организации доступа к хранилищу с приоритетами */
static pthread_mutex_t g_mutexSessionCache;

/* загрузка списка сессий из БД */
static void * pcrf_session_cache_load_session_list( void * );

static void pcrf_session_cache_provide_stat_cb( char **p_ppszStat );

/* отправка сообщение нодам */
#define NODE_POLL_WAIT 1

int pcrf_session_cache_init( pthread_t *p_ptThread )
{
  /* останавливаем поток обработки команд */
  g_bSessionListWork = true;

  /* инициализация ветки статистики */
  g_psoSessionCacheStat = stat_get_branch ("session cache");
  /* загружаем список сессий из БД */
  CHECK_FCT( pthread_create( p_ptThread, NULL, pcrf_session_cache_load_session_list, NULL ) );
  /* создаем мьютекс */
  CHECK_FCT( pthread_mutex_init( &g_mutexSessionCache, NULL ) );

  UTL_LOG_N( *g_pcoLog,
    "session cache is initialized successfully!\n"
    "\tsession cache capasity is '%u' records\n",
    g_umapSessionCache.max_size() );

  stat_register_cb( pcrf_session_cache_provide_stat_cb );
  stat_register_cb( pcrf_session_cache_index_provide_stat_cb );

  return 0;
}

void pcrf_session_cache_fini (void)
{
  /* останавливаем поток обработки команд */
  g_bSessionListWork = false;

  /* уничтожаем мьютекс */
  pthread_mutex_destroy( &g_mutexSessionCache );

  /* зачищаем кеш */
  std::unordered_map<std::string, SSessionCache*>::iterator iter = g_umapSessionCache.begin();
  for ( ; iter != g_umapSessionCache.end(); ++iter ) {
    if ( NULL != iter->second ) {
      delete &(*iter->second);
    }
  }
}

int pcrf_make_timespec_timeout (timespec &p_soTimeSpec, uint32_t p_uiSec, uint32_t p_uiAddUSec)
{
  timeval soTimeVal;

  CHECK_FCT( gettimeofday( &soTimeVal, NULL ) );
  p_soTimeSpec.tv_sec = soTimeVal.tv_sec;
  p_soTimeSpec.tv_sec += p_uiSec;
  if ((soTimeVal.tv_usec + p_uiAddUSec) < USEC_PER_SEC) {
    p_soTimeSpec.tv_nsec = (soTimeVal.tv_usec + p_uiAddUSec) * NSEC_PER_USEC;
  } else {
    ++p_soTimeSpec.tv_sec;
    p_soTimeSpec.tv_nsec = (soTimeVal.tv_usec - USEC_PER_SEC + p_uiAddUSec) * NSEC_PER_USEC;
  }

  return 0;
}

void pcrf_session_cache_insert_local (std::string &p_strSessionId, SSessionCache *p_psoSessionInfo, std::string *p_pstrParentSessionId, int p_iPeerDialect )
{
  CTimeMeasurer coTM;
  std::pair<std::unordered_map<std::string,SSessionCache*>::iterator,bool> insertResult;
  int iPrio;

  /* дожидаемся завершения всех операций */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessionCache ), goto clean_and_exit);

  insertResult = g_umapSessionCache.insert (std::pair<std::string,SSessionCache*> (p_strSessionId, p_psoSessionInfo));
  if (! insertResult.second) {
    LOG_D( "session cache record replaced: Session-Id: %s", p_strSessionId.c_str() );
    /* если в кеше уже есть такая сессия обновляем ее значения */
    if (insertResult.first != g_umapSessionCache.end()) {
      delete &( *( insertResult.first->second ) );
      insertResult.first->second = p_psoSessionInfo;
      pcrf_session_cache_update_child(p_strSessionId, p_psoSessionInfo);
      stat_measure( g_psoSessionCacheStat, "updated", &coTM );
    } else {
      UTL_LOG_E(*g_pcoLog, "insertion into session cache failed: map: size: '%u'; max size: '%u'", g_umapSessionCache.size(), g_umapSessionCache.max_size());
    }
  } else {
    /* если создана новая запись */
    LOG_D( "session cache record created: Session-Id: %s", p_strSessionId.c_str() );

    /* создаем индекс по Framed-IP-Address */
    switch ( p_iPeerDialect ) {
      case GX_HW_UGW:
      case GX_ERICSSN:
        pcrf_session_cache_index_frameIPAddress_insert_session( p_psoSessionInfo->m_coFramedIPAddr.v, p_strSessionId );
        break;
    }
    stat_measure( g_psoSessionCacheStat, "inserted", &coTM );

    /* создаем индекс по subscriber-id */
    if ( p_psoSessionInfo && 0 == p_psoSessionInfo->m_coSubscriberId.is_null() ) {
      pcrf_session_cache_index_subscriberId_insert_session( p_psoSessionInfo->m_coSubscriberId.v, p_strSessionId );
    }

    /* сохраняем связку между сессиями */
    if ( NULL != p_pstrParentSessionId ) {
      pcrf_session_cache_mk_link2parent(p_strSessionId, p_pstrParentSessionId);
    }
  }

  clean_and_exit:
  pthread_mutex_unlock( &g_mutexSessionCache );

  return;
}

void pcrf_session_cache_insert ( std::string &p_strSessionId, SSessionInfo &p_soSessionInfo, SRequestInfo *p_psoRequestInfo, std::string *p_pstrParentSessionId)
{
  CTimeMeasurer coTM;
  /* проверяем параметры */
  if ( 0 != p_strSessionId.length() ) {
  } else {
    return;
  }

  SSessionCache *psoTmp = new SSessionCache;

  /* копируем необходмые данные */
  psoTmp->m_coSubscriberId    = p_soSessionInfo.m_strSubscriberId;
  psoTmp->m_coFramedIPAddr    = p_soSessionInfo.m_coFramedIPAddress;
  psoTmp->m_coCalledStationId = p_soSessionInfo.m_coCalledStationId;
  psoTmp->m_coOriginHost      = p_soSessionInfo.m_coOriginHost;
  psoTmp->m_coOriginRealm     = p_soSessionInfo.m_coOriginRealm;
  psoTmp->m_coIMEISV          = p_soSessionInfo.m_coIMEI;
  psoTmp->m_coEndUserIMSI     = p_soSessionInfo.m_coEndUserIMSI;
  psoTmp->m_coEndUserE164     = p_soSessionInfo.m_coEndUserE164;
  if ( NULL != p_psoRequestInfo ) {
    psoTmp->m_coIPCANType  = p_psoRequestInfo->m_soUserEnvironment.m_coIPCANType;
    psoTmp->m_iIPCANType   = p_psoRequestInfo->m_soUserEnvironment.m_iIPCANType;
    psoTmp->m_coSGSNMCCMNC = p_psoRequestInfo->m_soUserEnvironment.m_coSGSNMCCMNC;
    psoTmp->m_coSGSNIPAddr = p_psoRequestInfo->m_soUserEnvironment.m_coSGSNAddress;
    psoTmp->m_coRATType    = p_psoRequestInfo->m_soUserEnvironment.m_coRATType;
    psoTmp->m_iRATType     = p_psoRequestInfo->m_soUserEnvironment.m_iRATType;
    psoTmp->m_coCGI        = p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI;
    psoTmp->m_coECGI       = p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI;
    psoTmp->m_coTAI        = p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coTAI;
  }
  pcrf_ipc_cmd2remote( p_strSessionId, psoTmp, static_cast<uint16_t>( PCRF_CMD_INSERT_SESSION ), p_pstrParentSessionId );
  pcrf_session_cache_insert_local( p_strSessionId, psoTmp, p_pstrParentSessionId, pcrf_peer_dialect_ret( p_soSessionInfo.m_coOriginHost.v, p_soSessionInfo.m_coOriginRealm.v ) );

  return;
}

int pcrf_session_cache_get (std::string &p_strSessionId, SSessionInfo *p_psoSessionInfo, SRequestInfo *p_psoRequestInfo, std::string *p_pstrParentSessionId )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  CTimeMeasurer coTM;

  int iRetVal = 0;
  std::unordered_map<std::string,SSessionCache*>::iterator iter;

  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexSessionCache ), goto clean_and_exit );

  /* запрашиваем информацию о сессии из кеша */
  iter = g_umapSessionCache.find (p_strSessionId);
  if (iter != g_umapSessionCache.end ()) {
    if ( NULL != p_psoSessionInfo ) {
      if (! iter->second->m_coSubscriberId.is_null ()) {
        p_psoSessionInfo->m_strSubscriberId = iter->second->m_coSubscriberId.v;
      } else {
        p_psoSessionInfo->m_strSubscriberId = "";
      }
      if ( p_psoSessionInfo->m_strSessionId.length() == 0 )                  p_psoSessionInfo->m_strSessionId                        = p_strSessionId;
      if ( p_psoSessionInfo->m_coFramedIPAddress.is_null() )                 p_psoSessionInfo->m_coFramedIPAddress                   = iter->second->m_coFramedIPAddr;
      if ( p_psoSessionInfo->m_coCalledStationId.is_null() )                 p_psoSessionInfo->m_coCalledStationId                   = iter->second->m_coCalledStationId;
      if ( p_psoSessionInfo->m_coOriginHost.is_null() )                      p_psoSessionInfo->m_coOriginHost                        = iter->second->m_coOriginHost;
      if ( p_psoSessionInfo->m_coOriginRealm.is_null() )                     p_psoSessionInfo->m_coOriginRealm                       = iter->second->m_coOriginRealm;
      if ( p_psoSessionInfo->m_coIMEI.is_null() )                            p_psoSessionInfo->m_coIMEI                              = iter->second->m_coIMEISV;
      if ( p_psoSessionInfo->m_coEndUserIMSI.is_null() )                     p_psoSessionInfo->m_coEndUserIMSI                       = iter->second->m_coEndUserIMSI;
      if ( p_psoSessionInfo->m_coEndUserE164.is_null() )                     p_psoSessionInfo->m_coEndUserE164                       = iter->second->m_coEndUserE164;
    }
    if ( NULL != p_psoRequestInfo ) {
      if ( p_psoRequestInfo->m_soUserEnvironment.m_coIPCANType.is_null() )   p_psoRequestInfo->m_soUserEnvironment.m_coIPCANType   = iter->second->m_coIPCANType;
                                                                             p_psoRequestInfo->m_soUserEnvironment.m_iIPCANType    = iter->second->m_iIPCANType;
      if ( p_psoRequestInfo->m_soUserEnvironment.m_coSGSNMCCMNC.is_null() )  p_psoRequestInfo->m_soUserEnvironment.m_coSGSNMCCMNC  = iter->second->m_coSGSNMCCMNC;
      if ( p_psoRequestInfo->m_soUserEnvironment.m_coSGSNAddress.is_null() ) p_psoRequestInfo->m_soUserEnvironment.m_coSGSNAddress = iter->second->m_coSGSNIPAddr;
      if ( p_psoRequestInfo->m_soUserEnvironment.m_coRATType.is_null() )     p_psoRequestInfo->m_soUserEnvironment.m_coRATType     = iter->second->m_coRATType;
                                                                             p_psoRequestInfo->m_soUserEnvironment.m_iRATType      = iter->second->m_iRATType;
      /* если в запросе не было информации о лоакции */
      if ( p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI.is_null()
        && p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI.is_null()
        && p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coTAI.is_null() )
      {
        /* берем данные из кеша */
        p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coCGI  = iter->second->m_coCGI;
        p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coECGI = iter->second->m_coECGI;
        p_psoRequestInfo->m_soUserEnvironment.m_soUsrLoc.m_coTAI  = iter->second->m_coTAI;
      }

    }
    if ( NULL != p_pstrParentSessionId ) {
      pcrf_session_cache_get_linked_parent_session( p_strSessionId, *p_pstrParentSessionId );
    }
    stat_measure( g_psoSessionCacheStat, "hit", &coTM );
  } else {
    stat_measure( g_psoSessionCacheStat, "miss", &coTM );
    iRetVal = EINVAL;
  }

clean_and_exit:
  /* освобождаем мьютекс */
  pthread_mutex_unlock( &g_mutexSessionCache );

  LOG_D( "leave: %s; result code: %d", __FUNCTION__, iRetVal );

  return iRetVal;
}

int pcrf_session_cache_remove_local (std::string &p_strSessionId)
{
  int iRetVal = 0;
  CTimeMeasurer coTM;
  std::unordered_map<std::string,SSessionCache*>::iterator iter;

  /* дожадаемся освобождения мьютекса */
  CHECK_FCT( pthread_mutex_lock( &g_mutexSessionCache ) );

  pcrf_session_cache_remove_link (p_strSessionId);

  iter = g_umapSessionCache.find (p_strSessionId);
  if (iter != g_umapSessionCache.end ()) {
    if ( NULL != iter->second ) {
      if ( 0 == iter->second->m_coSubscriberId.is_null() ) {
        /* удаляем индекс по SubscriberId */
        pcrf_session_cache_rm_subscriber_session_id( iter->second->m_coSubscriberId.v, p_strSessionId );
      }
      /* удаляем индекс по Framed-IP-Address */
      pcrf_session_cache_index_frameIPAddress_remove_session( iter->second->m_coFramedIPAddr.v, p_strSessionId );
      /* удаляем сведения о сессии */
      delete &(*iter->second);
    } else {
      LOG_D( "pcrf_session_cache_remove_local: iter->second: empty pointer" );
    }
    g_umapSessionCache.erase (iter);
    stat_measure( g_psoSessionCacheStat, "delete.hit", &coTM );
  } else {
    iRetVal = ENODATA;
    stat_measure( g_psoSessionCacheStat, "delete.miss", &coTM );
    LOG_N( "session cache: delete: session not found: %s", p_strSessionId.c_str() );
  }

  /* освобождаем мьютекс */
  pthread_mutex_unlock( &g_mutexSessionCache );

  return iRetVal;
}

void pcrf_session_cache_remove (std::string &p_strSessionId)
{
  CTimeMeasurer coTM;

  pcrf_session_cache_remove_local (p_strSessionId);
  pcrf_session_rule_cache_remove_sess_local(p_strSessionId);

  pcrf_ipc_cmd2remote (p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSION), NULL);
}

static void pcrf_format_data_from_cache( std::string &p_strOut, const char *p_pszElementName, const char *p_pszElementValue )
{
  p_strOut += ' ';
  p_strOut += p_pszElementName;
  p_strOut += ": ";
  p_strOut += p_pszElementValue;
  p_strOut += ';';
}

#define PCRF_FORMAT_DATA_FROM_CACHE(a,b,c) pcrf_format_data_from_cache(a,b,0 == (c).is_null() ? (c).v.c_str() : "<null>")

void pcrf_session_cache_get_info_from_cache( std::string &p_strSessionId , std::string &p_strResult )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  int iDataFound = 0;
  std::unordered_map<std::string, SSessionCache*>::iterator iterSessionList;
  SSessionCache soSessionCache;

  /* проверяем содержимое параметра */
  if ( 0 != p_strSessionId.length() ) {
  } else {
    LOG_E( "%s; empty session-id", __FUNCTION__ );
    goto __leave_function__;
  }

  /* блокируем кэш */
  CHECK_FCT_DO( pcrf_session_cache_lock(), return );
  /* запрашиваем данные в кэше */
  iterSessionList = g_umapSessionCache.find( p_strSessionId );
  /* копируем данные в случае успеха */
  if ( iterSessionList != g_umapSessionCache.end() ) {
    soSessionCache = * dynamic_cast< SSessionCache* >( iterSessionList->second );
    iDataFound = 1;
  } else {
    iDataFound = 0;
  }
  /* снимаем блкировку */
  pcrf_session_cache_unlock();

  if ( 0 != iDataFound ) {
    p_strResult = "session info:";
    pcrf_format_data_from_cache( p_strResult, "Session-Id", p_strSessionId.c_str() );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "Subscriber-Id", soSessionCache.m_coSubscriberId );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "Framed-IP-Address", soSessionCache.m_coFramedIPAddr );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "Called-Station-Id", soSessionCache.m_coCalledStationId );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "IP-CAN-Type", soSessionCache.m_coIPCANType );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "SGSN-MCC-MNC", soSessionCache.m_coSGSNMCCMNC );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "SGSN-IP-Address", soSessionCache.m_coSGSNIPAddr );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "RAT-Type", soSessionCache.m_coRATType );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "Origin-Host", soSessionCache.m_coOriginHost );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "Origin-Realm", soSessionCache.m_coOriginRealm );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "CGI", soSessionCache.m_coCGI );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "ECGI", soSessionCache.m_coECGI );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "TAI", soSessionCache.m_coTAI );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "IMEI-SV", soSessionCache.m_coIMEISV );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "End-User-IMSI", soSessionCache.m_coEndUserIMSI );
    PCRF_FORMAT_DATA_FROM_CACHE( p_strResult, "End-User-E164", soSessionCache.m_coEndUserE164 );

    std::vector<SDBAbonRule> vectRuleList;

    pcrf_session_rule_cache_get( p_strSessionId, vectRuleList );

    /* список правил сессии */
    p_strResult += "\r\n";
    p_strResult += "session rule list:";
    if ( 0 == vectRuleList.size() ) {
      p_strResult += " <null>";
    } else {
      for ( std::vector<SDBAbonRule>::iterator iter = vectRuleList.begin(); iter != vectRuleList.end(); ++iter ) {
        p_strResult += ' ';
        p_strResult += iter->m_strRuleName;
        p_strResult += ';';
      }
    }

    std::list<std::string> listSessionIdList;

    pcrf_session_cache_get_linked_child_session_list( p_strSessionId, listSessionIdList );

    /* список связанных сессий */
    p_strResult += "\r\n";
    p_strResult += "linked session list:";
    if ( 0 == listSessionIdList.size() ) {
      p_strResult += " <null>";
    } else {
      for ( std::list<std::string>::iterator iter = listSessionIdList.begin(); iter != listSessionIdList.end(); ++iter ) {
        p_strResult += ' ';
        p_strResult += *iter;
        p_strResult += ';';
      }
    }
  } else {
    p_strResult = "data not found";
  }

  __leave_function__:

  LOG_D( "leave: %s", __FUNCTION__ );
}

static void * pcrf_session_cache_load_session_list( void * )
{
  int iRetVal = 0;
  CTimeMeasurer coTM;
  otl_connect *pcoDBConn = NULL;

  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
  } else {
    goto clean_and_exit;
  }

  try {
    otl_nocommit_stream coStream;

    coStream.open(
      1000,
      "select "
        "sl.session_id,"
        "sl.subscriber_id,"
        "sl.framed_ip_address,"
        "sl.called_station_id,"
        "sloc.ip_can_type,"
        "sloc.sgsn_mcc_mnc,"
        "sloc.sgsn_ip_address,"
        "sloc.rat_type,"
        "sl.origin_host,"
        "sl.origin_realm,"
        "sloc.cgi,"
        "sloc.ecgi,"
        "sloc.tai,"
        "sl.IMEISV,"
        "sl.end_user_imsi,"
        "sl.end_user_e164 "
      "from "
        "ps.sessionList sl "
        "inner join ps.peer p on sl.origin_host = p.host_name and sl.origin_realm = p.realm "
        "left join ps.sessionLocation sloc on sl.session_id = sloc.session_id "
      "where "
        "sl.time_end is null "
        "and sloc.time_end is null "
        "and p.protocol_id in(1, 4) /* GX_HW_UGW, GX_ERICSSN */ "
      "order by sl.time_start",
      *pcoDBConn );
    while ( 0 == coStream.eof() && g_bSessionListWork ) {
      {
        std::string strSessionId;
        SSessionCache *psoSessCache = new SSessionCache;

        coStream
          >> strSessionId
          >> psoSessCache->m_coSubscriberId
          >> psoSessCache->m_coFramedIPAddr
          >> psoSessCache->m_coCalledStationId
          >> psoSessCache->m_coIPCANType
          >> psoSessCache->m_coSGSNMCCMNC
          >> psoSessCache->m_coSGSNIPAddr
          >> psoSessCache->m_coRATType
          >> psoSessCache->m_coOriginHost
          >> psoSessCache->m_coOriginRealm
          >> psoSessCache->m_coCGI
          >> psoSessCache->m_coECGI
          >> psoSessCache->m_coTAI
          >> psoSessCache->m_coIMEISV
          >> psoSessCache->m_coEndUserIMSI
          >> psoSessCache->m_coEndUserE164;
        if ( 0 == psoSessCache->m_coIPCANType.is_null() ) {
          dict_object * enum_obj = NULL;
          dict_enumval_request req;
          memset( &req, 0, sizeof( struct dict_enumval_request ) );

          /* First, get the enumerated type of the IP-CAN-Type AVP (this is fast, no need to cache the object) */
          CHECK_FCT_DO( fd_dict_search( fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictIPCANType, &( req.type_obj ), ENOENT ), delete psoSessCache;  continue );

          /* Now search for the value given as parameter */
          req.search.enum_name = const_cast<char*>( psoSessCache->m_coIPCANType.v.c_str() );
          CHECK_FCT_DO(
            fd_dict_search( fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP ),
            LOG_D( "session-id: %s; ip-can-type: %s", strSessionId.c_str(), psoSessCache->m_coIPCANType.v.c_str() );  delete psoSessCache; continue );

          /* finally retrieve its data */
          CHECK_FCT_DO( fd_dict_getval( enum_obj, &( req.search ) ), delete psoSessCache; continue );

          /* copy the found value, we're done */
          psoSessCache->m_iIPCANType = req.search.enum_value.i32;
        }
        if ( 0 == psoSessCache->m_coRATType.is_null() ) {
          dict_object * enum_obj = NULL;
          dict_enumval_request req;
          memset( &req, 0, sizeof( struct dict_enumval_request ) );

          /* First, get the enumerated type of the RAT-Type AVP (this is fast, no need to cache the object) */
          CHECK_FCT_DO( fd_dict_search( fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictRATType, &( req.type_obj ), ENOENT ), delete psoSessCache; continue );

          /* Now search for the value given as parameter */
          req.search.enum_name = const_cast<char*>( psoSessCache->m_coRATType.v.c_str() );
          CHECK_FCT_DO( fd_dict_search( fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP ), delete psoSessCache; continue );

          /* finally retrieve its data */
          CHECK_FCT_DO( fd_dict_getval( enum_obj, &( req.search ) ), delete psoSessCache; continue );

          /* copy the found value, we're done */
          psoSessCache->m_iRATType = req.search.enum_value.i32;
        }
        pcrf_session_cache_insert_local( strSessionId, psoSessCache, NULL, pcrf_peer_dialect_ret( psoSessCache->m_coOriginHost.v, psoSessCache->m_coOriginRealm.v ) );
      }
    }
    {
      char mcDuration[ 128 ];
      if ( 0 == coTM.GetDifference( NULL, mcDuration, sizeof( mcDuration ) ) ) {
        UTL_LOG_N( *g_pcoLog, "session list is loaded in '%s'; session count: '%u'", mcDuration, g_umapSessionCache.size() );
      }
    }
    coStream.close();
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    iRetVal = coExcept.code;
  }

  clean_and_exit:
  if ( NULL != pcoDBConn ) {
    pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
    pcoDBConn = NULL;
  }

  int *piRetVal = reinterpret_cast< int* >( malloc( sizeof( int ) ) );

  *piRetVal = iRetVal;

  pthread_exit( piRetVal );
}

int pcrf_session_cache_lock()
{
  CHECK_FCT( pthread_mutex_lock( &g_mutexSessionCache ) );
}

void pcrf_session_cache_unlock()
{
  pthread_mutex_unlock( &g_mutexSessionCache );
}

void pcrf_session_cache_update_some_values( std::string &p_strSessionId, const SSessionCache *p_psoSomeNewInfo )
{
  std::unordered_map<std::string, SSessionCache*>::iterator iterCache;

  iterCache = g_umapSessionCache.find( p_strSessionId );

  if ( iterCache != g_umapSessionCache.end() ) {
    if ( ! p_psoSomeNewInfo->m_coSubscriberId.is_null() )    iterCache->second->m_coSubscriberId    = p_psoSomeNewInfo->m_coSubscriberId;
    if ( ! p_psoSomeNewInfo->m_coFramedIPAddr.is_null() )    iterCache->second->m_coFramedIPAddr    = p_psoSomeNewInfo->m_coFramedIPAddr;
    if ( ! p_psoSomeNewInfo->m_coCalledStationId.is_null() ) iterCache->second->m_coCalledStationId = p_psoSomeNewInfo->m_coCalledStationId;
    if ( ! p_psoSomeNewInfo->m_coIPCANType.is_null() )       iterCache->second->m_coIPCANType       = p_psoSomeNewInfo->m_coIPCANType;
                                                             iterCache->second->m_iIPCANType        = p_psoSomeNewInfo->m_iIPCANType;
    if ( ! p_psoSomeNewInfo->m_coSGSNMCCMNC.is_null() )      iterCache->second->m_coSGSNMCCMNC      = p_psoSomeNewInfo->m_coSGSNMCCMNC;
    if ( ! p_psoSomeNewInfo->m_coSGSNIPAddr.is_null() )      iterCache->second->m_coSGSNIPAddr      = p_psoSomeNewInfo->m_coSGSNIPAddr;
    if ( ! p_psoSomeNewInfo->m_coRATType.is_null() )         iterCache->second->m_coRATType         = p_psoSomeNewInfo->m_coRATType;
                                                             iterCache->second->m_iRATType          = p_psoSomeNewInfo->m_iRATType;
    /* если получены новые данные о локации */
    if ( 0 == p_psoSomeNewInfo->m_coCGI.is_null()
      || 0 == p_psoSomeNewInfo->m_coECGI.is_null()
      || 0 == p_psoSomeNewInfo->m_coTAI.is_null() )
    {
                                                             iterCache->second->m_coCGI             = p_psoSomeNewInfo->m_coCGI;
                                                             iterCache->second->m_coECGI            = p_psoSomeNewInfo->m_coECGI;
                                                             iterCache->second->m_coTAI             = p_psoSomeNewInfo->m_coTAI;
    }
  }
}

static void pcrf_session_cache_provide_stat_cb( char **p_ppszStat )
{
  int iFnRes;

  if(0 == pcrf_session_cache_lock()){
    iFnRes = asprintf( p_ppszStat, "session cache has %u items", g_umapSessionCache.size() );
    if ( 0 < iFnRes ) {
    } else {
      *p_ppszStat = NULL;
    }
    pcrf_session_cache_unlock();
  }
}
