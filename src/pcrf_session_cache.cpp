#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include "utils/ps_common.h"
#include "utils/pspacket/pspacket.h"

#include <semaphore.h>
#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <errno.h>
#include <list>

#include "utils/log/log.h"
extern CLog *g_pcoLog;

#include "utils/stat/stat.h"
static SStat *g_psoSessionCacheStat;

struct SSessionCache {
  otl_value<std::string>  m_coSubscriberId;
  otl_value<std::string>  m_coFramedIPAddr;
  otl_value<std::string>  m_coCalledStationId;
  otl_value<std::string>  m_coIPCANType;
  otl_value<std::string>  m_coSGSNIPAddr;
  otl_value<std::string>  m_coRATType;
  otl_value<std::string>  m_coOriginHost;
  otl_value<std::string>  m_coOriginRealm;
  otl_value<std::string>  m_coCGI;
  otl_value<std::string>  m_coECGI;
  otl_value<std::string>  m_coIMEISV;
  otl_value<std::string>  m_coEndUserIMSI;
  int32_t                 m_iIPCANType;
  int32_t                 m_iRATType;
  SSessionCache() : m_iIPCANType(0), m_iRATType(0) { }
};

struct SNode {
	DiamId_t  m_diamid;
	size_t		m_diamid_len;
	DiamId_t	m_diamrlm;
	size_t		m_diamrlm_len;
  std::string m_addr;
  uint16_t m_port;
};

/* хранилище для информации о сессиях */
static std::map<std::string,SSessionCache> *g_pmapSessionCache;
static std::map<std::string,std::list<std::string> > *g_pmapParent;
static std::map<std::string,std::string> *g_pmapChild;

/* для организации низкоприоритетного доступа к хранилищу */
static pthread_mutex_t g_mutexSCLowPrio;
/* мьютекс для организации доступа к хранилищу */
static pthread_mutex_t g_mutexSessionCache;

/* ожиданеие мьютекса */
#define MUTEX_RD_WAIT 1000
#define MUTEX_WR_WAIT 2000
#define MUTEX_RM_WAIT 5000

/* список нод */
static std::vector<SNode> *g_pvectNodeList;

/* поток для обработки входящих команд */
static pthread_t    g_thrdSessionCacheReceiver;
static volatile bool g_lets_work = true;
static std::string  g_strLocalIPAddress = "0.0.0.0";
static uint16_t     g_uiLocalPort = 7777;
static volatile bool g_module_is_initialized = false;
static void * pcrf_session_cache_receiver (void *p_vParam);
#define POLL_TIMEOUT 100

/* загрузка списка нод */
static int g_send_sock = -1;
static int pcrf_session_cache_init_node ();
static void pcrf_session_cache_fini_node ();

/* загрузка списка сессий из БД */
static void * pcrf_session_cache_load_session_list(void *p_pArg);

/* отправка сообщение нодам */
#define NODE_POLL_WAIT 1
#pragma pack(push,1)
struct SPayloadHdr {
  uint16_t m_vend_id;
  uint16_t m_avp_id;
  uint16_t m_padding;
  uint16_t m_payload_len;
};
#pragma pack(pop)

int pcrf_session_cache_init ()
{
  /* инициализация ветки статистики */
  g_psoSessionCacheStat = stat_get_branch ("session cache");
  /* создаем кеш сессий */
  g_pmapSessionCache = new std::map<std::string,SSessionCache>;
  g_pmapParent = new std::map<std::string,std::list<std::string> >;
  g_pmapChild = new std::map<std::string,std::string>;
  /* загружаем список сессий из БД */
  pthread_t tThread;
  if (0 == pthread_create(&tThread, NULL, pcrf_session_cache_load_session_list, NULL)) {
    pthread_detach(tThread);
  }
  /* создаем список нод */
  g_pvectNodeList = new std::vector<SNode>;
  /* создаем семафор */
  CHECK_FCT(pthread_mutex_init (&g_mutexSCLowPrio, NULL));
  CHECK_FCT( pthread_mutex_init (&g_mutexSessionCache, NULL) );
  /* создаем поток для обработки входящих команд */
  CHECK_FCT( pthread_create (&g_thrdSessionCacheReceiver, NULL, pcrf_session_cache_receiver, NULL) );
  /* загрузка списка нод */
  CHECK_FCT( pcrf_session_cache_init_node () );

  g_module_is_initialized = true;

  UTL_LOG_N(*g_pcoLog, "session cache is initialized successfully");

  return 0;
}

void pcrf_session_cache_fini (void)
{
  /* останавливаем поток обработки команд */
  g_lets_work = false;
  if (0 != g_thrdSessionCacheReceiver) {
    CHECK_FCT_DO(pthread_join(g_thrdSessionCacheReceiver, NULL), /* continue */ );
  }
  /* уничтожаем семафор */
  CHECK_FCT_DO( pthread_mutex_destroy (&g_mutexSCLowPrio), /* continue */ );
  CHECK_FCT_DO( pthread_mutex_destroy (&g_mutexSessionCache), /* continue */ );
  /* удаляем кеш */
  if (NULL != g_pmapSessionCache) {
    delete g_pmapSessionCache;
  }
  if (NULL != g_pmapParent) {
    delete g_pmapParent;
  }
  if (NULL != g_pmapChild) {
    delete g_pmapChild;
  }
  /* освобождаем ресурсы нод */
  pcrf_session_cache_fini_node ();
  /* удаляем список нод */
  if (NULL != g_pvectNodeList) {
    delete g_pvectNodeList;
  }
}

int pcrf_make_timespec_timeout (timespec &p_soTimeSpec, uint32_t p_uiAddUSec)
{
  timeval soTimeVal;

  CHECK_FCT(gettimeofday(&soTimeVal, NULL));
  p_soTimeSpec.tv_sec = soTimeVal.tv_sec;
  if ((soTimeVal.tv_usec + p_uiAddUSec) < USEC_PER_SEC) {
    p_soTimeSpec.tv_nsec = (soTimeVal.tv_usec + p_uiAddUSec) * NSEC_PER_USEC;
  } else {
    ++p_soTimeSpec.tv_sec;
    p_soTimeSpec.tv_nsec = (soTimeVal.tv_usec - USEC_PER_SEC + p_uiAddUSec) * NSEC_PER_USEC;
  }

  return 0;
}

static inline void pcrf_session_cache_update_child (std::string &p_strSessionId, SSessionCache &p_soSessionInfo)
{
  std::map<std::string,std::list<std::string> >::iterator iterParent = g_pmapParent->find (p_strSessionId);
  std::list<std::string>::iterator iterList;
  std::map<std::string,SSessionCache>::iterator iterCache;

  if (iterParent != g_pmapParent->end()) {
    for (iterList = iterParent->second.begin(); iterList != iterParent->second.end(); ++iterList) {
      iterCache = g_pmapSessionCache->find (*iterList);
      if (iterCache != g_pmapSessionCache->end ()) {
        if (! p_soSessionInfo.m_coSubscriberId.is_null())     iterCache->second.m_coSubscriberId    = p_soSessionInfo.m_coSubscriberId;
        if (! p_soSessionInfo.m_coFramedIPAddr.is_null())     iterCache->second.m_coFramedIPAddr    = p_soSessionInfo.m_coFramedIPAddr;
        if (! p_soSessionInfo.m_coCalledStationId.is_null())  iterCache->second.m_coCalledStationId = p_soSessionInfo.m_coCalledStationId;
        if (! p_soSessionInfo.m_coIPCANType.is_null())        iterCache->second.m_coIPCANType       = p_soSessionInfo.m_coIPCANType;
                                                              iterCache->second.m_iIPCANType        = p_soSessionInfo.m_iIPCANType;
        if (! p_soSessionInfo.m_coSGSNIPAddr.is_null())       iterCache->second.m_coSGSNIPAddr      = p_soSessionInfo.m_coSGSNIPAddr;
        if (! p_soSessionInfo.m_coRATType.is_null())          iterCache->second.m_coRATType         = p_soSessionInfo.m_coRATType;
                                                              iterCache->second.m_iRATType          = p_soSessionInfo.m_iRATType;
        if (! p_soSessionInfo.m_coCGI.is_null())              iterCache->second.m_coCGI             = p_soSessionInfo.m_coCGI;
        if (! p_soSessionInfo.m_coECGI.is_null())             iterCache->second.m_coECGI            = p_soSessionInfo.m_coECGI;
      }
    }
  }
}

static inline void pcrf_session_cache_rm_parent2child_link (std::string &p_strSessionId, std::string &p_strParentSessionId)
{
  std::map<std::string,std::list<std::string> >::iterator iter = g_pmapParent->find (p_strParentSessionId);
  if (iter != g_pmapParent->end()) {
    for (std::list<std::string>::iterator iterLst = iter->second.begin(); iterLst != iter->second.end(); ) {
      if (*iterLst == p_strSessionId) {
        iterLst = iter->second.erase (iterLst);
      } else {
        ++iterLst;
      }
    }
  }
}

static inline void pcrf_session_cache_mk_parent2child (std::string &p_strSessionId, std::string &p_strParentSessionId)
{
  std::map<std::string,std::list<std::string> >::iterator iter = g_pmapParent->find (p_strParentSessionId);
  if (iter != g_pmapParent->end ()) {
    iter->second.push_back (p_strSessionId);
  } else {
    std::list<std::string> lst;
    lst.push_back (p_strSessionId);
    g_pmapParent->insert (std::pair<std::string,std::list<std::string> > (p_strParentSessionId, lst));
  }
}

static inline void pcrf_session_cache_mk_link2parent (std::string &p_strSessionId, std::string *p_pstrParentSessionId)
{
  if (NULL != p_pstrParentSessionId) {
  } else {
    return;
  }
  /* создаем линк к дочернему элементу */
  pcrf_session_cache_mk_parent2child (p_strSessionId, *p_pstrParentSessionId);

  /* создаем линк к родителю */
  std::pair<std::map<std::string,std::string>::iterator,bool> pair;
  pair = g_pmapChild->insert (std::pair<std::string,std::string> (p_strSessionId, *p_pstrParentSessionId));
  /* если связка уже существует */
  if (! pair.second) {
    /* если отношения между родительской и дочерней сессией не изменились */
    if (pair.first != g_pmapChild->end()) {
      if (pair.first->second == (*p_pstrParentSessionId)) {
      } else {
        UTL_LOG_N(*g_pcoLog, "session id '%s': parent was changed from '%s' to '%s'", p_strSessionId.c_str(), pair.first->second.c_str(), p_pstrParentSessionId->c_str());
        pcrf_session_cache_rm_parent2child_link(p_strSessionId, pair.first->second);
        pair.first->second = *p_pstrParentSessionId;
        pcrf_session_cache_mk_parent2child(p_strSessionId, *p_pstrParentSessionId);
      }
    } else {
      UTL_LOG_E(*g_pcoLog, "insertion of child2parent link failed: map: size: '%u'; max size: '%u'", g_pmapChild->size(), g_pmapChild->max_size());
    }
  }
}

static inline void pcrf_session_cache_remove_link (std::string &p_strSessionId)
{
  std::map<std::string,std::list<std::string> >::iterator iterParent;
  std::list<std::string>::iterator iterList;
  std::map<std::string,std::string>::iterator iterChild;

  iterParent = g_pmapParent->find (p_strSessionId);
  /* если сессия родительская */
  if (iterParent != g_pmapParent->end ()) {
    /* обходим список всех дочерних сессий */
    for (iterList = iterParent->second.begin(); iterList != iterParent->second.end(); ++iterList) {
      /* ищем и удаляем дочернюю сессию */
      iterChild = g_pmapChild->find (*iterList);
      if (iterChild != g_pmapChild->end() && iterChild->second == p_strSessionId) {
        g_pmapChild->erase (iterChild);
      }
    }
    g_pmapParent->erase(iterParent);
  } else {
    iterChild = g_pmapChild->find (p_strSessionId);
    /* если сессия дочерняя */
    if (iterChild != g_pmapChild->end ()) {
      /* удаляем связку родительской сессии */
      pcrf_session_cache_rm_parent2child_link (p_strSessionId, iterChild->second);
      /* удаляем связку с родетельской сессией */
      g_pmapChild->erase (iterChild);
    }
  }
}

static inline void pcrf_session_cache_insert_local (std::string &p_strSessionId, SSessionCache &p_soSessionInfo, std::string *p_pstrParentSessionId, bool p_bLowPriority = false)
{
  std::pair<std::map<std::string,SSessionCache>::iterator,bool> insertResult;

  if (p_bLowPriority) {
    CHECK_FCT_DO(pthread_mutex_lock(&g_mutexSCLowPrio), goto clean_and_exit);
  }

  timespec soTimeSpec;

  CHECK_FCT_DO(pcrf_make_timespec_timeout (soTimeSpec, MUTEX_WR_WAIT), return);

  /* дожидаемся завершения всех операций */
  CHECK_FCT_DO(pthread_mutex_timedlock (&g_mutexSessionCache, &soTimeSpec), stat_measure(g_psoSessionCacheStat, "insert/update failed", NULL); goto unlock_low_priority);

  insertResult = g_pmapSessionCache->insert (std::pair<std::string,SSessionCache> (p_strSessionId, p_soSessionInfo));
  /* если в кеше уже есть такая сессия обновляем ее значения */
  if (! insertResult.second) {
    if (insertResult.first != g_pmapSessionCache->end()) {
      insertResult.first->second = p_soSessionInfo;
      pcrf_session_cache_update_child(p_strSessionId, p_soSessionInfo);
      stat_measure(g_psoSessionCacheStat, "updated", NULL);
    } else {
      UTL_LOG_E(*g_pcoLog, "insertion into session cache failed: map: size: '%u'; max size: '%u'", g_pmapSessionCache->size(), g_pmapSessionCache->max_size());
    }
  } else {
    stat_measure (g_psoSessionCacheStat, "inserterd", NULL);
  }

  /* сохраняем связку между сессиями */
  pcrf_session_cache_mk_link2parent(p_strSessionId, p_pstrParentSessionId);

  CHECK_FCT_DO( pthread_mutex_unlock (&g_mutexSessionCache), /* continue */ );

unlock_low_priority:
  if (p_bLowPriority) {
    CHECK_FCT_DO(pthread_mutex_unlock(&g_mutexSCLowPrio), /* continue */);
  }

clean_and_exit:

  return;
}

static inline int pcrf_session_cache_fill_payload (SPayloadHdr *p_psoPayload, size_t p_stMaxSize, uint16_t p_uiVendId, uint16_t p_uiAVPId, const void *p_pvData, uint16_t p_uiDataLen)
{
  if (p_uiDataLen + sizeof(*p_psoPayload) <= p_stMaxSize) {
    /* все в порядке */
  } else {
    /* размера буфера не достаточно */
    return EINVAL;
  }
  p_psoPayload->m_vend_id = p_uiVendId;
  p_psoPayload->m_avp_id = p_uiAVPId;
  p_psoPayload->m_padding = 0;
  p_psoPayload->m_payload_len = p_uiDataLen + sizeof(*p_psoPayload);
  memcpy(reinterpret_cast<char*>(p_psoPayload) + sizeof(*p_psoPayload), p_pvData, p_uiDataLen);

  return 0;
}

static int pcrf_session_cache_fill_pspack (
  char *p_pmcBuf,
  size_t p_stBufSize,
  std::string &p_strSessionId,
  SSessionCache *p_psoSessionInfo,
  uint16_t p_uiReqType,
  std::string *p_pstrOptionalParam)
{
  int iRetVal = 0;
  CPSPacket ps_pack;
  static uint32_t uiReqNum = 0;
  SPayloadHdr *pso_payload_hdr;
  char mc_attr[256];
  uint16_t uiVendId;
  uint16_t uiAVPId;

  CHECK_FCT(ps_pack.Init (reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, uiReqNum, p_uiReqType));

  /* связываем заголовок с буфером */
  pso_payload_hdr = reinterpret_cast<SPayloadHdr*>(mc_attr);

  /* добавляем SessionId */
  CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), 0, 263, p_strSessionId.data(), p_strSessionId.length()));
  if (0 < (iRetVal = ps_pack.AddAttr (reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
  } else {
    return -1;
  }

  if (static_cast<uint16_t>(PCRF_CMD_INSERT_SESSION) == p_uiReqType) {
    if (NULL != p_psoSessionInfo) {
    } else {
      return -1;
    }
    otl_value<std::string> *pco_field;
    /* добавляем Subscriber-Id */
    pco_field = &p_psoSessionInfo->m_coSubscriberId;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PS_SUBSCR;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if ( 0 < (iRetVal = ps_pack.AddAttr (reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем Framed-IP-Address */
    pco_field = &p_psoSessionInfo->m_coFramedIPAddr;
    if (0 == pco_field->is_null()) {
      uiVendId = 0;
      uiAVPId = 8;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем Called-Station-Id */
    pco_field = &p_psoSessionInfo->m_coCalledStationId;
    if (0 == pco_field->is_null()) {
      uiVendId = 0;
      uiAVPId = 30;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем IP-CAN-Type */
    uiVendId = 10415;
    uiAVPId = 1027;
    CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, &p_psoSessionInfo->m_iIPCANType, sizeof(p_psoSessionInfo->m_iIPCANType)));
    if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
    } else {
      return -1;
    }
    /* добавляем IP-CAN-Type (в текстовом виде) */
    pco_field = &p_psoSessionInfo->m_coIPCANType;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IPCANTYPE;
      CHECK_FCT(pcrf_session_cache_fill_payload(pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем SGSN-IP-Address */
    pco_field = &p_psoSessionInfo->m_coSGSNIPAddr;
    if (0 == pco_field->is_null()) {
      uiVendId = 10415;
      uiAVPId = 6;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем RAT-Type */
    uiVendId = 10415;
    uiAVPId = 1032;
    CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, &p_psoSessionInfo->m_iRATType, sizeof(p_psoSessionInfo->m_iRATType)));
    if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
    } else {
      return -1;
    }
    /* добавляем RAT-Type (в текстовом виде)*/
    pco_field = &p_psoSessionInfo->m_coRATType;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_RATTYPE;
      CHECK_FCT(pcrf_session_cache_fill_payload(pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем Origin-Host */
    pco_field = &p_psoSessionInfo->m_coOriginHost;
    if (0 == pco_field->is_null()) {
      uiVendId = 0;
      uiAVPId = 264;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем Origin-Realm */
    pco_field = &p_psoSessionInfo->m_coOriginRealm;
    if (0 == pco_field->is_null()) {
      uiVendId = 0;
      uiAVPId = 296;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем CGI */
    pco_field = &p_psoSessionInfo->m_coCGI;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_CGI;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем ECGI */
    pco_field = &p_psoSessionInfo->m_coECGI;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_ECGI;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем IMEI-SV */
    pco_field = &p_psoSessionInfo->m_coIMEISV;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IMEI;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем End-User-IMSI */
    pco_field = &p_psoSessionInfo->m_coEndUserIMSI;
    if (0 == pco_field->is_null()) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IMSI;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
    /* добавляем Parent-Session-Id (по необходимости) */
    if (NULL != p_pstrOptionalParam) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_PSES;
      CHECK_FCT(pcrf_session_cache_fill_payload (pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, p_pstrOptionalParam->data(), p_pstrOptionalParam->length ()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    }
  } else if (static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSION) == p_uiReqType) {
    /* для удаления сессии достаточно лишь SessionId*/
  } else if (static_cast<uint16_t>(PCRF_CMD_INSERT_SESSRUL) == p_uiReqType
    || static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSRUL) == p_uiReqType) {
    /* для выполнения этой операции необходим атрибут Rule-Name */
    if (NULL != p_pstrOptionalParam) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_RULNM;
      CHECK_FCT(pcrf_session_cache_fill_payload(pso_payload_hdr, sizeof(mc_attr), uiVendId, uiAVPId, p_pstrOptionalParam->data(), p_pstrOptionalParam->length()));
      if (0 < (iRetVal = ps_pack.AddAttr(reinterpret_cast<SPSRequest*>(p_pmcBuf), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_payload_len))) {
      } else {
        return -1;
      }
    } else {
      return -1;
    }
  } else {
    UTL_LOG_E (*g_pcoLog, "unsupported command type: '%#x", static_cast<uint32_t>(p_uiReqType));
    return -1;
  }

  return iRetVal;
}

void pcrf_session_cache_cmd2remote (std::string &p_strSessionId, SSessionCache *p_psoSessionInfo, uint16_t p_uiCmdType, std::string *p_pstrOptionalParam)
{
  char mc_buf[4096];
  int iPayloadLen;
  std::vector<SNode>::iterator iter;

  iPayloadLen = pcrf_session_cache_fill_pspack (mc_buf, sizeof(mc_buf), p_strSessionId, p_psoSessionInfo, p_uiCmdType, p_pstrOptionalParam);
  if (0 < iPayloadLen) {
  } else {
    return;
  }

  /* собираем пакет данных для запроса */
  ssize_t sent;
  sockaddr_in soAddr;
  /* обходим все ноды */
  for (iter = g_pvectNodeList->begin(); iter != g_pvectNodeList->end(); ++iter) {
    soAddr.sin_family = AF_INET;
    if (0 == inet_aton (iter->m_addr.c_str(), &soAddr.sin_addr)) {
      UTL_LOG_E( *g_pcoLog, "inet_aton can not convert address: '%s'", iter->m_addr.c_str());
      continue;
    }
    soAddr.sin_port = htons (iter->m_port);
    sent = sendto (g_send_sock, mc_buf, static_cast<ssize_t>(iPayloadLen), 0, reinterpret_cast<sockaddr*>(&soAddr), sizeof(soAddr));
    if (iPayloadLen == sent) {
    } else {
      int i_errno = errno;
      UTL_LOG_E( *g_pcoLog, "sendto: error accurred: '%s'; error code: '%d'", strerror (i_errno), i_errno);
    }
  }
}

void pcrf_session_cache_insert (otl_value<std::string> &p_coSessionId, SSessionInfo &p_soSessionInfo, SRequestInfo &p_soRequestInfo, std::string *p_pstrParentSessionId)
{
  CTimeMeasurer coTM;
  /* проверяем параметры */
  if (0 == p_coSessionId.is_null()) {
  } else {
    return;
  }

  SSessionCache soTmp;

  /* копируем необходмые данные */
  soTmp.m_coSubscriberId    = p_soSessionInfo.m_strSubscriberId;
  soTmp.m_coFramedIPAddr    = p_soSessionInfo.m_coFramedIPAddress;
  soTmp.m_coCalledStationId = p_soSessionInfo.m_coCalledStationId;
  soTmp.m_coIPCANType       = p_soRequestInfo.m_soUserLocationInfo.m_coIPCANType;
  soTmp.m_iIPCANType        = p_soRequestInfo.m_soUserLocationInfo.m_iIPCANType;
  soTmp.m_coSGSNIPAddr      = p_soRequestInfo.m_soUserLocationInfo.m_coSGSNAddress;
  soTmp.m_coRATType         = p_soRequestInfo.m_soUserLocationInfo.m_coRATType;
  soTmp.m_iRATType          = p_soRequestInfo.m_soUserLocationInfo.m_iRATType;
  soTmp.m_coOriginHost      = p_soSessionInfo.m_coOriginHost;
  soTmp.m_coOriginRealm     = p_soSessionInfo.m_coOriginRealm;
  soTmp.m_coCGI             = p_soRequestInfo.m_soUserLocationInfo.m_coCGI;
  soTmp.m_coECGI            = p_soRequestInfo.m_soUserLocationInfo.m_coECGI;
  soTmp.m_coIMEISV          = p_soSessionInfo.m_coIMEI;
  soTmp.m_coEndUserIMSI     = p_soSessionInfo.m_coEndUserIMSI;

  pcrf_session_cache_insert_local (p_coSessionId.v, soTmp, p_pstrParentSessionId);

  pcrf_session_cache_cmd2remote (p_coSessionId.v, &soTmp, static_cast<uint16_t>(PCRF_CMD_INSERT_SESSION), p_pstrParentSessionId);

  return;
}

int pcrf_session_cache_get (std::string &p_strSessionId, SSessionInfo &p_soSessionInfo, SRequestInfo &p_soRequestInfo)
{
  CTimeMeasurer coTM;

  int iRetVal = 0;
  std::map<std::string,SSessionCache>::iterator iter;
  timespec soTimeSpec;

  /* готовим таймер мьютекса */
  CHECK_FCT_DO(pcrf_make_timespec_timeout(soTimeSpec, MUTEX_RD_WAIT), return errno );
  CHECK_FCT_DO( pthread_mutex_timedlock(&g_mutexSessionCache, &soTimeSpec), stat_measure(g_psoSessionCacheStat, "getting failed", NULL); iRetVal = ETIMEDOUT;  goto clean_and_exit );

  /* запрашиваем информацию о сессии из кеша */
  iter = g_pmapSessionCache->find (p_strSessionId);
  if (iter != g_pmapSessionCache->end ()) {
    stat_measure (g_psoSessionCacheStat, "hit", NULL);
    if (! iter->second.m_coSubscriberId.is_null ()) {
      p_soSessionInfo.m_strSubscriberId = iter->second.m_coSubscriberId.v;
    } else {
      p_soSessionInfo.m_strSubscriberId = "";
    }
    if (p_soSessionInfo.m_coFramedIPAddress.is_null())                  p_soSessionInfo.m_coFramedIPAddress                   = iter->second.m_coFramedIPAddr;
    if (p_soSessionInfo.m_coCalledStationId.is_null())                  p_soSessionInfo.m_coCalledStationId                   = iter->second.m_coCalledStationId;
    if (p_soRequestInfo.m_soUserLocationInfo.m_coIPCANType.is_null())   p_soRequestInfo.m_soUserLocationInfo.m_coIPCANType    = iter->second.m_coIPCANType;
                                                                        p_soRequestInfo.m_soUserLocationInfo.m_iIPCANType     = iter->second.m_iIPCANType;
    if (p_soRequestInfo.m_soUserLocationInfo.m_coSGSNAddress.is_null()) p_soRequestInfo.m_soUserLocationInfo.m_coSGSNAddress  = iter->second.m_coSGSNIPAddr;
    if (p_soRequestInfo.m_soUserLocationInfo.m_coRATType.is_null())     p_soRequestInfo.m_soUserLocationInfo.m_coRATType      = iter->second.m_coRATType;
                                                                        p_soRequestInfo.m_soUserLocationInfo.m_iRATType       = iter->second.m_iRATType;
    if (p_soSessionInfo.m_coOriginHost.is_null())                       p_soSessionInfo.m_coOriginHost                        = iter->second.m_coOriginHost;
    if (p_soSessionInfo.m_coOriginRealm.is_null())                      p_soSessionInfo.m_coOriginRealm                       = iter->second.m_coOriginRealm;
    if (p_soRequestInfo.m_soUserLocationInfo.m_coCGI.is_null())         p_soRequestInfo.m_soUserLocationInfo.m_coCGI          = iter->second.m_coCGI;
    if (p_soRequestInfo.m_soUserLocationInfo.m_coECGI.is_null())        p_soRequestInfo.m_soUserLocationInfo.m_coECGI         = iter->second.m_coECGI;
    if (p_soSessionInfo.m_coIMEI.is_null())                             p_soSessionInfo.m_coIMEI                              = iter->second.m_coIMEISV;
    if (p_soSessionInfo.m_coEndUserIMSI.is_null())                      p_soSessionInfo.m_coEndUserIMSI                       = iter->second.m_coEndUserIMSI;
  } else {
    stat_measure (g_psoSessionCacheStat, "miss", NULL);
    iRetVal = EINVAL;
  }
  /* освобождаем мьютекс */
  CHECK_FCT_DO( pthread_mutex_unlock (&g_mutexSessionCache), /* continue */ );

clean_and_exit:

  return iRetVal;
}

static void pcrf_session_cache_remove_local (std::string &p_strSessionId)
{
  std::map<std::string,SSessionCache>::iterator iter;

  timespec soTimeSpec;

  CHECK_FCT_DO(pcrf_make_timespec_timeout(soTimeSpec, MUTEX_RM_WAIT), return );
  /* дожадаемся освобождения мьютекса */
  CHECK_FCT_DO( pthread_mutex_timedlock (&g_mutexSessionCache, &soTimeSpec), stat_measure(g_psoSessionCacheStat, "removal failed", NULL); goto clean_and_exit );

  pcrf_session_cache_remove_link (p_strSessionId);

  iter = g_pmapSessionCache->find (p_strSessionId);
  if (iter != g_pmapSessionCache->end ()) {
    g_pmapSessionCache->erase (iter);
    stat_measure (g_psoSessionCacheStat, "removed", NULL);
  }

  /* освобождаем семафор */
  CHECK_FCT_DO( pthread_mutex_unlock (&g_mutexSessionCache), /* continue */ );

clean_and_exit:

  return;
}

void pcrf_session_cache_remove (std::string &p_strSessionId)
{
  CTimeMeasurer coTM;

  pcrf_session_cache_remove_local (p_strSessionId);
  pcrf_session_rule_cache_remove_sess_local(p_strSessionId);

  pcrf_session_cache_cmd2remote (p_strSessionId, NULL, static_cast<uint16_t>(PCRF_CMD_REMOVE_SESSION), NULL);
}

static inline int pcrf_session_cache_process_request (const char *p_pmucBuf, const ssize_t p_stMsgLen)
{
  int iRetVal = 0;
  CPSPacket coPSPack;
  uint32_t uiPackNum;
  uint16_t uiReqType;
  uint16_t uiPackLen;
  std::multimap<uint16_t,SPSReqAttrParsed> mmap;
  std::multimap<uint16_t,SPSReqAttrParsed>::iterator iter;
  std::string strSessionId;
  uint8_t *pmucBuf = NULL;
  size_t stBufSize = 0;
  SPayloadHdr *psoPayload;
  SSessionCache soCache;
  std::string *pstrParentSessionId = NULL;
  std::string *pstrRuleName = NULL;

  /* парсинг запроса */
  CHECK_FCT( coPSPack.Parse (reinterpret_cast<const SPSRequest*>(p_pmucBuf), static_cast<size_t>(p_stMsgLen), uiPackNum, uiReqType, uiPackLen, mmap) );

  /* обходим все атрибуты запроса */
  for (iter = mmap.begin(); iter != mmap.end(); ++iter) {
    /* проверяем размр буфера */
    if (stBufSize < iter->second.m_usDataLen) {
      /* выделяем дополнительную память с запасом на будущее */
      stBufSize = iter->second.m_usDataLen * 2;
      if (NULL != pmucBuf) {
        free (pmucBuf);
      }
      pmucBuf = reinterpret_cast<uint8_t*>(malloc (stBufSize));
    }
    psoPayload = reinterpret_cast<SPayloadHdr*>(pmucBuf);
    memcpy (psoPayload, iter->second.m_pvData, iter->second.m_usDataLen);
    switch (psoPayload->m_vend_id) {
    case 0:     /* Dimeter */
      switch (psoPayload->m_avp_id) {
      case 8:   /* Framed-IP-Address */
        soCache.m_coFramedIPAddr.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coFramedIPAddr.set_non_null();
        break;  /* Framed-IP-Address */
      case 30:  /* Called-Station-Id */
        soCache.m_coCalledStationId.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coCalledStationId.set_non_null();
        break;  /* Called-Station-Id */
      case 263: /* Session-Id */
        strSessionId.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        break;  /* Session-Id */
      case 264: /* Origin-Host */
        soCache.m_coOriginHost.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coOriginHost.set_non_null();
        break;  /* Origin-Host */
      case 296: /* Origin-Realm */
        soCache.m_coOriginRealm.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coOriginRealm.set_non_null();
        break;  /* Origin-Realm */
      default:
        UTL_LOG_N( *g_pcoLog, "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_vend_id, psoPayload->m_avp_id );
      }
      break;    /* Diameter */
    case 10415: /* 3GPP */
      switch (psoPayload->m_avp_id) {
      case 6:     /* SGSN-IP-Address */
        soCache.m_coSGSNIPAddr.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coSGSNIPAddr.set_non_null();
        break;    /* SGSN-IP-Address */
      case 1027:  /* IP-CAN-Type */
        if (sizeof(soCache.m_iIPCANType) == psoPayload->m_payload_len - sizeof(*psoPayload)) {
          soCache.m_iIPCANType = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload));
        } else {
          /* invalid data size */
        }
        break;    /* IP-CAN-Type */
      case 1032:  /* RAT-Type */
        if (sizeof(soCache.m_iRATType) == psoPayload->m_payload_len - sizeof(*psoPayload)) {
          soCache.m_iRATType = *reinterpret_cast<int32_t*>(reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload));
        } else {
          /* invalid data size */
        }
        break;    /* RAT-Type */
      default:
        UTL_LOG_N( *g_pcoLog, "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_vend_id, psoPayload->m_avp_id );
      }
      break;    /* 3GPP */
    case 65535: /* Tenet */
      switch (psoPayload->m_avp_id) {
      case PS_SUBSCR: /* Subscriber-Id */
        soCache.m_coSubscriberId.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coSubscriberId.set_non_null();
        break;        /* Subscriber-Id */
      case PCRF_ATTR_CGI: /* CGI */
        soCache.m_coCGI.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coCGI.set_non_null();
        break;            /* CGI */
      case PCRF_ATTR_ECGI:  /* ECGI */
        soCache.m_coECGI.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coECGI.set_non_null();
        break;              /* ECGI */
      case PCRF_ATTR_IMEI:  /* IMEI-SV */
        soCache.m_coIMEISV.v.insert (0, reinterpret_cast<const char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coIMEISV.set_non_null();
        break;              /* IMEI-SV */
      case PCRF_ATTR_IMSI:  /* End-User-IMSI */
        soCache.m_coEndUserIMSI.v.insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coEndUserIMSI.set_non_null();
        break;              /* End-User-IMSI */
      case PCRF_ATTR_PSES:  /* Parent-Session-Id */
        pstrParentSessionId = new std::string;
        pstrParentSessionId->insert (0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        break;              /* Parent-Session-Id */
      case PCRF_ATTR_IPCANTYPE: /* IP-CAN-Type */
        soCache.m_coIPCANType.v.insert(0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coIPCANType.set_non_null();
        break;                  /* IP-CAN-Type */
      case PCRF_ATTR_RATTYPE: /* RAT-Type */
        soCache.m_coRATType.v.insert(0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        soCache.m_coRATType.set_non_null();
        break;                  /* RAT-Type */
      case PCRF_ATTR_RULNM: /* Rule-Name */
        pstrRuleName = new std::string;
        pstrRuleName->insert(0, reinterpret_cast<char*>(psoPayload) + sizeof(*psoPayload), psoPayload->m_payload_len - sizeof(*psoPayload));
        break;                  /* Rule-Name */
      default:
        UTL_LOG_N( *g_pcoLog, "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_vend_id, psoPayload->m_avp_id );
      }
      break;    /* Tenet */
    default:
      UTL_LOG_N( *g_pcoLog, "unsupported vendor: '%u'", psoPayload->m_vend_id );
    }
  }

  /* если Session-Id не задан прерываем обработку */
  if (0 != strSessionId.length ()) {
  } else {
    UTL_LOG_E( *g_pcoLog, "session id not defined" );
    goto clean_and_exit;
  }

  /* выполняем команду */
  switch (uiReqType) {
  case PCRF_CMD_INSERT_SESSION:
    pcrf_session_cache_insert_local (strSessionId, soCache, pstrParentSessionId);
    break;
  case PCRF_CMD_REMOVE_SESSION:
    pcrf_session_cache_remove_local (strSessionId);
    pcrf_session_rule_cache_remove_sess_local(strSessionId);
    break;
  case PCRF_CMD_INSERT_SESSRUL:
    if (NULL != pstrRuleName) {
      pcrf_session_rule_cache_insert_local(strSessionId, *pstrRuleName);
    }
    break;
  case PCRF_CMD_REMOVE_SESSRUL:
    if (NULL != pstrRuleName) {
      pcrf_session_rule_cache_remove_rule_local(strSessionId, *pstrRuleName);
    }
    break;
  default:
    UTL_LOG_N( *g_pcoLog, "unsupported command: '%#x'", uiReqType);
  }

clean_and_exit:
  if (NULL != pmucBuf) {
    free (pmucBuf);
  }
  if (NULL != pstrParentSessionId) {
    delete pstrParentSessionId;
  }
  if (NULL != pstrRuleName) {
    delete pstrRuleName;
  }

  return iRetVal;
}

static void * pcrf_session_cache_receiver (void *p_vParam)
{
  int iRecvSock;
  uint8_t *pmucBuf = NULL;
  sockaddr_in soAddr;
  socklen_t stAddrLen;
  pollfd soPollFD;
  const size_t stBufSize = 0x10000;
  ssize_t stRecv;

  /* предотвращаем предпреждение компилятора */
  p_vParam = p_vParam;

  /* создаем сокет для получения запросов */
  iRecvSock = socket (AF_INET, SOCK_DGRAM, 0);
  if (-1 != iRecvSock) {
  } else {
    goto clean_and_exit;
  }

  /* ждем пока модуль не инициализируется полностью */
  while (! g_module_is_initialized && g_lets_work) {
    sleep (0);
  }

  /* привязываемся к локальному адресу */
  soAddr.sin_family = AF_INET;
  if (0 != inet_aton (g_strLocalIPAddress.c_str(), &soAddr.sin_addr)) {
  } else {
    UTL_LOG_E( *g_pcoLog, "inet_aton can not convert local address: '%s'", g_strLocalIPAddress.c_str());
    goto clean_and_exit;
  }
  soAddr.sin_port = htons (g_uiLocalPort);
  CHECK_FCT_DO( bind (iRecvSock, reinterpret_cast<sockaddr*>(&soAddr), sizeof(soAddr)), goto clean_and_exit );

  pmucBuf = reinterpret_cast<uint8_t*> (malloc (stBufSize));
  while (g_lets_work) {
    soPollFD.fd = iRecvSock;
    soPollFD.events = POLLIN;
    soPollFD.revents = 0;
    if (1 == poll (&soPollFD, 1, POLL_TIMEOUT)) {
      if (soPollFD.revents | POLLIN) {
        stAddrLen = sizeof(soAddr);
        stRecv = recvfrom (iRecvSock, pmucBuf, stBufSize, 0, reinterpret_cast<sockaddr*>(&soAddr), &stAddrLen);
        if (sizeof(soAddr) >= stAddrLen) {
          /* в этом случае мы имеем корректно сформированный адрес пира */
          /* использование inet_ntoa в этом случае не опасно, т.к. обрабатываем по одному пакету за цикл */
        }
        if (0 < stRecv) {
          pcrf_session_cache_process_request (reinterpret_cast<char*>(pmucBuf), stRecv);
        }
      }
    }
  }

clean_and_exit:
  if (-1 != iRecvSock) {
    shutdown (iRecvSock, SHUT_RDWR);
    close (iRecvSock);
  }
  if (NULL != pmucBuf) {
    free (pmucBuf);
  }

  pthread_exit (NULL);
}

static int pcrf_session_cache_init_node ()
{
  g_send_sock = socket (AF_INET, SOCK_DGRAM, 0);
  if (-1 != g_send_sock) {
  } else {
    return errno;
  }

  otl_connect *pcoConn = NULL;
  if (0 == pcrf_db_pool_get(&pcoConn, __FUNCTION__, 10) && NULL != pcoConn) {
  } else {
    return -1;
  }

  try {
    SNode soNode;
    otl_value<std::string>  coHost;
    otl_value<std::string>  coReal;
    otl_value<std::string>  coIPAddress;
    otl_value<uint32_t>     coPort;

    otl_nocommit_stream coStream;
    coStream.open (
      10,
      "select host_name, realm, ip_address, port from ps.node",
      *pcoConn);
    while (! coStream.eof ()) {
      coStream
        >> coHost
        >> coReal
        >> coIPAddress
        >> coPort;
      UTL_LOG_D(*g_pcoLog, "node information loaded: host: '%s'; realm: '%s'; ip-address: '%s'; port: '%u'", coHost.v.c_str(), coReal.v.c_str(), coIPAddress.v.c_str(), coPort.v );
      if (! coHost.is_null () && coHost.v.length() == fd_g_config->cnf_diamid_len && 0 == coHost.v.compare (0, fd_g_config->cnf_diamid_len, reinterpret_cast<const char*>(fd_g_config->cnf_diamid))
        && ! coReal.is_null() && coReal.v.length() == fd_g_config->cnf_diamrlm_len && 0 == coReal.v.compare (0, fd_g_config->cnf_diamrlm_len, reinterpret_cast<const char*>(fd_g_config->cnf_diamrlm))) {
          if (! coIPAddress.is_null ()) {
            g_strLocalIPAddress = coIPAddress.v;
          }
          if (! coPort.is_null ()) {
            g_uiLocalPort = static_cast<uint16_t>(coPort.v);
          }
          UTL_LOG_D(*g_pcoLog, "home node detected: set local ip-address to '%s'; set local port to '%u'", g_strLocalIPAddress.c_str(), g_uiLocalPort );
          continue;
      }
      if (! coHost.is_null()) {
        soNode.m_diamid = strdup (coHost.v.c_str());
        soNode.m_diamid_len = coHost.v.length();
      } else {
        soNode.m_diamid = NULL;
        soNode.m_diamid_len = 0;
      }
      if (! coReal.is_null ()) {
        soNode.m_diamrlm = strdup (coReal.v.c_str());
        soNode.m_diamrlm_len = coReal.v.length();
      } else {
        soNode.m_diamrlm = NULL;
        soNode.m_diamrlm_len = 0;
      }
      if (! coIPAddress.is_null ()) {
        soNode.m_addr = coIPAddress.v;
      } else {
        soNode.m_addr = "";
      }
      if (! coPort.is_null ()) {
        soNode.m_port = coPort.v;
      } else {
        soNode.m_port = 0;
      }
      g_pvectNodeList->push_back (soNode);
    }
  } catch (otl_exception &coExept) {
    UTL_LOG_F (*g_pcoLog, "can not load node list: error: '%d'; description: '%s'", coExept.code, coExept.msg);
  }

  if (NULL != pcoConn) {
    pcrf_db_pool_rel (pcoConn, __FUNCTION__);
  }

  return 0;
}

static void pcrf_session_cache_fini_node ()
{
  if (-1 != g_send_sock) {
    shutdown (g_send_sock, SHUT_RDWR);
    close (g_send_sock);
    g_send_sock = -1;
  }

  std::vector<SNode>::iterator iter = g_pvectNodeList->begin();
  for (; iter != g_pvectNodeList->end (); ++iter) {
    if (NULL != iter->m_diamid) {
      free (iter->m_diamid);
      iter->m_diamid = NULL;
    }
    if (NULL != iter->m_diamrlm) {
      free (iter->m_diamrlm);
      iter->m_diamrlm = NULL;
    }
  }
}

static void * pcrf_session_cache_load_session_list(void *p_pArg)
{
  int iRetVal = 0;
  CTimeMeasurer coTM;
  otl_connect *pcoDBConn = NULL;

  if (0 == pcrf_db_pool_get(&pcoDBConn, __FUNCTION__, 10) && NULL != pcoDBConn) {
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
        "sloc.sgsn_ip_address,"
        "sloc.rat_type,"
        "sl.origin_host,"
        "sl.origin_realm,"
        "sloc.cgi,"
        "sloc.ecgi,"
        "sl.IMEISV,"
        "sl.end_user_imsi "
      "from "
        "ps.sessionList sl "
        "inner join ps.peer p on sl.origin_host = p.host_name and sl.origin_realm = p.realm "
        "left join ps.sessionLocation sloc on sl.session_id = sloc.session_id "
      "where "
        "sl.time_end is null "
        "and sloc.time_end is null "
        "and p.protocol_id = :protocol_id/*int*/",
      *pcoDBConn);
    coStream
      << GX_3GPP;
    while (0 == coStream.eof()) {
      {
        std::string strSessionId;
        SSessionCache soSessCache;

        coStream
          >> strSessionId
          >> soSessCache.m_coSubscriberId
          >> soSessCache.m_coFramedIPAddr
          >> soSessCache.m_coCalledStationId
          >> soSessCache.m_coIPCANType
          >> soSessCache.m_coSGSNIPAddr
          >> soSessCache.m_coRATType
          >> soSessCache.m_coOriginHost
          >> soSessCache.m_coOriginRealm
          >> soSessCache.m_coCGI
          >> soSessCache.m_coECGI
          >> soSessCache.m_coIMEISV
          >> soSessCache.m_coEndUserIMSI;
        if (0 == soSessCache.m_coIPCANType.is_null()) {
          dict_object * enum_obj = NULL;
          dict_enumval_request req;
          memset(&req, 0, sizeof(struct dict_enumval_request));

          /* First, get the enumerated type of the IP-CAN-Type AVP (this is fast, no need to cache the object) */
          CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictIPCANType, &(req.type_obj), ENOENT), continue);

          /* Now search for the value given as parameter */
          req.search.enum_name = const_cast<char*>(soSessCache.m_coIPCANType.v.c_str());
          CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP), continue);

          /* finally retrieve its data */
          CHECK_FCT_DO(fd_dict_getval(enum_obj, &(req.search)), continue);

          /* copy the found value, we're done */
          soSessCache.m_iIPCANType = req.search.enum_value.i32;
          LOG_D("%s: IP-CAN-Type: '%d', '%s'", __FUNCTION__, soSessCache.m_iIPCANType, soSessCache.m_coIPCANType.v.c_str());
        }
        if (0 == soSessCache.m_coRATType.is_null()) {
          dict_object * enum_obj = NULL;
          dict_enumval_request req;
          memset(&req, 0, sizeof(struct dict_enumval_request));

          /* First, get the enumerated type of the RAT-Type AVP (this is fast, no need to cache the object) */
          CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, g_psoDictRATType, &(req.type_obj), ENOENT), continue);

          /* Now search for the value given as parameter */
          req.search.enum_name = const_cast<char*>(soSessCache.m_coRATType.v.c_str());
          CHECK_FCT_DO(fd_dict_search(fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &req, &enum_obj, ENOTSUP), continue);

          /* finally retrieve its data */
          CHECK_FCT_DO(fd_dict_getval(enum_obj, &(req.search)), continue);

          /* copy the found value, we're done */
          soSessCache.m_iRATType = req.search.enum_value.i32;
          LOG_D("%s: RAT-Type: '%d', '%s'", __FUNCTION__, soSessCache.m_iRATType, soSessCache.m_coRATType.v.c_str());
        }
        pcrf_session_cache_insert_local(strSessionId, soSessCache, NULL, true);
      }
    }
    {
      char mcDuration[128];
      if (0 == coTM.GetDifference(NULL, mcDuration, sizeof(mcDuration))) {
        UTL_LOG_N(*g_pcoLog, "session list is loaded in '%s'; session count: '%u'", mcDuration, g_pmapSessionCache->size());
      }
    }
  } catch (otl_exception &coExcept) {
    UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
    iRetVal = coExcept.code;
  }

clean_and_exit:
  if (NULL != pcoDBConn) {
    pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
    pcoDBConn = NULL;
  }

  pthread_exit(NULL);
}
