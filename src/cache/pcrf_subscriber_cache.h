#ifndef __PCRF_SUBSCRIBER_CACHE_H__
#define __PCRF_SUBSCRIBER_CACHE_H__

#include <string>

#include "../app_pcrf_header.h"

#ifdef __cplusplus
extern "C" {	/* функции, реализованные на C++ */
#endif

int pcrf_subscriber_cache_init();
void pcrf_subscriber_cache_fini();

int pcrf_subscriber_cache_reload();

int pcrf_subscriber_cache_get_subscriber_id( const SSubscriptionIdData &p_soSubscrData, std::string &p_strSubscriberId );

#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif

#endif /* __PCRF_SUBSCRIBER_CACHE_H__ */
