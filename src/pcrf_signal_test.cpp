#include "app_pcrf_header.h"

#include <signal.h>

static void pcrf_sigusr1_oper( void );
static void pcrf_sigusr2_oper( void );

extern "C"
int pcrf_sig_test_init()
{
  CHECK_FCT( fd_event_trig_regcb( SIGUSR1, "app_pcrf", pcrf_sigusr1_oper ) );
  CHECK_FCT( fd_event_trig_regcb( SIGUSR2, "app_pcrf", pcrf_sigusr2_oper ) );

  LOG_N( "SIGNALTEST module is initialized successfully" );
}

static void pcrf_sigusr1_oper( void )
{
  LOG_D( "enter into '%s'", __FUNCTION__ );

  pcrf_zabbix_set_parameter( "test_host", "diameter.request.quantity", "PEER_NAME", "hostA" );
  pcrf_zabbix_set_parameter( "test_host", "diameter.request.quantity", "PEER_NAME", "hostB" );
  pcrf_zabbix_set_parameter( "test_host", "diameter.request.quantity", "PEER_NAME", "hostC" );

  LOG_D( "leave '%s'", __FUNCTION__ );
}

static void pcrf_sigusr2_oper( void )
{
  LOG_D( "enter into '%s'", __FUNCTION__ );

  static uint64_t ui64TestValueA = 0;
  static uint64_t ui64TestValueB = 0;
  static uint64_t ui64TestValueC = 0;

  time_t tmTimeStamp = time( NULL );

  ui64TestValueA += rand() % 256;
  ui64TestValueB += rand() % 256;
  ui64TestValueC += rand() % 256;

  pcrf_stat_add( "diameter.request.quantity.ccr-i[%s]", "diameter.request.quantity", ui64TestValueA, "PEER_NAME", "hostD" );
  pcrf_stat_add( "diameter.request.quantity.ccr-i[%s]", "diameter.request.quantity", ui64TestValueB, "PEER_NAME", "hostE" );
  pcrf_stat_add( "diameter.request.quantity.ccr-i[%s]", "diameter.request.quantity", ui64TestValueC, "PEER_NAME", "hostF" );

  pcrf_stat_add( "diameter.request.quantity.ccr-u[%s]", "diameter.request.quantity", ui64TestValueA + ( rand() % 256 ), "PEER_NAME", "hostD" );
  pcrf_stat_add( "diameter.request.quantity.ccr-u[%s]", "diameter.request.quantity", ui64TestValueB + ( rand() % 256 ), "PEER_NAME", "hostE" );
  pcrf_stat_add( "diameter.request.quantity.ccr-u[%s]", "diameter.request.quantity", ui64TestValueC + ( rand() % 256 ), "PEER_NAME", "hostF" );

  pcrf_stat_add( "diameter.request.quantity.ccr-t[%s]", "diameter.request.quantity", ui64TestValueA + ( rand() % 256 ), "PEER_NAME", "hostD" );
  pcrf_stat_add( "diameter.request.quantity.ccr-t[%s]", "diameter.request.quantity", ui64TestValueB + ( rand() % 256 ), "PEER_NAME", "hostE" );
  pcrf_stat_add( "diameter.request.quantity.ccr-t[%s]", "diameter.request.quantity", ui64TestValueC + ( rand() % 256 ), "PEER_NAME", "hostF" );

  LOG_D( "leave '%s'", __FUNCTION__ );
}
