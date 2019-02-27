#ifndef __PCRF_SESSION_CACHE_INDEX_H__
#define __PCRF_SESSION_CACHE_INDEX_H__

#include <string>
#include <list>

#include "pcrf_session_cache.h"

/* функции для работы с индесами по Framed-IP-Address */
int pcrf_session_cache_index_frameIPAddress_insert_session( std::string &p_strFramedIPAddress, std::string &p_strSessionId );
int pcrf_session_cache_index_frameIPAddress_remove_session( std::string &p_strFramedIPAddress, std::string &p_strSessionId );

/* получение списка session-id по Framed-IP-Address */
int pcrf_session_cache_index_frameIPAddress_get_sessionList( std::string &p_strFramedIPAddress, std::list<std::string> &p_listSessionId );

/* поиск IPCAN-сессии для загрузки данных для SCE */
int pcrf_server_find_core_session( std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strIPCANSessionId );

/* функции для работы с индексами по subscriber-id */
void pcrf_session_cache_index_subscriberId_insert_session( std::string &p_strSubscriberId, std::string &p_strSessionId );
int pcrf_session_cache_get_subscriber_session_id( std::string &p_strSubscriberId, std::vector<std::string> &p_vectSessionId );
void pcrf_session_cache_rm_subscriber_session_id( std::string &p_strSubscriberId, std::string &p_strSessionId );

/* функции для работы с индексами по IP-CAN Session-Id */
void pcrf_session_cache_mk_link2parent( std::string &p_strSessionId, std::string *p_pstrParentSessionId );
void pcrf_session_cache_remove_link( std::string &p_strSessionId );
void pcrf_session_cache_update_child( std::string &p_strSessionId, SSessionCache *p_psoSessionInfo );
int  pcrf_session_cache_get_linked_child_session_list( std::string &p_strSessionId, std::list<std::string> &p_listSessionId );
int  pcrf_session_cache_get_linked_parent_session( std::string &p_strSessionId, std::string &p_strParentSessionId );

void pcrf_session_cache_index_provide_stat_cb( char **p_ppszStat );

#endif /* __PCRF_SESSION_CACHE_INDEX_H__ */
