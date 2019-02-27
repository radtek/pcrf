#include <pthread.h>

#include "pcrf_rule_cache.h"
#include "pcrf_subscriber_cache.h"
#include "pcrf_cache.h"

/* флаг продолжения работы */
volatile int g_iCacheWork;

static int pcrf_cache_rwlock_init();
static void pcrf_cache_rwlock_fini();

static pthread_rwlock_t g_rwlockCache;
static pthread_t g_threadCacheUpdate;
/* мьютекс, используемый в качестве таймера обновления локального хранилища */
static pthread_mutex_t g_mutexCacheUpdateTimer;

/* функция потока обновления локального хранилища */
static void *pcrf_cache_update( void *p_pvParam );

int pcrf_cache_init()
{
	int iRetVal = 0;

	g_iCacheWork = 1;
	CHECK_FCT( pcrf_cache_rwlock_init() );

	CHECK_FCT( pcrf_rule_cache_init() );
	CHECK_FCT( pcrf_subscriber_cache_init() );

	CHECK_FCT( pthread_mutex_init( &g_mutexCacheUpdateTimer, NULL ) );
	CHECK_FCT( pthread_mutex_lock( &g_mutexCacheUpdateTimer ) );
	CHECK_FCT( pthread_create( &g_threadCacheUpdate, NULL, pcrf_cache_update, NULL ) );

	return iRetVal;
}

void pcrf_cache_fini()
{
	g_iCacheWork = 0;
	CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexCacheUpdateTimer ), /* continue */ );
	if( 0 != g_threadCacheUpdate ) {
		CHECK_FCT_DO( pthread_join( g_threadCacheUpdate, NULL ), /* continue */ );
	}

	CHECK_FCT_DO( pthread_mutex_destroy( &g_mutexCacheUpdateTimer ), /* continue */ );
	pcrf_cache_rwlock_fini();

	pcrf_subscriber_cache_fini();
	pcrf_rule_cache_fini();
}

int pcrf_cache_rwlock_init()
{
	CHECK_FCT( pthread_rwlock_init( &g_rwlockCache, NULL ) );
}

void pcrf_cache_rwlock_fini()
{
	CHECK_FCT_DO( pthread_rwlock_destroy( &g_rwlockCache ), /* continue */ );
}

int pcrf_cache_rwlock_rdlock()
{
	CHECK_FCT( pthread_rwlock_rdlock( &g_rwlockCache ) );
}

int pcrf_cache_rwlock_wrlock()
{
	CHECK_FCT( pthread_rwlock_wrlock( &g_rwlockCache ) );
}

int pcrf_cache_rwlock_unlock()
{
	CHECK_FCT( pthread_rwlock_unlock( &g_rwlockCache ) );
}

static void *pcrf_cache_update( void *p_pvParam )
{
	timespec soTimeSpec;
	int iFnRes;

	while( 0 != g_iCacheWork ) {
		CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 60, 0 ), break );
		iFnRes = pthread_mutex_timedlock( &g_mutexCacheUpdateTimer, &soTimeSpec );
		if( ETIMEDOUT == iFnRes ) {
			pcrf_subscriber_cache_reload();
			pcrf_rule_cache_reload();
		} else {
			break;
		}
	}

	pthread_exit( NULL );
}
