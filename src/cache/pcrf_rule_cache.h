#ifndef __PCRF_RULE_CACHE_H__
#define __PCRF_RULE_CACHE_H__

#include <string>

#include "../app_pcrf_header.h"

#ifdef __cplusplus
extern "C" {	/* функции, реализованные на C++ */
#endif

int pcrf_rule_cache_init();
void pcrf_rule_cache_fini();

int pcrf_rule_cache_reload();

/* загрузка описания правила */
int pcrf_rule_cache_get_rule_info( std::string &p_strRuleName, SDBAbonRule &p_soRule );

#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif

#endif /* __PCRF_RULE_CACHE_H__ */
