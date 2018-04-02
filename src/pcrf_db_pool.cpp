#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

extern CLog *g_pcoLog;

/* структура для хранения сведений о пуле */
struct SDBPoolInfo {
	otl_connect *m_pcoDBConn;
  pthread_mutex_t m_mutexLock;
	SDBPoolInfo *m_psoNext;
#ifdef DEBUG
	CTimeMeasurer *m_pcoTM;
#endif
};

/* указатели на пул подключений к БД */
static SDBPoolInfo *g_psoDBPoolHead = NULL;

/* семафор для организации очереди на получение сободного подключения к БД */
static sem_t g_tDBPoolSem;

/* проверочный запрос по умолчанию */
static const char *g_pcszDefCheckReq = "select to_char(sysdate,'ddmmyyyy') from dual";
/* размер пула подключений к БД по умолчанию */
#define DB_POOL_SIZE_DEF 1

/* функция подключения к БД */
int pcrf_db_pool_connect( otl_connect *p_pcoDBConn );
/* функция отключение от БД */
void pcrf_db_pool_logoff (otl_connect *p_pcoDBConn);
/* функция проверки работоспособности подключения к БД */
int pcrf_db_pool_check_conn( otl_connect *p_pcoDBConn );

int pcrf_db_pool_init (void)
{
	int iRetVal = 0;

	/* если в конфигурационном файле не задан размер пула БД используем значение по умолчанию */
	int iPoolSize = g_psoConf->m_iDBPoolSize ? g_psoConf->m_iDBPoolSize : DB_POOL_SIZE_DEF;

	/* инициализация семафора */
	CHECK_POSIX_DO (sem_init (&g_tDBPoolSem, 0, iPoolSize), goto fn_error);

	/* инициализация пула БД */
	SDBPoolInfo *psoTmp;
	SDBPoolInfo *psoLast;

	try {
		for (int iInd = 0; iInd < iPoolSize; ++iInd) {
			psoTmp = new SDBPoolInfo;
			/* инициализация структуры */
			memset (psoTmp, 0, sizeof (*psoTmp));
      CHECK_POSIX_DO( pthread_mutex_init( &psoTmp->m_mutexLock, NULL ), goto fn_error );
			/* укладываем всех в список */
			if (NULL == g_psoDBPoolHead) {
				g_psoDBPoolHead = psoTmp;
			} else {
				psoLast->m_psoNext = psoTmp;
			}
			psoLast = psoTmp;
			/* создаем объект класса подключения к БД */
			psoTmp->m_pcoDBConn = new otl_connect;
			psoTmp->m_pcoDBConn->otl_initialize(1);
#ifdef DEBUG
			psoTmp->m_pcoTM = new CTimeMeasurer();
#endif
			if (psoTmp->m_pcoDBConn) {
        CHECK_POSIX_DO( pcrf_db_pool_connect( psoTmp->m_pcoDBConn ), goto fn_error );
			}
		}
	} catch (std::bad_alloc &coBadAlloc) {
		UTL_LOG_F(*g_pcoLog, "memory allocftion error: '%s';", coBadAlloc.what());
		iRetVal = ENOMEM;
		goto fn_error;
	}

	/* если всё выполнено без ошибок выходим из функции */
	goto fn_return;

	fn_error:
	pcrf_db_pool_fin ();
	iRetVal = -1600;

	fn_return:
	return iRetVal;
}

void pcrf_db_pool_fin (void)
{
	/* освобождаем ресурсы, занятые семафором */
	sem_close (&g_tDBPoolSem);
	/* освобождаем пул подключения к БД */
	SDBPoolInfo *psoTmp;
	psoTmp = g_psoDBPoolHead;
	while (psoTmp) {
		psoTmp = g_psoDBPoolHead->m_psoNext;
		if (g_psoDBPoolHead->m_pcoDBConn) {
      if ( 0 != pthread_mutex_trylock( &g_psoDBPoolHead->m_mutexLock ) ) {
        g_psoDBPoolHead->m_pcoDBConn->cancel();
      }
      if (g_psoDBPoolHead->m_pcoDBConn->connected) {
				pcrf_db_pool_logoff (g_psoDBPoolHead->m_pcoDBConn);
			}
      pthread_mutex_unlock( &g_psoDBPoolHead->m_mutexLock );
      pthread_mutex_destroy( &g_psoDBPoolHead->m_mutexLock );
      delete g_psoDBPoolHead->m_pcoDBConn;
			g_psoDBPoolHead->m_pcoDBConn = NULL;
#ifdef DEBUG
			delete g_psoDBPoolHead->m_pcoTM;
			g_psoDBPoolHead->m_pcoTM = NULL;
#endif
		}
		delete g_psoDBPoolHead;
		g_psoDBPoolHead = psoTmp;
	}
}

int pcrf_db_pool_get( otl_connect **p_ppcoDBConn, const char *p_pszClient, unsigned int p_uiWaitUSec )
{
  int iRetVal = -1;
  int iFnRes;
  timespec soWaitTime;

  /* инициализация значения */
  *p_ppcoDBConn = NULL;

  pcrf_make_timespec_timeout( soWaitTime, 0, p_uiWaitUSec );

  /* ждем когда освободится семафор или истечет таймаут */
  if ( 0 != ( iFnRes = sem_timedwait( &g_tDBPoolSem, &soWaitTime ) ) ) {
    iFnRes = errno;
    if ( NULL != p_pszClient ) {
      UTL_LOG_E( *g_pcoLog, "failed waiting for a free DB connection: client: '%s'; error: '%s'", p_pszClient, strerror( iFnRes ) );
    }
    return iFnRes;
  }

  /* начинаем поиск свободного подключения */
  SDBPoolInfo *psoTmp = g_psoDBPoolHead;
  /* обходим весь пул начиная с головы пока не дойдем до конца */
  while ( psoTmp ) {
    /* если подключения занято идем дальше */
    if ( 0 == pthread_mutex_trylock( &psoTmp->m_mutexLock ) ) {
      /* нашли свободное подключение */
      break;
    } else {
      psoTmp = psoTmp->m_psoNext;
    }
  }
  /* на всякий случай, проверим указатель */
  if ( psoTmp ) {
    /* помечаем подключение как занятое */
    ( *p_ppcoDBConn ) = psoTmp->m_pcoDBConn;
    iRetVal = 0;
#ifdef DEBUG
    psoTmp->m_pcoTM->Set();
#endif
    UTL_LOG_D( *g_pcoLog, "selected DB connection: '%p'; '%x:%s';", psoTmp->m_pcoDBConn, pthread_self(), p_pszClient );
  } else {
    iRetVal = -2222;
    UTL_LOG_F( *g_pcoLog, "unexpected error: free db connection not found" );
  }

  return iRetVal;
}

int pcrf_db_pool_rel(void *p_pcoDBConn, const char *p_pszClient)
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

	int iRetVal = 0;
	SDBPoolInfo *psoTmp = g_psoDBPoolHead;

	/* обходим весь список */
	while (psoTmp) {
		/* если нашли искомое подключение */
		if (psoTmp->m_pcoDBConn == p_pcoDBConn) {
			break;
		} else {
			psoTmp = psoTmp->m_psoNext;
		}
	}
	/* на всякий случай проверяем указатель */
	if (psoTmp) {
		/* метим подключение как незанятое */
    if ( 0 == pthread_mutex_unlock( &psoTmp->m_mutexLock ) ) {
#ifdef DEBUG
      char mcTimeInterval[ 256 ];

      psoTmp->m_pcoTM->GetDifference( NULL, mcTimeInterval, sizeof( mcTimeInterval ) );
      UTL_LOG_D( *g_pcoLog, "released DB connection: '%p'; '%x:%s' in '%s';", psoTmp->m_pcoDBConn, pthread_self(), p_pszClient, mcTimeInterval );
#endif
    } else {
			UTL_LOG_F(*g_pcoLog, "connection is already freely: %p", psoTmp->m_pcoDBConn);
		}
		/* освобождаем семафор */
		sem_post (&g_tDBPoolSem);
	} else {
		/* такого быть не должно */
    UTL_LOG_F(*g_pcoLog, "connection is not exists: %p; client: %s", p_pcoDBConn, p_pszClient);
		iRetVal = -2000;
	}

	return iRetVal;
}

int pcrf_db_pool_restore( otl_connect *p_pcoDBConn )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

	int iFnRes;

	/* для начала самая простая проверка */
	/* когда объект не подключен к БД */
	if (! p_pcoDBConn->connected) {
    UTL_LOG_E( *g_pcoLog, "DB not connected: '%p'", p_pcoDBConn );
    iFnRes = pcrf_db_pool_connect( p_pcoDBConn );
		if (0 == iFnRes) {
      /* подключение восстановлено */
      return 1;
		} else {
			/* подключение восстановить не удалось */
			return -1;
		}
	}
	/* проверяем подключение на работоспособность */
  iFnRes = pcrf_db_pool_check_conn( p_pcoDBConn );
	/* если подключение неработоспособно */
	if (iFnRes) {
		pcrf_db_pool_logoff (p_pcoDBConn);
    iFnRes = pcrf_db_pool_connect( p_pcoDBConn );
		if (0 == iFnRes) {
			/* подключение восстановлено */
      UTL_LOG_N( *g_pcoLog, "DB connection '%p' restored", p_pcoDBConn );
			return 1;
		} else {
			/* подключение восстановить не удалось */
      UTL_LOG_E( *g_pcoLog, "can not to restore DB connection '%p'", p_pcoDBConn );
			return -1;
		}
	} else {
		/* если подключение работоспособно и не требует восстановления */
		return 0;
	}
}

int pcrf_db_pool_connect( otl_connect *p_pcoDBConn )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

	int iRetVal = 0;

	try {
		char mcConnString[0x1000];
		int iStrLen;

		iStrLen = snprintf (mcConnString, sizeof (mcConnString) - 1, "%s/%s@%s", g_psoConf->m_pszDBUser, g_psoConf->m_pszDBPswd, g_psoConf->m_pszDBServer);
		if (0 < iStrLen) {
      if (sizeof (mcConnString) > static_cast<size_t>(iStrLen)) {
      } else {
        return -20;
      }
    } else {
			return -30;
		}
		mcConnString[iStrLen] = '\0';
    p_pcoDBConn->rlogon (mcConnString, 0, NULL, NULL);
    p_pcoDBConn->auto_commit_off();
    UTL_LOG_N( *g_pcoLog, "DB connection '%p' is established successfully", p_pcoDBConn );
	} catch (otl_exception &coExcept) {
    UTL_LOG_F( *g_pcoLog, "DB connection: '%p': error code: '%d'; message: '%s'", p_pcoDBConn, coExcept.code, coExcept.msg );
		iRetVal = coExcept.code;
	}

	return iRetVal;
}

void pcrf_db_pool_logoff (otl_connect *p_pcoDBConn)
{
  if ( NULL != p_pcoDBConn ) {
    if ( 0 != p_pcoDBConn->connected ) {
      p_pcoDBConn->logoff();
    }
  }
}

int pcrf_db_pool_check_conn( otl_connect *p_pcoDBConn )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

	int iRetVal = 0;
	otl_nocommit_stream coStream;
	try {
		const char *pcszCheckReq;

		/* если проверочный запрос задан в конфигурации и он непустой */
		if (g_psoConf->m_pszDBDummyReq && g_psoConf->m_pszDBDummyReq[0]) {
			pcszCheckReq = g_psoConf->m_pszDBDummyReq;
		} else {
			/* в противном случае используем запрос по умолчанию */
			pcszCheckReq = g_pcszDefCheckReq;
		}

		coStream.open (1, pcszCheckReq, *p_pcoDBConn );
		char mcResult[32];
		coStream >> mcResult;
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "DB connection: '%p': error code: '%d'; message: '%s'; query: '%s'", p_pcoDBConn, coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		coStream.close();
	}

	return iRetVal;
}
