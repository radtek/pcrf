#ifndef _PCRF_LOCK_H_
#define _PCRF_LOCK_H_

#include <pthread.h>

/* инициализация массива мьютексов */
int pcrf_lock_init( pthread_mutex_t p_mmutex[], int p_iCount );

/* освобождение ресурсов */
void pcrf_lock_fini( pthread_mutex_t p_mmutex[], int p_iCount );

/* блокировка мьютекса */
int pcrf_lock( pthread_mutex_t p_mmutexLock[ ], int &p_iPrio );

/* разблокировка мьютекса */
void pcrf_unlock( pthread_mutex_t p_mmutexUlck[ ], int p_iPrio );

#endif /* _PCRF_LOCK_H_ */
