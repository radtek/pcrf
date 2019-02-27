#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

static pthread_mutex_t g_mutexCDRTimer;
static pthread_t g_threadCDR;
static int g_iFile;
static char g_mcFileName[ PATH_MAX ];

#define CDR_INITIAL_NAME "%Y%m%d%H%M%S_cdr.txt"
#define CDR_FILE_FLAG O_CREAT | O_APPEND | O_RDWR
#define CDR_FILE_MODE S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP | S_IROTH

int pcrf_cdr_make_timestamp( char *p_pszFileName, size_t p_stSize, const char *p_pszFormat )
{
  int iRetVal = 0;
  time_t tmTm;
  tm soTm;

  if ( static_cast<time_t>( -1 ) != time( &tmTm ) ) {
  } else {
    iRetVal = errno;
    return iRetVal;
  }
  if ( NULL != localtime_r( &tmTm, &soTm ) ) {
  } else {
    iRetVal = -1;
    return iRetVal;
  }
  if ( 0 == strftime( p_pszFileName, p_stSize, p_pszFormat, &soTm ) ) {
  } else {
    iRetVal = 0;
  }

  return iRetVal;
}

void pcrf_cdr_make_record( SMsgDataForDB &p_soReqData, std::string &p_strData )
{
  /* формируем тип записи */
  switch ( p_soReqData.m_psoReqInfo->m_iCCRequestType ) {
    case INITIAL_REQUEST:
      p_strData = "1\t";
    break;
    case TERMINATION_REQUEST:
      p_strData = "3\t";
      break;
    default:
      p_strData.clear();
      return;
  }

  /* формируем общие параметры */
  /* записываем Session-Id */
  p_strData += p_soReqData.m_psoSessInfo->m_strSessionId;
  p_strData += '\t';

  /* формируем временную метку */
  timeval soTV;
  char mcString[ 32 ];
  int iFnRes;

  if ( 0 == gettimeofday( &soTV, NULL ) ) {
    uint64_t uiTmStmp;

    uiTmStmp =  ( soTV.tv_sec  * 1000 );
    uiTmStmp += ( soTV.tv_usec / 1000 );
    iFnRes = snprintf( mcString, sizeof(mcString), "%llu", uiTmStmp );
    if ( iFnRes > 0 && iFnRes < sizeof( mcString ) ) {
      p_strData += mcString;
    }
  }
  p_strData += '\t';

  /* записываем imsi */
  if ( 0 == p_soReqData.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI.is_null() ) {
    p_strData += p_soReqData.m_psoSessInfo->m_soSubscriptionData.m_coEndUserIMSI.v;
  }

  /* формируем параметры, характерные для начала сессии */
  if ( INITIAL_REQUEST == p_soReqData.m_psoReqInfo->m_iCCRequestType ) {
    p_strData += '\t';

    /* записываем Framed-IP-Address */
    if ( static_cast<uint32_t>( -1 ) != p_soReqData.m_psoSessInfo->m_ui32FramedIPAddress ) {
      iFnRes = snprintf( mcString, sizeof( mcString ), "%u", p_soReqData.m_psoSessInfo->m_ui32FramedIPAddress );
      if ( iFnRes > 0 && iFnRes < sizeof( mcString ) ) {
        p_strData += mcString;
      }
    }
    p_strData += '\t';

    /* записываем imeisv */
    if ( 0 == p_soReqData.m_psoSessInfo->m_coIMEI.is_null() ) {
      p_strData += p_soReqData.m_psoSessInfo->m_coIMEI.v;
    }
    p_strData += '\t';

    /* записываем apn */
    if ( 0 == p_soReqData.m_psoSessInfo->m_coCalledStationId.is_null() ) {
      p_strData += p_soReqData.m_psoSessInfo->m_coCalledStationId.v;
    }
  }

  p_strData += '\n';
}

void pcrf_cdr_write_record( std::string &p_strData, int p_iFile )
{
  if ( 0 < p_strData.length() ) {
    write( p_iFile, p_strData.data(), p_strData.length() );
  }
}

int pcrf_cdr_make_filename( const char *p_pszDir, const char *p_pszFileMask, char *p_pszFileName, size_t p_stSize )
{
  if ( NULL != p_pszDir ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  char mcTimeStamp[ 256 ];
  int iFnRes;

  CHECK_FCT( pcrf_cdr_make_timestamp( mcTimeStamp, sizeof( mcTimeStamp ), p_pszFileMask ) );

  iFnRes = snprintf( p_pszFileName, p_stSize, "%s%s", p_pszDir, mcTimeStamp );
  if ( 0 < iFnRes ) {
    if ( p_stSize > iFnRes ) {
    } else {
      return EINVAL;
    }
  } else {
    return -1;
  }

  return iRetVal;
}

void pcrf_cdr_file_completed( const char *p_pszFileName )
{
  if ( NULL != g_psoConf->m_pszCDRComplDir ) {
    char mcNewFileName[ 1024 ];

    CHECK_FCT_DO( pcrf_cdr_make_filename( g_psoConf->m_pszCDRComplDir, g_psoConf->m_pszCDRMask, mcNewFileName, sizeof( mcNewFileName ) ), return );

    if ( 0 == rename( p_pszFileName, mcNewFileName ) ) {
    } else {
      LOG_D( "can not to move file: %d", errno );
    }
  }
}

void * pcrf_cdr_recreate_file( void *p_vParam )
{
  char mcFileName[ 1024 ];
  int iTmpFile = -1;
  int iNewFile;
  size_t stLen;
  time_t tmTm;
  tm soTm;
  timeval soTimeVal;
  timespec soTimeSpec;

  if ( 0 == g_psoConf->m_iCDRInterval ) {
    g_psoConf->m_iCDRInterval = 60;
  }

  /* готовим ожидание */
  CHECK_FCT_DO( gettimeofday( &soTimeVal, NULL ), pthread_exit(NULL) );
  soTimeSpec.tv_sec = soTimeVal.tv_sec;
  soTimeSpec.tv_sec += g_psoConf->m_iCDRInterval;
  soTimeSpec.tv_nsec = 0;

  while ( ( ETIMEDOUT == pthread_mutex_timedlock( &g_mutexCDRTimer, &soTimeSpec ) ) ) {
    if ( -1 != iTmpFile ) {
      close( iTmpFile );
    }
    CHECK_FCT_DO( pcrf_cdr_make_filename( g_psoConf->m_pszCDRDir, CDR_INITIAL_NAME, mcFileName, sizeof( mcFileName ) ), pthread_exit( NULL ) );
    iNewFile = open( mcFileName, CDR_FILE_FLAG, CDR_FILE_MODE );
    if ( -1 != iNewFile ) {
    } else {
      break;
    }
    iTmpFile = g_iFile;
    g_iFile = iNewFile;
    pcrf_cdr_file_completed( g_mcFileName );
    strcpy( g_mcFileName, mcFileName );
    /* готовим ожидание */
    CHECK_FCT_DO( gettimeofday( &soTimeVal, NULL ), break );
    soTimeSpec.tv_sec = soTimeVal.tv_sec;
    soTimeSpec.tv_sec += g_psoConf->m_iCDRInterval;
    if ( 0 != ( soTimeSpec.tv_sec % g_psoConf->m_iCDRInterval ) ) {
      soTimeSpec.tv_sec /= g_psoConf->m_iCDRInterval;
      soTimeSpec.tv_sec *= g_psoConf->m_iCDRInterval;
    }
    soTimeSpec.tv_nsec = 0;
  }

  pthread_exit( NULL );
}

int pcrf_cdr_init()
{
  int iRetVal = 0;

  if ( NULL != g_psoConf->m_pszCDRMask && NULL != g_psoConf->m_pszCDRDir ) {
  } else {
    return EINVAL;
  }

  CHECK_FCT( pcrf_cdr_make_filename( g_psoConf->m_pszCDRDir, CDR_INITIAL_NAME, g_mcFileName, sizeof( g_mcFileName ) ) );
  g_iFile = open( g_mcFileName, CDR_FILE_FLAG, CDR_FILE_MODE );
  if ( -1 != g_iFile ) {
  } else {
    iRetVal = errno;
    return iRetVal;
  }

  CHECK_FCT( pthread_mutex_init( &g_mutexCDRTimer, NULL ) );
  CHECK_FCT( pthread_mutex_lock( &g_mutexCDRTimer ) );
  CHECK_FCT( pthread_create( &g_threadCDR, NULL, pcrf_cdr_recreate_file, NULL ) );
  CHECK_FCT( pthread_detach( g_threadCDR ) );

  LOG_N( "CDR module is initialized successfully" );

  return iRetVal;
}

void pcrf_cdr_fini()
{
  close( g_iFile );
  pcrf_cdr_file_completed( g_mcFileName );
  pthread_mutex_unlock( &g_mutexCDRTimer );
}

int pcrf_cdr_write_cdr( SMsgDataForDB &p_soReqData )
{
  std::string strRecCont;
  int iFile = g_iFile;

  if ( NULL != g_psoConf->m_pszCDRMask && -1 != iFile ) {
    pcrf_cdr_make_record( p_soReqData, strRecCont );
    pcrf_cdr_write_record( strRecCont, iFile );
  }

  return 0;
}
