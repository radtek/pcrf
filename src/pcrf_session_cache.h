#ifndef __PCRF_SESSION_CACHE_H__
#define __PCRF_SESSION_CACHE_H__

#include <string>
#include <list>

#include "app_pcrf_header.h"
#include "pcrf_otl.h"

struct SSessionCache {
  otl_value<std::string>  m_coSubscriberId;
  otl_value<std::string>  m_coFramedIPAddr;
  otl_value<std::string>  m_coCalledStationId;
  otl_value<std::string>  m_coIPCANType;
  otl_value<std::string>  m_coSGSNMCCMNC;
  otl_value<std::string>  m_coSGSNIPAddr;
  otl_value<std::string>  m_coRATType;
  otl_value<std::string>  m_coOriginHost;
  otl_value<std::string>  m_coOriginRealm;
  otl_value<std::string>  m_coCGI;
  otl_value<std::string>  m_coECGI;
  otl_value<std::string>  m_coTAI;
  otl_value<std::string>  m_coIMEISV;
  otl_value<std::string>  m_coEndUserIMSI;
  otl_value<std::string>  m_coEndUserE164;
  int32_t                 m_iIPCANType;
  int32_t                 m_iRATType;
  SSessionCache() : m_iIPCANType( 0 ), m_iRATType( 0 ) { }
  ~SSessionCache() { }
};

/* добавление данных о сессии в кеш */
void pcrf_session_cache_insert( std::string &p_strSessionId, SSessionInfo &p_soSessionInfo, SRequestInfo *p_psoRequestInfo, std::string *p_pstrParentSessionId );
/* загрузка данных о сессии из кеша */
int pcrf_session_cache_get( std::string &p_strSessionId, SSessionInfo *p_psoSessionInfo, SRequestInfo *p_psoRequestInfo, std::string *p_pstrParentSessionId );
/* удаление данных из кеша */
void pcrf_session_cache_remove( std::string &p_strSessionId );
/* получение списка session-id по Framed-IP-Address */
int pcrf_session_cache_index_frameIPAddress_get_sessionList( std::string &p_strFramedIPAddress, std::list<std::string> &p_listSessionId );
/* функции блоикровки/разблокировки кеша сессий */
int pcrf_session_cache_lock();
void pcrf_session_cache_unlock();

void pcrf_session_cache_insert_local( std::string &p_strSessionId, SSessionCache *p_psoSessionInfo, std::string *p_pstrParentSessionId, int p_iPeerDialect );
int  pcrf_session_cache_remove_local( std::string &p_strSessionId );

void pcrf_session_cache_get_info_from_cache( std::string &p_strSessionId, std::string &p_strResult );

void pcrf_session_cache_update_some_values( std::string &p_strSessionId, const SSessionCache *p_psoSomeNewInfo );

#endif /* __PCRF_SESSION_CACHE_H__ */
