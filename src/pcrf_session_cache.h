#ifndef __PCRF_SESSION_CACHE_H__
#define __PCRF_SESSION_CACHE_H__

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
  int32_t                 m_iIPCANType;
  int32_t                 m_iRATType;
  SSessionCache() : m_iIPCANType( 0 ), m_iRATType( 0 ) { }
};

void pcrf_session_cache_update_some_values( std::string &p_strSessionId, const SSessionCache *p_psoSomeNewInfo );

#endif /* __PCRF_SESSION_CACHE_H__ */
