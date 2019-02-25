#ifndef __PCRF_LINKED_SESSION_H__
#define __PCRF_LINKED_SESSION_H__

#include <string>
#include <vector>

#include "app_pcrf_header.h"

void pcrf_linked_session_terminate( std::string &p_strIPCANSessionId );
void pcrf_linked_session_rule_report( std::string &p_strIPCANSessionId, std::vector<SSessionPolicyInfo > &p_vectRuleReport );

#endif /* __PCRF_LINKED_SESSION_H__ */
