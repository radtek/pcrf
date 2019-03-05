#include "app_pcrf.h"
#include "utils/stat/stat.h"

#include <freeDiameter/libfdproto.h>
/* static objects */
struct SAppPCRFConf *g_psoConf = NULL;
static struct SAppPCRFConf soConf;

uint32_t g_ui32OriginStateId;

/* initialyze instance */
static int app_pcrf_conf_init ()
{
	g_psoConf = &soConf;

	memset (g_psoConf, 0, sizeof (struct SAppPCRFConf));

	return 0;
}

/* entry point */
static int pcrf_entry( char * conffile )
{
	pthread_t tSessionListInitializer;
	pthread_t tSessRuleLstInitializer;
	void *pvThreadResult;
	int iRetVal;

	/* запоминаем значение Origin-State-Id */
	g_ui32OriginStateId = ( uint32_t ) time( NULL );

	/* Initialize configuration */
	CHECK_FCT( app_pcrf_conf_init() );

	/* Parse configuration file */
	if( conffile != NULL ) {
		CHECK_FCT( app_pcrf_conf_handle( conffile ) );
	}

	/* инищиализация логгера */
	CHECK_FCT( pcrf_logger_init() );

	/* инициализация модуля статистики */
	CHECK_FCT( stat_init() );

	/* Install objects definitions for this app_pcrf application */
	CHECK_FCT( app_pcrf_dict_init() );

	/* инициализация модуля формирования CDR */
	if( 0 != g_psoConf->m_iGenerateCDR ) {
		CHECK_FCT( pcrf_cdr_init() );
	}

	/* инициализация пула подключений к БД */
	CHECK_FCT( pcrf_db_pool_init() );

	/* инициализация очереди sql-запросов */
	CHECK_FCT( pcrf_sql_queue_init() );

	/* формирование списка клиентов и регистрация функции валидации клиента */
	CHECK_FCT( app_pcrf_load_peer() );

	/* инициализация трейсера */
	CHECK_FCT( pcrf_tracer_init() );

	/* инициализация кеша */
	CHECK_FCT( pcrf_cache_init() );

	/* инициализация кеша сессий */
	CHECK_FCT( pcrf_session_cache_init( &tSessionListInitializer ) );

	/* инициализация кеша правил сессий */
	CHECK_FCT( pcrf_session_rule_list_init( &tSessRuleLstInitializer ) );

	CHECK_FCT( pcrf_ipc_init() );

	/* ждем окончания инициализации кеша сессий и кеша правил сессий */
	CHECK_FCT( pthread_join( tSessionListInitializer, &pvThreadResult ) );
	if( NULL != pvThreadResult ) {
		iRetVal = *( ( int* ) ( pvThreadResult ) );
		free( pvThreadResult );
		if( 0 == iRetVal ) {
			LOG_N( "session list loaded successfully" );
		} else {
			LOG_F( "an error occurred while session list loading: code: %d", iRetVal );
			return iRetVal;
		}
	}
	CHECK_FCT( pthread_join( tSessRuleLstInitializer, &pvThreadResult ) );
	if( NULL != pvThreadResult ) {
		iRetVal = *( ( int* ) ( pvThreadResult ) );
		free( pvThreadResult );
		if( 0 == iRetVal ) {
			LOG_N( "session rule list loaded successfully" );
		} else {
			LOG_F( "an error occurred while session rule list loading: code: %d", iRetVal );
			return iRetVal;
		}
	}

	/* инициализация модуля получения данных подписчика */
	CHECK_FCT( pcrf_subscriber_data_init() );

	/* Install the handlers for incoming messages */
	CHECK_FCT( app_pcrf_serv_init() );

	/* инициализация клиента (client) */
	CHECK_FCT( pcrf_cli_init() );

	return 0;
}

/* Unload */
void fd_ext_fini( void )
{
	app_pcrf_serv_fini();
	pcrf_cli_fini();
	pcrf_subscriber_data_fini();
	pcrf_ipc_fini();
	pcrf_session_rule_list_fini();
	pcrf_session_cache_fini();
	pcrf_cache_fini();
	pcrf_tracer_fini();
	pcrf_sql_queue_fini();
	pcrf_db_pool_fin();
	if( 0 != g_psoConf->m_iGenerateCDR ) {
		pcrf_cdr_fini();
	}
	stat_fin();
	pcrf_logger_fini();
	pcrf_tracer_rwlock_fini();
}

EXTENSION_ENTRY ("app_pcrf", pcrf_entry);
