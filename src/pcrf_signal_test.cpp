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

  timeval soTimeVal;
  std::string strParamValue;

  gettimeofday( &soTimeVal, NULL );
  usleep( rand() % 10000 );

  strParamValue = "CCR-I from ugw";

  for ( int i = 0, cnt = ( 256 + ( rand() % 256 ) ); i < cnt; ++i ) {
    pcrf_stat_add( "diameter.request.processed_in.avg[%s]", "diameter.request.statistics", &soTimeVal, "COMMAND_FROM_PEER_AVG", strParamValue.c_str(), ePCRFStatAvg );
    if ( ( rand() % 10 ) == 1 ) {
      pcrf_stat_add( "diameter.request.timedout.quantity[%s]", "diameter.request.statistics", NULL, "COMMAND_FROM_PEER_TIMEDOUT", strParamValue.c_str(), ePCRFStatCount );
    }
    pcrf_stat_add( "diameter.request.quantity[%s]", "diameter.request.statistics", NULL, "COMMAND_FROM_PEER_COUNT", strParamValue.c_str(), ePCRFStatCount );
  }

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

  pcrf_stat_add( "diameter.request.quantity[%s]", "diameter.request.statistics", NULL, "COMMAND_FROM_PEER_COUNT", "hostD", ePCRFStatCount );

  LOG_D( "leave '%s'", __FUNCTION__ );
}
