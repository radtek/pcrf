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

  if ( 0 == pcrf_session_cache_get_linked_session_list( p_strSessionId, listSessionList ) ) {
    std::list<std::string>::iterator iter = listSessionList.begin();
    if ( 0 == pcrf_session_cache_get( *iter, soSessionInfo, NULL ) ) {
      switch ( soSessionInfo.m_uiPeerDialect ) {
        case GX_PROCERA:
           pcrf_procera_terminate_session( *iter );
          break;
        case RX_IMS:
          app_rx_send_asr( soSessionInfo, AC_BEARER_RELEASED );
          break;
      }
    }
  }
}
