#include <list>

#include "pcrf_linked_session.h"
#include "app_pcrf_header.h"
#include "pcrf_session_cache.h"
#include "pcrf_session_cache_index.h"

#include "pcrf_procera.h"

#include "../dict_rx/dict_rx.h"
#include "../app_rx/app_rx_as.h"

void pcrf_terminate_linked_session( std::string &p_strSessionId )
{
  std::list<std::string> listSessionList;
  SSessionInfo soSessionInfo;

  if ( 0 == pcrf_session_cache_get_linked_child_session_list( p_strSessionId, listSessionList ) ) {
    std::list<std::string>::iterator iter = listSessionList.begin();
    for ( ; iter != listSessionList.end(); ++iter ) {
      if ( 0 == pcrf_session_cache_get( *iter, &soSessionInfo, NULL, NULL ) ) {
        CHECK_FCT_DO( pcrf_peer_dialect( soSessionInfo ), continue );
        switch ( soSessionInfo.m_uiPeerDialect ) {
          case GX_PROCERA:
             pcrf_procera_terminate_session( *iter );
            break;
          case RX_IMS:
            LOG_N(
              "%s: Session-Id: %s; Origin-Host: %s; Origin-Realm: %s; IPCAN-Session-Id: %s",
              __FUNCTION__, soSessionInfo.m_strSessionId.c_str(), soSessionInfo.m_coOriginHost.v.c_str(), soSessionInfo.m_coOriginRealm.v.c_str(), p_strSessionId.c_str() );
            app_rx_send_asr( soSessionInfo, AC_BEARER_RELEASED );
            break;
        }
      }
    }
  }
}
