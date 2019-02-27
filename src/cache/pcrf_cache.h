#ifndef __PCRF_CACHE_H__
#define __PCRF_CACHE_H__

#ifdef __cplusplus
extern "C" {	/* �������, ������������� �� C++ */
#endif

int pcrf_cache_init();
void pcrf_cache_fini();

int pcrf_cache_rwlock_rdlock();
int pcrf_cache_rwlock_wrlock();
int pcrf_cache_rwlock_unlock();

#ifdef __cplusplus
}				/* �������, ������������� �� C++ */
#endif

#endif /* __PCRF_CACHE_H__ */
