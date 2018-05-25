#include "app_pcrf_header.h"
#include "pcrf_session_cache_index.h"

#include <map>

/* в качестве ключа используется ip-адрес, в качестве значений - список идентификаторов сессий */
static std::map<std::string, std::list<std::string> > g_mapFramedIPIndex;

/* список идентификаторов сессий по ip-адресу */
int pcrf_session_cache_index_frameIPAddress_get_sessionList( std::string &p_strFramedIPAddress, std::list<std::string> &p_listSessionId )
{
  int iRetVal = 0;
  std::map<std::string, std::list<std::string> >::iterator iterList;

  CHECK_FCT( pcrf_session_cache_lock() );

  iterList = g_mapFramedIPIndex.find( p_strFramedIPAddress );

  if ( iterList != g_mapFramedIPIndex.end() ) {
    p_listSessionId = iterList->second;
    LOG_D( "%u SessionId-s retreived", p_listSessionId.size() );
  }

  pcrf_session_cache_unlock();

  return iRetVal;
}

int pcrf_session_cache_index_frameIPAddress_insert_session(std::string &p_strFramedIPAddress, std::string &p_strSessionId)
{
  int iRetVal = 0;
  std::map<std::string, std::list<std::string> >::iterator iterList;

  iterList = g_mapFramedIPIndex.find( p_strFramedIPAddress );
  if ( iterList != g_mapFramedIPIndex.end() ) {
    iterList->second.push_back( p_strSessionId );
    LOG_D( "appended: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
  } else {
    std::list<std::string> listSessionIdList;

    listSessionIdList.push_back( p_strSessionId );
    g_mapFramedIPIndex.insert( std::make_pair<std::string, std::list<std::string> >( p_strFramedIPAddress, listSessionIdList ) );
    LOG_D( "inserted: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
  }

  return iRetVal;
}

int pcrf_session_cache_index_frameIPAddress_remove_session( std::string &p_strFramedIPAddress, std::string &p_strSessionId )
{
  int iRetVal = 0;

  std::map<std::string, std::list<std::string> >::iterator iterList;

  iterList = g_mapFramedIPIndex.find( p_strFramedIPAddress );
  if ( iterList != g_mapFramedIPIndex.end() ) {
    std::list<std::string>::iterator iterSessList;

    for ( iterSessList = iterList->second.begin(); iterSessList != iterList->second.end(); ) {
      if ( 0 == p_strSessionId.compare( *iterSessList ) ) {
        iterSessList = iterList->second.erase( iterSessList );
        LOG_D( "removed: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
      } else {
        ++iterSessList;
      }
    }
  } else {
    LOG_D( "framedIPAddress %s is not found in session cache index" );
  }

  return iRetVal;
}
