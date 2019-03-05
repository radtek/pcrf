#ifndef __PCRF_DEFAULT_RULE_SELECTOR_H__
#define __PCRF_DEFAULT_RULE_SELECTOR_H__

struct SDefaultRuleSelectorData;

#ifdef __cplusplus
#include "../app_pcrf_header.h"
#include <list>
	int pcrf_drs_get_defaultRuleList( SUserEnvironment	&p_soSessEnviron, std::list<std::string> *p_plistRuleName );
extern "C" {
#endif

struct SDefaultRuleSelectorData * pcrf_drs_create();
int pcrf_drs_add_selector( struct SDefaultRuleSelectorData *p_psoSelectorData, const char *p_pszParameter, const char *p_pszValue );
int pcrf_drs_add_defaultRule( const struct SDefaultRuleSelectorData * p_psoSelectorData , const char *p_pszRuleName );

#ifdef __cplusplus
}
#endif

#endif /* __PCRF_DEFAULT_RULE_SELECTOR_H__ */
