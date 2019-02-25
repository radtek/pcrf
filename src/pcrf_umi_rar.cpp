#include "app_pcrf_header.h"
#include "pcrf_session_cache.h"
#include "pcrf_session_cache_index.h"

int pcrf_send_umi_rar( otl_value<std::string> &p_coSubscriberId, std::list<std::string> *p_plistMonitKey )
{
  if ( 0 == p_coSubscriberId.is_null() && NULL != p_plistMonitKey ) {
  } else {
    return EINVAL;
  }
  LOG_D( "enter: %s; subscriber-id: %s; monit-key count: %u", __FUNCTION__, p_coSubscriberId.v.c_str(), p_plistMonitKey->size() );

  int iRetVal = 0;
  std::vector<std::string> vectSessionList;

  CHECK_FCT_DO( ( iRetVal = pcrf_session_cache_get_subscriber_session_id( p_coSubscriberId.v, vectSessionList ) ), return iRetVal );
  LOG_D( "session count: %u", vectSessionList.size() );

  SMsgDataForDB soInfo;

  pcrf_server_DBstruct_init( &soInfo );

  /* обходим все сессии */
  for ( int i = 0; i < vectSessionList.size(); ++i ) {
    soInfo.m_psoReqInfo->m_vectUsageInfo.clear();
    LOG_D( "session-id: %s", vectSessionList[ i ].c_str() );
    pcrf_session_cache_get( vectSessionList[ i ], soInfo.m_psoSessInfo, soInfo.m_psoReqInfo, NULL );
    soInfo.m_psoSessInfo->m_strSessionId = vectSessionList[ i ];
    CHECK_POSIX_DO( ( iRetVal = pcrf_peer_dialect( *soInfo.m_psoSessInfo ) ), goto clean_and_exit );
    for ( std::list<std::string>::iterator iter = p_plistMonitKey->begin(); iter != p_plistMonitKey->end(); ++ iter ) {
      SDBMonitoringInfo soMonitInfo;
      soInfo.m_psoSessInfo->m_mapMonitInfo.insert( std::pair<std::string, SDBMonitoringInfo>( *iter, soMonitInfo ) );
    }
    CHECK_FCT_DO( ( iRetVal = pcrf_client_gx_rar( soInfo.m_psoSessInfo, soInfo.m_psoReqInfo, NULL, NULL, NULL, NULL, 0 ) ), /* continue */ );
  }


  clean_and_exit:
  pcrf_server_DBStruct_cleanup( &soInfo );

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}
