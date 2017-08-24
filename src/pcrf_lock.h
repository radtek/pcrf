#ifndef _PCRF_LOCK_H_
#define _PCRF_LOCK_H_

#include <pthread.h>

/* ������������� ������� ��������� */
int pcrf_lock_init( pthread_mutex_t p_mmutex[], int p_iCount );

/* ������������ �������� */
void pcrf_lock_fini( pthread_mutex_t p_mmutex[], int p_iCount );

/* ���������� �������� */
int pcrf_lock( pthread_mutex_t p_mmutexLock[ ], int &p_iPrio );

/* ������������� �������� */
void pcrf_unlock( pthread_mutex_t p_mmutexUlck[ ], int p_iPrio );

#endif /* _PCRF_LOCK_H_ */
