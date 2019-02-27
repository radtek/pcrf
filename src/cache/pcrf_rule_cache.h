#ifndef __PCRF_RULE_CACHE_H__
#define __PCRF_RULE_CACHE_H__

#include <string>

#include "../app_pcrf_header.h"

#ifdef __cplusplus
extern "C" {	/* �������, ������������� �� C++ */
#endif

int pcrf_rule_cache_init();
void pcrf_rule_cache_fini();

int pcrf_rule_cache_reload();

/* �������� �������� ������� */
int pcrf_rule_cache_get_rule_info( std::string &p_strRuleName, SDBAbonRule &p_soRule );

#ifdef __cplusplus
}				/* �������, ������������� �� C++ */
#endif

#endif /* __PCRF_RULE_CACHE_H__ */
