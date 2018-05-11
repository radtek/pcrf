#ifndef __PCRF_SESSION_CACHE_H__
#define __PCRF_SESSION_CACHE_H__

#include <string>
#include <list>

int pcrf_session_cache_index_frameIPAddress_insert_session( std::string &p_strFramedIPAddress, std::string &p_strSessionId );
int pcrf_session_cache_index_frameIPAddress_remove_session( std::string &p_strFramedIPAddress, std::string &p_strSessionId );

#endif /* __PCRF_SESSION_CACHE_H__ */
