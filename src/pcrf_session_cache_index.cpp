#include <map>
#include <set>

#include "pcrf_session_cache_index.h"
#include "app_pcrf_header.h"
#include "utils/log/log.h"

extern CLog *g_pcoLog;

/* в качестве ключа используется ip-адрес, в качестве значений - список идентификаторов сессий */
static std::map<std::string, std::list<std::string> > g_mapFramedIPIndex;

/* индекс хранилища по IP-CAN Session-Id */
/* ключ - IP-CAN Session-Id, значение - список AF Session-Id */
static std::map<std::string, std::list<std::string> > g_mapParent;
/* ключ - AF Session-Id, значение - IP-CAN Session-Id */
/* индекс хранилища по AF Session-Id */
static std::map<std::string, std::string> g_mapChild;

/* индекс хранилища сессий по subscriber-id */
/* ключ - subscriber-id, значение - список Session-Id */
static std::map<std::string, std::set<std::string> > g_mapSubscriberId;

/* список идентификаторов сессий по ip-адресу */
int pcrf_session_cache_index_frameIPAddress_get_sessionList( std::string &p_strFramedIPAddress, std::list<std::string> &p_listSessionId )
{
  int iRetVal = 0;
  std::map<std::string, std::list<std::string> >::iterator iterList;

  CHECK_FCT( pcrf_session_cache_lock() );

  iterList = g_mapFramedIPIndex.find( p_strFramedIPAddress );

  if ( iterList != g_mapFramedIPIndex.end() ) {
    p_listSessionId = iterList->second;
    LOG_D( "Framed-IP-Address index: %u SessionId-s retreived", p_listSessionId.size() );
  } else {
    LOG_D( "Framed-IP-Address index: Framed-IP-Address %s not found", p_strFramedIPAddress.c_str() );
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
    LOG_D( "Framed-IP-Address index: appended: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
  } else {
    std::list<std::string> listSessionIdList;

    listSessionIdList.push_back( p_strSessionId );
    g_mapFramedIPIndex.insert( std::pair<std::string, std::list<std::string> >( p_strFramedIPAddress, listSessionIdList ) );
    LOG_D( "Framed-IP-Address index: inserted: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
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
        LOG_D( "Framed-IP-Address index: removed: Framed-IP-Address: %s; Session-Id: %s", p_strFramedIPAddress.c_str(), p_strSessionId.c_str() );
      } else {
        ++iterSessList;
      }
    }
  } else {
    LOG_D( "Framed-IP-Address index: framedIPAddress %s is not found" );
  }

  return iRetVal;
}

int pcrf_server_find_core_session( std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strIPCANSessionId )
{
	int iRetVal = 0;
	SSessionInfo soSessInfo;

	if( 0 == pcrf_server_find_core_sess_byframedip( p_strFramedIPAddress, soSessInfo ) ) {
	} else {
		return ENODATA;
	}

	if( 0 == p_strSubscriberId.compare( soSessInfo.m_strSubscriberId ) ) {
		p_strIPCANSessionId.assign( soSessInfo.m_strSessionId );
	} else {
		p_strIPCANSessionId.clear();
		iRetVal = ENODATA;
	}

	return iRetVal;
}

void pcrf_session_cache_update_child( std::string &p_strSessionId, SSessionCache *p_psoSessionInfo )
{
  std::map<std::string, std::list<std::string> >::iterator iterParent = g_mapParent.find( p_strSessionId );
  std::list<std::string>::iterator iterList;

  if ( iterParent != g_mapParent.end() ) {
    for ( iterList = iterParent->second.begin(); iterList != iterParent->second.end(); ++iterList ) {
      pcrf_session_cache_update_some_values( *iterList, p_psoSessionInfo );
      LOG_D( "%s: parent session-id: %s; linked session-id: %s", __FUNCTION__, p_strSessionId.c_str(), iterList->c_str() );
    }
  }
}

static inline void pcrf_session_cache_rm_parent2child_link( std::string &p_strSessionId, std::string &p_strParentSessionId )
{
  std::map<std::string, std::list<std::string> >::iterator iter = g_mapParent.find( p_strParentSessionId );
  if ( iter != g_mapParent.end() ) {
    for ( std::list<std::string>::iterator iterLst = iter->second.begin(); iterLst != iter->second.end(); ) {
      if ( *iterLst == p_strSessionId ) {
        iterLst = iter->second.erase( iterLst );
        LOG_D( "session link: unlink: session-id: %s; parent session-id: %s", p_strSessionId.c_str(), p_strParentSessionId.c_str() );
      } else {
        ++iterLst;
      }
    }
  } else {
    LOG_D( "session link: unlink: parent session-id: %s is not found", p_strParentSessionId.c_str() );
  }
}

static inline void pcrf_session_cache_mk_parent2child( std::string &p_strSessionId, std::string &p_strParentSessionId )
{
  std::map<std::string, std::list<std::string> >::iterator iter = g_mapParent.find( p_strParentSessionId );

  if ( iter != g_mapParent.end() ) {
    iter->second.push_back( p_strSessionId );
    LOG_D( "session link: parent to child link created: parent session-id: %s; session-id: %s", p_strParentSessionId.c_str(), p_strSessionId.c_str() );
  } else {
    std::list<std::string> lst;
    lst.push_back( p_strSessionId );
    g_mapParent.insert( std::pair<std::string, std::list<std::string> >( p_strParentSessionId, lst ) );
    LOG_D( "session link: parent to child link inserted: parent session-id: %s; session-id: %s", p_strParentSessionId.c_str(), p_strSessionId.c_str() );
  }
}

void pcrf_session_cache_mk_link2parent( std::string &p_strSessionId, std::string *p_pstrParentSessionId )
{
  if ( NULL != p_pstrParentSessionId ) {
  } else {
    return;
  }
  /* создаем линк к дочернему элементу */
  pcrf_session_cache_mk_parent2child( p_strSessionId, *p_pstrParentSessionId );

  /* создаем линк к родителю */
  std::pair<std::map<std::string, std::string>::iterator, bool> pair;
  pair = g_mapChild.insert( std::pair<std::string, std::string>( p_strSessionId, *p_pstrParentSessionId ) );
  /* если связка уже существует */
  if ( ! pair.second ) {
    /* если отношения между родительской и дочерней сессией не изменились */
    if ( pair.first != g_mapChild.end() ) {
      if ( pair.first->second == ( *p_pstrParentSessionId ) ) {
        LOG_D( "session lik: it passed the same child to parent pair: session-id: %s; parent session-id: %s", p_strSessionId.c_str(), p_pstrParentSessionId->c_str() );
      } else {
        LOG_D( "session lik: child to parent: session-id: %s; previous parent session-id: %s; new parent session-id: %s", p_strSessionId.c_str(), pair.first->second.c_str(), p_pstrParentSessionId->c_str() );
        pcrf_session_cache_rm_parent2child_link( p_strSessionId, pair.first->second );
        pair.first->second = *p_pstrParentSessionId;
        pcrf_session_cache_mk_parent2child( p_strSessionId, *p_pstrParentSessionId );
      }
    } else {
      LOG_E( "session link: insertion of child to parent link failed: map: size: '%u'; max size: '%u'", g_mapChild.size(), g_mapChild.max_size() );
    }
  } else {
    LOG_D( "session link: child to parent link inserted: session-id: %s; parent session-id: %s", p_strSessionId.c_str(), p_pstrParentSessionId->c_str() );
  }
}

void pcrf_session_cache_remove_link( std::string &p_strSessionId )
{
  std::map<std::string, std::list<std::string> >::iterator iterParent;
  std::list<std::string>::iterator iterList;
  std::map<std::string, std::string>::iterator iterChild;

  iterParent = g_mapParent.find( p_strSessionId );
  /* если сессия родительская */
  if ( iterParent != g_mapParent.end() ) {
    /* обходим список всех дочерних сессий */
    for ( iterList = iterParent->second.begin(); iterList != iterParent->second.end(); ++iterList ) {
      /* ищем и удаляем дочернюю сессию */
      iterChild = g_mapChild.find( *iterList );
      if ( iterChild != g_mapChild.end() && iterChild->second == p_strSessionId ) {
        LOG_D( "session link: child to parent link removed: session-id: %s; parent session-id: %s", iterChild->first.c_str(), iterChild->second.c_str() );
        g_mapChild.erase( iterChild );
      }
    }
    LOG_D( "session link: parent session removed: parent session-id: %s", iterParent->first.c_str() );
    g_mapParent.erase( iterParent );
  } else {
    iterChild = g_mapChild.find( p_strSessionId );
    /* если сессия дочерняя */
    if ( iterChild != g_mapChild.end() ) {
      /* удаляем связку родительской сессии */
      pcrf_session_cache_rm_parent2child_link( p_strSessionId, iterChild->second );
      /* удаляем связку с родетельской сессией */
      LOG_D( "session link: child to parent link removed: session-id: %s; parent session-id: %s", iterChild->first.c_str(), iterChild->second.c_str() );
      g_mapChild.erase( iterChild );
    }
  }
}

int pcrf_session_cache_get_linked_child_session_list( std::string &p_strSessionId, std::list<std::string> &p_listSessionId )
{
  int iRetVal = 0;
  std::map<std::string, std::list<std::string> >::iterator iter;

  CHECK_FCT( pcrf_session_cache_lock() );

  iter = g_mapParent.find( p_strSessionId );
  if ( iter != g_mapParent.end() ) {
    /* копируем список */
    p_listSessionId = iter->second;
    LOG_D( "%s: in cache: %u; in parameter: %u", __FUNCTION__, iter->second.size(), p_listSessionId.size() );
  } else {
    LOG_D( "%s: no data found: session-id: %u", __FUNCTION__, p_strSessionId.size() );
    iRetVal = ENODATA;
  }

  pcrf_session_cache_unlock();

  return iRetVal;
}

int pcrf_session_cache_get_linked_parent_session( std::string &p_strSessionId, std::string &p_strParentSessionId )
{
  int iRetVal = 0;
  std::map<std::string, std::string>::iterator iter;

  iter = g_mapChild.find( p_strSessionId );
  if ( iter != g_mapChild.end() ) {
    p_strParentSessionId = iter->second;
    LOG_D( "session link: session-id: %s; parent session-id: %s", p_strSessionId.c_str(), p_strParentSessionId.c_str() );
  } else {
    iRetVal = ENODATA;
    LOG_D( "session link: session-id: %s; parent session-id: not found", p_strSessionId.c_str() );
  }

  return iRetVal;
}

void pcrf_session_cache_index_subscriberId_insert_session( std::string &p_strSubscriberId, std::string &p_strSessionId )
{
  std::set<std::string> setSessionIdList;
  std::pair<std::map<std::string, std::set<std::string> >::iterator, bool> insertSubscrIdRes;

  setSessionIdList.insert( p_strSessionId );
  insertSubscrIdRes = g_mapSubscriberId.insert( std::pair < std::string, std::set<std::string> >( p_strSubscriberId, setSessionIdList ) );
  if ( insertSubscrIdRes.second ) {
    /* если создана новая запись */
  } else {
    /* если запись уже существует */
    insertSubscrIdRes.first->second.insert( p_strSessionId );
  }
}

int pcrf_session_cache_get_subscriber_session_id( std::string &p_strSubscriberId, std::vector<std::string> &p_vectSessionId )
{
  LOG_D( "enter: %s; subscriber-id: %s", __FUNCTION__, p_strSubscriberId.c_str() );

  int iRetVal = 0;
  std::map<std::string, std::set<std::string> >::iterator iter;

  CHECK_FCT( pcrf_session_cache_lock() );

  iter = g_mapSubscriberId.find( p_strSubscriberId );

  if ( iter != g_mapSubscriberId.end() ) {
    for ( std::set<std::string>::iterator iterList = iter->second.begin(); iterList != iter->second.end(); ++ iterList ) {
      p_vectSessionId.push_back( *iterList );
    }
    LOG_D( "session list size: %d", p_vectSessionId.size() );
  } else {
    iRetVal = ENODATA;
  }

  pcrf_session_cache_unlock();

  LOG_D( "leave: %s; result code: %d", __FUNCTION__, iRetVal );

  return iRetVal;
}

void pcrf_session_cache_rm_subscriber_session_id( std::string &p_strSubscriberId, std::string &p_strSessionId )
{
  std::map<std::string, std::set<std::string> >::iterator iter = g_mapSubscriberId.find( p_strSubscriberId );

  if ( iter != g_mapSubscriberId.end() ) {
    std::set<std::string>::iterator iterSessId = iter->second.find( p_strSessionId );
    if ( iterSessId != iter->second.end() ) {
      iter->second.erase( iterSessId );
    }
  }
}

void pcrf_session_cache_index_provide_stat_cb( char **p_ppszStat )
{
  std::string strStat;
  char mcStat[ 256 ];
  int iFnRes;

  CHECK_FCT_DO( pcrf_session_cache_lock(), return );
  iFnRes = snprintf( mcStat, sizeof( mcStat ), "index by Framed-IP-Address has %u members", g_mapFramedIPIndex.size() );
  if ( 0 < iFnRes && sizeof( mcStat ) > iFnRes ) {
    strStat += mcStat;
  }
  iFnRes = snprintf( mcStat, sizeof( mcStat ), "index by Subscriber-Id has %u members", g_mapSubscriberId.size() );
  if ( 0 < iFnRes && sizeof( mcStat ) > iFnRes ) {
    strStat += "\r\n";
    strStat += mcStat;
  }
  pcrf_session_cache_unlock();

  iFnRes = asprintf( p_ppszStat, "%s", strStat.c_str() );
  if ( 0 < iFnRes ) {
  } else {
    *p_ppszStat = NULL;
  }
}
