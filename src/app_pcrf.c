#include "app_pcrf.h"

#include <freeDiameter/libfdproto.h>

/* static objects */
struct SAppPCRFConf *g_psoConf = NULL;
static struct SAppPCRFConf soConf;

/* initialyze instance */
static int app_pcrf_conf_init ()
{
	g_psoConf = &soConf;

	memset (g_psoConf, 0, sizeof (struct SAppPCRFConf));

	return 0;
}

/* entry point */
static int pcrf_entry (char * conffile)
{
	/* Initialize configuration */
	CHECK_FCT (app_pcrf_conf_init ());

	/* Parse configuration file */
	if (conffile != NULL) {
		CHECK_FCT (app_pcrf_conf_handle (conffile));
	}

	/* инищиализация логгера */
	CHECK_FCT(pcrf_logger_init());

  /* инициализация модуля заббикс */
  CHECK_FCT( pcrf_zabbix_init() );

	/* инициализация модуля статистики */
  CHECK_FCT( pcrf_stat_init() );

  /* инициализация обработчика сигналов SIGUSR */
  CHECK_FCT( pcrf_sig_test_init() );

	/* Install objects definitions for this app_pcrf application */
	CHECK_FCT (app_pcrf_dict_init ());

  /* инициализация модуля формирования CDR */
  if ( 0 != g_psoConf->m_iGenerateCDR ) {
    CHECK_FCT( pcrf_cdr_init() );
  }

	/* инициализация пула подключений к БД */
	CHECK_FCT (pcrf_db_pool_init ());

  /* инициализация очереди sql-запросов */
  CHECK_FCT(pcrf_sql_queue_init());

  /* формирование списка клиентов и регистрация функции валидации клиента */
  CHECK_FCT(app_pcrf_load_peer());

	/* инициализация трейсера */
	CHECK_FCT (pcrf_tracer_init ());

  /* инициализация кеша сессий */
  CHECK_FCT( pcrf_session_cache_init () );

  /* инициализация кеша правил */
  CHECK_FCT(pcrf_rule_cache_init());

  /* инициализация кеша правил сессий */
  CHECK_FCT(pcrf_session_rule_list_init());

  /* Install the handlers for incoming messages */
	CHECK_FCT (app_pcrf_serv_init ());

	/* инициализация клиента (client) */
	CHECK_FCT (pcrf_cli_init ());

	return 0;
}

/* Unload */
void fd_ext_fini(void)
{
	app_pcrf_serv_fini ();
	pcrf_cli_fini ();
  pcrf_session_rule_list_fini();
  pcrf_rule_cache_fini();
  pcrf_session_cache_fini ();
  pcrf_tracer_fini ();
  pcrf_sql_queue_fini();
	pcrf_db_pool_fin ();
  if ( 0 != g_psoConf->m_iGenerateCDR ) {
    pcrf_cdr_fini();
  }
  pcrf_stat_fini();
  pcrf_zabbix_fini();
	pcrf_logger_fini();

  LOG_N( "app_pcrf module is stopped successfully" );
}

EXTENSION_ENTRY ("app_pcrf", pcrf_entry);
