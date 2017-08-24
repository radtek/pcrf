#include "pcrf_lock.h"

#include <freeDiameter/extension.h>

int pcrf_lock( pthread_mutex_t p_mmutexLock[ ], int &p_iPrio )
{
  int i;

  i = p_iPrio;
  --i;
  p_iPrio = 0;
  for ( ; i >= 0; --i ) {
    CHECK_FCT( pthread_mutex_lock( &p_mmutexLock[ i ] ) );
    ++p_iPrio;
  }

  ASSERT( i == -1 );

  return 0;
}

/* разблокировка мьютекса */
void pcrf_unlock( pthread_mutex_t p_mmutexUlck[ ], int p_iPrio )
{
  int i = 0;

  for ( ; i < p_iPrio; ++i ) {
    CHECK_FCT_DO( pthread_mutex_unlock( &p_mmutexUlck[ i ] ), break );
  }

  ASSERT( i == p_iPrio );
}

int pcrf_lock_init( pthread_mutex_t p_mmutex[], int p_iCount )
{
  int iRetVal = 0;

  for ( int i = 0; i < p_iCount; ++i ) {
    CHECK_FCT( pthread_mutex_init( &p_mmutex[ i ], NULL ) );
  }

  return iRetVal;
}

void pcrf_lock_fini( pthread_mutex_t p_mmutex[ ], int p_iCount )
{
  for ( int i = 0; i < p_iCount; ++i ) {
    CHECK_FCT_DO( pthread_mutex_destroy( &p_mmutex[ i ] ), /* continue */ );
  }
}
