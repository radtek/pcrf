#include "app_pcrf_header.h"

#include <poll.h>
#include <sys/signal.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <stdio.h>
#include <stdio_ext.h>

extern CLog *g_pcoLog;

static int g_iWrite;
static int g_iRead;
static pid_t g_tPId;
static pthread_mutex_t g_mutexZabbixQueue;
static pthread_mutex_t g_mutexZabbixWriteTimer;
static pthread_mutex_t g_mutexZabbixReadTimer;
static std::vector<std::string> g_vectorQueue;
static pthread_t g_trheadDataSender;
static pthread_t g_trheadDataReceiver;

static void pcrf_zabbix_sig_oper( void );
static int  pcrf_zabbix_create_child_process();
static int  pcrf_zabbix_recreate_child_process();
static void pcrf_zabbix_push_to_queue( const char *p_pszMessage, int p_iMessageSize );
static bool pcrf_zabbix_check_child_process();

static void * pcrf_zabbix_send_data_thread( void* );
/* 0 - успешно, 1 - запись не произведена */
static int  pcrf_zabbix_write( const char *p_pszData, ssize_t p_stDataLen );
/* возвращает количество обработанных записей */
static void * pcrf_zabbix_read( void * );

extern "C"
int pcrf_zabbix_init( void )
{
  g_vectorQueue.reserve( 4096 );
  CHECK_FCT( pthread_mutex_init( &g_mutexZabbixQueue, NULL ) );
  CHECK_FCT( pthread_mutex_init( &g_mutexZabbixWriteTimer, NULL ) );
  CHECK_FCT( pthread_mutex_init( &g_mutexZabbixReadTimer, NULL ) );
  CHECK_FCT( fd_event_trig_regcb( SIGPIPE, "pcrf_zabbix", pcrf_zabbix_sig_oper ) );
  CHECK_FCT( pcrf_zabbix_create_child_process() );
  CHECK_FCT( pthread_create( &g_trheadDataSender, NULL, pcrf_zabbix_send_data_thread, NULL ) );
  CHECK_FCT( pthread_create( &g_trheadDataReceiver, NULL, pcrf_zabbix_read, NULL ) );

  LOG_N( "ZABBIX module is initialized successfully" );

  return 0;
}

extern "C"
void pcrf_zabbix_fini( void )
{
  pthread_mutex_unlock( &g_mutexZabbixWriteTimer );
  pthread_mutex_unlock( &g_mutexZabbixReadTimer );
  pthread_mutex_destroy( &g_mutexZabbixQueue );
  pthread_join( g_trheadDataSender, NULL );
  pthread_join( g_trheadDataReceiver, NULL );
  pthread_mutex_destroy( &g_mutexZabbixWriteTimer );
  pthread_mutex_destroy( &g_mutexZabbixReadTimer );
  if ( 0 != g_tPId ) {
    kill( g_tPId, SIGTERM );
    pcrf_zabbix_write( "\r\n\r\n", 4 );
    waitpid( g_tPId, NULL, 0 );
  }
  shutdown( g_iRead, SHUT_RD );
  close( g_iRead );
  shutdown( g_iWrite, SHUT_WR );
  close( g_iWrite );

  LOG_N( "ZABBIX module is stopped successfully" );
}

static void pcrf_zabbix_sig_oper( void )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  /* блокируем доступ к очереди */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixQueue ), return );

  if ( pcrf_zabbix_check_child_process() ) {
  } else {
    /* создаем заново процесс-потомок */
    CHECK_FCT_DO( pcrf_zabbix_create_child_process(), /* nothing */ );
  }

  /* снимаем блокировку очереди */
  CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexZabbixQueue ), /* nothing */ );

  LOG_D( "leave: %s", __FUNCTION__ );
}

void pcrf_zabbix_set_parameter( const char *p_pszHostName, const char *p_pszKeyName, const char *p_pszParameterName, const char *p_pszParameterValue )
{
  char mcValue[ 256 ];
  int iFnRes;
  time_t tmTimeStamp = time( NULL );

  if ( static_cast< time_t >( -1 ) != tmTimeStamp ) {
  } else {
    UTL_LOG_E( *g_pcoLog, "time failed: error code: %d; description: %s", errno, strerror( errno ) );
    return;
  }

  iFnRes = snprintf(
    mcValue, sizeof( mcValue ),
    "{\\\"data\\\":[{\\\"{#%s}\\\":\\\"%s\\\"}]}",
    p_pszParameterName, p_pszParameterValue );
  if ( 0 < iFnRes ) {
    if ( iFnRes < sizeof( mcValue ) ) {
      pcrf_zabbix_enqueue_data( p_pszHostName, p_pszKeyName, mcValue, tmTimeStamp );
    }
  }
}

void pcrf_zabbix_enqueue_data( const char *p_pszHostName, const char *p_pszKey, const char *p_pszValue, time_t &p_tTimeStamp )
{
  char mcKey[ 256 ];
  char mcMessage[ 1024 ];
  int iFnRes;

  iFnRes = snprintf(
    mcMessage, sizeof( mcMessage ),
    "\"%s\" \"%s\" %u \"%s\"\r\n",
    p_pszHostName, p_pszKey, p_tTimeStamp, p_pszValue );
  if ( 0 < iFnRes ) {
    if ( iFnRes < sizeof( mcMessage ) ) {
      pcrf_zabbix_push_to_queue( mcMessage, iFnRes );
    }
  }
}

void pcrf_zabbix_enqueue_data( const char *p_pszHostName, const char *p_pszKey, const uint64_t p_ui64Value, time_t &p_tTimeStamp )
{
  std::string strValue = std::to_string( p_ui64Value );

  pcrf_zabbix_enqueue_data( p_pszHostName, p_pszKey, strValue.c_str(), p_tTimeStamp );
}

static int pcrf_zabbix_create_child_process()
{
  int iRetVal = 0;
  pid_t tPId;
  int miReadPipe[ 2 ];
  int miWritePipe[ 2 ];

  /* создаем две пары пайпов */
  CHECK_FCT_DO( pipe( miReadPipe ), iRetVal = errno );
  if ( 0 == iRetVal ) {
  } else {
    UTL_LOG_E( *g_pcoLog, "%s: pipe failed: %d: %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
    goto parent_exit;
  }

  CHECK_FCT_DO( pipe( miWritePipe ), iRetVal = errno );
  if ( 0 == iRetVal ) {
  } else {
    UTL_LOG_E( *g_pcoLog, "%s: pipe failed: %d: %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
    goto parent_exit;
  }

  /* создаем процесс-потомок */
  tPId = fork();
  if ( -1 != tPId ) {
  } else {
    iRetVal = errno;
    UTL_LOG_E( *g_pcoLog, "%s: fork failed: error code: %i; description: %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
    goto parent_exit;
  }

  if ( 0 == tPId ) {
    /* в теле процесса-потомка */
    const char *pszCmd = "/usr/bin/zabbix_sender";
    char * const mpszAttList[ ] = {
      const_cast< char* >( pszCmd ),
      const_cast< char* >( "-z" ),
      const_cast< char* >( "172.27.27.21" ),
      const_cast< char* >( "-Tr" ),
      const_cast< char* >( "-i" ),
      const_cast< char* >( "-" ),
      NULL
    };

    /* закрываем ненужные дескрипторы */
    close( miReadPipe[ 1 ] );
    close( miWritePipe[ 0 ] );

    /* дублируем поток ввода */
    if ( -1 != dup2( miReadPipe[ 0 ], STDIN_FILENO ) ) {
    } else {
      iRetVal = errno;
      UTL_LOG_E( *g_pcoLog, "%s: dup2 on miReadPipe[ 0 ] failed: %d: %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
      goto child_exit;
    }
    /* дублируем поток вывода */
    if ( -1 != dup2( miWritePipe[ 1 ], STDOUT_FILENO ) ) {
    } else {
      iRetVal = errno;
      UTL_LOG_E( *g_pcoLog, "%s: dup2 on miWritePipe[ 1 ] failed: %d: %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
      goto child_exit;
    }

    /* переназначаем бинарный образ процесса-потомка */
    if ( -1 != execve( pszCmd, mpszAttList, NULL ) ) {
      UTL_LOG_D( *g_pcoLog, "%s: execve completed successfully", __FUNCTION__ );
    } else {
      iRetVal = errno;
      UTL_LOG_E( *g_pcoLog, "%s: execve failed: %d; %s", __FUNCTION__, iRetVal, strerror( iRetVal ) );
    }

    child_exit:

    if ( 0 == iRetVal ) {
    } else {
      close( miReadPipe[ 0] );
      close( miWritePipe[ 1 ] );
    }

    exit( iRetVal );
  }

  /* в теле процесса-родителя */
  /* закрываем ненужные дескрипторы */
  close( miReadPipe[ 0 ] );
  close( miWritePipe[ 1 ] );

  parent_exit:

  if ( 0 == iRetVal ) {
    /* сохряняем нужные дескрипторы */
    g_iWrite = miReadPipe[ 1 ];
    g_iRead = miWritePipe[ 0 ];
    /* сохраняем pid процесса */
    g_tPId = tPId;
  } else {
    close( miReadPipe[ 1 ] );
    close( miWritePipe[ 0 ] );
  }

  return iRetVal;
}

static int pcrf_zabbix_recreate_child_process()
{
  int iRetVal = 0;
  int iStatus;

  /* блокируем доступ к очереди */
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixQueue ), goto exit_from_function );

  if ( 0 != g_tPId ) {
    if ( g_tPId == waitpid( g_tPId, &iStatus, WNOHANG ) ) {
      UTL_LOG_D( *g_pcoLog, "%s: zombi cleared: pid: %u", __FUNCTION__, g_tPId );
    } else {
      UTL_LOG_D( *g_pcoLog, "%s: we cant reach to our zombi: pid: %u", __FUNCTION__, g_tPId );
    }
    g_tPId = 0;
  }

  shutdown( g_iRead, SHUT_RDWR );
  close( g_iRead);
  shutdown( g_iWrite, SHUT_RDWR );
  close( g_iWrite );

  iRetVal = pcrf_zabbix_create_child_process();

  /* снимаем блокировку доступа к очереди */
  CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexZabbixQueue ), /* nothing */ );

  exit_from_function:

  return iRetVal;
}

static void pcrf_zabbix_push_to_queue( const char *p_pszMessage, int p_iMessageSize )
{
  std::string strValue;

  strValue.insert( 0, p_pszMessage, p_iMessageSize );

  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixQueue ), return );
  g_vectorQueue.push_back( strValue );
  CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexZabbixQueue ), return );

  UTL_LOG_D( *g_pcoLog, "%s: message: %s", __FUNCTION__, strValue.c_str() );
}

static void * pcrf_zabbix_send_data_thread( void* )
{
  timespec soTimeSpec;
  int iFnRes;
  std::vector<std::string>::iterator iterList;
  std::string strData;

  /* задаем время срабатывания таймера */
  CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 5, 0 ), goto exit_from_thread );
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixWriteTimer ), goto exit_from_thread );

  while ( ETIMEDOUT == pthread_mutex_timedlock( &g_mutexZabbixWriteTimer, &soTimeSpec ) ) {
    /* задаем время срабатывания таймера */
    CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 5, 0 ), goto exit_from_thread );
    /* блокируем доступ к очереди */
    CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixQueue ), goto exit_from_thread );
    UTL_LOG_D( *g_pcoLog, "zabbix queue size: %u", g_vectorQueue.size() );
    /* обходим в цикле все необработанные элементы */
    for ( iterList = g_vectorQueue.begin(); iterList != g_vectorQueue.end(); ++iterList ) {
      strData += iterList->data();
    }
    g_vectorQueue.clear();
    /* снимаем блокировку очереди */
    CHECK_FCT_DO( pthread_mutex_unlock( &g_mutexZabbixQueue ), goto exit_from_thread );

    if ( 0 < strData.length() ) {
    } else {
      continue;
    }

    UTL_LOG_D( *g_pcoLog, "%s: data block:\n%s", __FUNCTION__, strData.c_str() );

    if ( pcrf_zabbix_check_child_process() ) {
    } else {
      CHECK_FCT_DO( pcrf_zabbix_recreate_child_process(), goto exit_from_thread );
    }
    iFnRes = pcrf_zabbix_write( strData.data(), strData.length() );
    if ( 0 == iFnRes ) {
      /* стираем ранее сформированный блок данных */
      strData.clear();
    }
  }

  exit_from_thread:

  pthread_exit( NULL );
}

static int pcrf_zabbix_write( const char *p_pszData, ssize_t p_stDataLen )
{
  int iRetVal = 0;
  ssize_t stWritten;
  pollfd soPoll;
  int iFnRes;

  memset( &soPoll, 0, sizeof( soPoll ) );
  soPoll.fd = g_iWrite;
  soPoll.events = POLLOUT;

  /* ждем когда файл будет доступен для записи */
  iFnRes = poll( &soPoll, 1, 1000 );
  if ( 1 == iFnRes ) {
    if ( POLLOUT & soPoll.revents ) {
      /* файл доступен для записи */
      stWritten = write( g_iWrite, p_pszData, p_stDataLen );
      if ( stWritten == p_stDataLen ) {
        /* надеемся, что нормальная запись будет самым частым событием */
        UTL_LOG_D( *g_pcoLog, "%u bytes is written successfully", stWritten );
      } else {
        /* в случае возникновения ошибки записи */
        UTL_LOG_E( *g_pcoLog, "%s[%u]: %u bytes is written and an error occurred: code: %d; description: %s", __FUNCTION__, __LINE__, stWritten, errno, strerror( errno ) );
        switch ( errno ) {
          case EBADF:
          case EINVAL:
          case EPIPE:
            iRetVal = 1;
            break;
          case EFAULT:
          case EFBIG:
          case EINTR:
          case EIO:
          case ENOSPC:
            iRetVal = 1;
            break;
        }
      }
    } else {
      UTL_LOG_E( *g_pcoLog, "%s: poll returned fd state: %#04hx", __FUNCTION__, soPoll.revents );
      iRetVal = 1;
    }
  } else {
    if ( 0 == iFnRes ) {
      UTL_LOG_D( *g_pcoLog, "%s: poll timed out", __FUNCTION__ );
    } else {
      UTL_LOG_E( *g_pcoLog, "%s: poll failed: %d: %s", __FUNCTION__, errno, strerror( errno ) );
    }
    iRetVal = 1;
  }

  return iRetVal;
}

static void * pcrf_zabbix_read( void * )
{
  int iRetVal = 0;
  timespec soTimeSpec;
  pollfd soPoll;
  uint32_t uiProcessed, uiFailed, uiTotal;
  double dSpent;
  int iFnRes;
  FILE *psoFile;

  /* открываем файл по дескриптору */
  psoFile = fdopen( g_iRead, "r" );
  if ( NULL != psoFile ) {
  } else {
    goto exit_from_thread;
  }

  /* задаем время срабатывания таймера */
  CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 5, 0 ), goto exit_from_thread );
  CHECK_FCT_DO( pthread_mutex_lock( &g_mutexZabbixReadTimer ), goto exit_from_thread );

  while ( ETIMEDOUT == pthread_mutex_timedlock( &g_mutexZabbixReadTimer, &soTimeSpec ) ) {
    CHECK_FCT_DO( pcrf_make_timespec_timeout( soTimeSpec, 5, 0 ), goto exit_from_thread );
    /* на всякий случай првоерим состояние нашего потомка */
    if ( pcrf_zabbix_check_child_process() ) {
    } else {
      /* в случае необходимости пытаемся пересоздать его*/
      CHECK_FCT_DO( pcrf_zabbix_recreate_child_process(), goto exit_from_thread );
    }
    /* готовимся к проверке состояния потока */
    memset( &soPoll, 0, sizeof( soPoll ) );
    soPoll.fd = g_iRead;
    soPoll.events = POLLIN;
    /* читаем все, что можно */
    while ( 1 == poll( &soPoll, 1, 0 ) ) {
      if ( POLLIN & soPoll.revents ) {
        char mcMessage[ 4096 ];
        ssize_t stRead;

        stRead = read( g_iRead, mcMessage, sizeof( mcMessage ) );
        if ( stRead > 0 ) {
          if ( stRead >= sizeof( mcMessage ) ) {
            mcMessage[ sizeof( mcMessage ) - 1 ] = '\0';
          } else {
            mcMessage[ stRead ] = '\0';
          }
          UTL_LOG_D( *g_pcoLog, "message from zabbix_sender:\n%s", mcMessage );
        } else {
          break;
        }

        break;

        iFnRes = sscanf(
          mcMessage,
          "info from server: \"processed: %u; failed : %u; total : %u; seconds spent : %lf\"",
          &uiProcessed, &uiFailed, &uiTotal, &dSpent );
        if ( 4 == iFnRes ) {
          UTL_LOG_D( *g_pcoLog, "zabbix_sender: processed: %u; failed: %u; total: %u; spent: %f", uiProcessed, uiFailed, uiTotal, dSpent );
        } else {
          if ( EOF != iFnRes ) {
            UTL_LOG_E( *g_pcoLog, "error occurred while pipe reading: expected %u values but assigned only %u", 4, iFnRes );
            break;
          } else {
            UTL_LOG_D( *g_pcoLog, "EOF enconutered while pipe reading" );
            break;
          }
        }
      } else {
        iRetVal = errno;
        UTL_LOG_E( *g_pcoLog, "error occurred while reading of zabbix_sender response: code: %u; description: %s", iRetVal, strerror( iRetVal ) );
        break;
      }
    }
  }

  fclose( psoFile );

  exit_from_thread:

  pthread_exit( NULL );
}

static bool pcrf_zabbix_check_child_process()
{
  bool bRetVal = true;
  int iChildStatus;
  pid_t tWaitResCode;

  tWaitResCode = waitpid( g_tPId, &iChildStatus, WNOHANG );
  if ( -1 != tWaitResCode ) {
    if ( tWaitResCode == g_tPId ) {
      UTL_LOG_E( *g_pcoLog, "%s: child process state is changed: status: %#08x", __FUNCTION__, iChildStatus );
      bRetVal = false;
    } else {
      if ( 0 != tWaitResCode ) {
        UTL_LOG_D( *g_pcoLog, "%s: child process [pid = %d] was terminated\n", __FUNCTION__, tWaitResCode );
      }
    }
  } else {
    UTL_LOG_E( *g_pcoLog, "waitpid failed: error code: %d; description: %s", errno, strerror( errno ) );
  }

  return bRetVal;
}
