#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>

/* структура для хранения сведений о пуле */
struct SDBPoolInfo {
	otl_connect *m_pcoDBConn;
	volatile int m_iIsBusy;
	SDBPoolInfo *m_psoNext;
};

/* указатели на пул подключений к БД */
static SDBPoolInfo *g_psoDBPoolHead = NULL;
static SDBPoolInfo *g_psoDBPoolTail = NULL;

/* семафор для организации очереди на получение сободного подключения к БД */
static sem_t g_tDBPoolSem;
/* мьютекс для безопасного поиска свободного подключения */
static pthread_mutex_t g_tMutex;
static int g_iMutexInitialized = 0;

/* проверочный запрос по умолчанию */
static const char *g_pcszDefCheckReq = "select to_char(sysdate,'ddmmyyyy') from dual";
/* размер пула подключений к БД по умолчанию */
#define DB_POOL_SIZE_DEF 1
#define DB_POOL_WAIT_DEF 1

/* функция подключения к БД */
int pcrf_db_pool_connect (otl_connect *p_pcoDBConn);
/* функция отключение от БД */
void pcrf_db_pool_logoff (otl_connect *p_pcoDBConn);
/* функция проверки работоспособности подключения к БД */
int pcrf_db_pool_check_conn (otl_connect *p_pcoDBConn);

int pcrf_db_pool_init (void)
{
	int iRetVal = 0;
	/* если в конфигурационном файле не задан размер пула БД используем значение по умолчанию */
	int iPoolSize = g_psoConf->m_iDBPoolSize ? g_psoConf->m_iDBPoolSize : DB_POOL_SIZE_DEF;

	/* инициализация семафора */
	//g_ptDBPoolSem = sem_open (NULL, O_CREAT);
	//if (SEM_FAILED == g_ptDBPoolSem) {
	//	iRetVal = errno;
	//	goto fn_error;
	//}
	/* инициализация безыминного семафора */
	CHECK_POSIX_DO (sem_init (&g_tDBPoolSem, 0, iPoolSize), goto fn_error);

	/* инициализация мьютекса поиска свободного подключения */
	CHECK_POSIX_DO (pthread_mutex_init (&g_tMutex, NULL), goto fn_error);

	g_iMutexInitialized = 1;
	/* инициализация пула БД */
	SDBPoolInfo *psoTmp;

	for (int iInd = 0; iInd < iPoolSize; ++iInd) {
		psoTmp = new SDBPoolInfo;
		if (NULL ==  g_psoDBPoolTail) {
			g_psoDBPoolTail  = psoTmp;
		}
		/* на всякий случай проверяем, все ли в порядке с указателем */
		if (NULL == psoTmp) {
			iRetVal = -1500;
			break;
		}
		/* инициализация струтуры */
		memset (psoTmp, 0, sizeof (*psoTmp));
		/* укладываем всех в список */
		if (NULL == g_psoDBPoolHead) {
			g_psoDBPoolHead = psoTmp;
		} else {
			psoTmp->m_psoNext = g_psoDBPoolHead;
			g_psoDBPoolHead = psoTmp;
		}
		/* создаем объект класса подключения к БД */
		psoTmp->m_pcoDBConn = new otl_connect;
		/* снова, на всякий случай, проверяем указатель */
		if (psoTmp->m_pcoDBConn) {
			CHECK_POSIX_DO (pcrf_db_pool_connect (psoTmp->m_pcoDBConn), goto fn_error);
			/* если возникла ошибка подключения */
		}
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
			if (g_psoDBPoolHead->m_iIsBusy) {
				g_psoDBPoolHead->m_pcoDBConn->cancel ();
			}
			if (g_psoDBPoolHead->m_pcoDBConn->connected) {
				pcrf_db_pool_logoff (g_psoDBPoolHead->m_pcoDBConn);
			}
			delete g_psoDBPoolHead->m_pcoDBConn;
			g_psoDBPoolHead->m_pcoDBConn = NULL;
		}
		delete g_psoDBPoolHead;
		g_psoDBPoolHead = psoTmp;
	}
	g_psoDBPoolTail = NULL;
	/* освобождаем ресурсы, занятые мьютексом */
	if (g_iMutexInitialized) {
		pthread_mutex_destroy (&g_tMutex);
		g_iMutexInitialized = 0;
	}
}

int pcrf_db_pool_get (void **p_ppcoDBConn)
{
	int iRetVal = 0;

	/* инициализация значения */
	*p_ppcoDBConn = NULL;

	timeval soCurTime;
	timespec soWaitTime;

	/* запрашиваем текущее время */
	gettimeofday (&soCurTime, NULL);
	/* задаем время завершения ожидания семафора */
	soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBPoolWait ? g_psoConf->m_iDBPoolWait : DB_POOL_WAIT_DEF);
	soWaitTime.tv_nsec = soCurTime.tv_usec * 1000;

	/* ждем когда освободится семафор или истечет таймаут */
	CHECK_POSIX (sem_timedwait (&g_tDBPoolSem, &soWaitTime));

	/* начинаем поиск свободного подключения */
	/* блокируем доступ к участку кода для безопасного поиска */
	/* для ожидания используем ту же временную метку, чтобы полное ожидание не превышало заданного значения таймаута */
	CHECK_POSIX (pthread_mutex_timedlock (&g_tMutex, &soWaitTime));

	SDBPoolInfo *psoTmp = g_psoDBPoolHead;
	/* обходим весь пул начиная с головы пока не дойдем до конца */
	while (psoTmp) {
		/* если подключения занято идем дальше */
		if (psoTmp->m_iIsBusy) {
			psoTmp = psoTmp->m_psoNext;
		} else {
			/* в противном случае завершаем обход */
			break;
		}
	}
	/* на всякий случай, проверим указатель */
	if (psoTmp) {
		/* помечаем подключение как занятое */
		psoTmp->m_iIsBusy = 1;
		*p_ppcoDBConn = psoTmp->m_pcoDBConn;
	} else {
		iRetVal = -2222;
	}
	CHECK_POSIX (pthread_mutex_unlock (&g_tMutex));

	return iRetVal;
}

int pcrf_db_pool_rel (void *p_pcoDBConn)
{
	int iRetVal = 0;
	SDBPoolInfo *psoTmp = g_psoDBPoolHead;

	/* устанавливаем блокировку на участок кода */
	iRetVal = pthread_mutex_lock (&g_tMutex);
	/* если возникла ошибка ожидания мьютекса */
	if (iRetVal) {
		return iRetVal;
	}
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
		psoTmp->m_iIsBusy = 0;
		/* освобождаем семафор */
		sem_post (&g_tDBPoolSem);
	} else {
		/* такого быть не должно */
		iRetVal = -2000;
	}
	/* снимаем блокировку участка кода */
	iRetVal = pthread_mutex_unlock (&g_tMutex);
	/* если разблокировка мьютекса выполнилась неудачно */
	if (iRetVal) {
		/* это плохо, что делать в данном случае? */
	}

	return iRetVal;
}

int pcrf_db_pool_restore (void *p_pcoDBConn)
{
	int iFnRes;
	otl_connect *pcoDBConn = (otl_connect *) p_pcoDBConn;

	/* для начала самая простая проверка */
	/* когда объект не подключен к БД */
	if (! pcoDBConn->connected) {
		iFnRes = pcrf_db_pool_connect (pcoDBConn);
		/* подключение восстановлено */
		if (0 == iFnRes) {
			return 1;
		} else {
			/* подключение восстановить не удалось */
			return -1;
		}
	}
	/* проверяем подключение на работоспособность */
	iFnRes = pcrf_db_pool_check_conn (pcoDBConn);
	/* если подключение неработоспособно */
	if (iFnRes) {
		pcrf_db_pool_logoff (pcoDBConn);
		iFnRes = pcrf_db_pool_connect (pcoDBConn);
		/* подключение восстановлено */
		if (0 == iFnRes) {
			return 1;
		} else {
			/* подключение восстановить не удалось */
			return -1;
		}
	} else {
		/* если подключение работоспособно и не требует восстановления */
		return 0;
	}
}

int pcrf_db_pool_connect (otl_connect *p_pcoDBConn)
{
	int iRetVal = 0;

	try {
		char mcConnString[0x1000];
		int iStrLen;

		iStrLen = snprintf (mcConnString, sizeof (mcConnString) - 1, "%s/%s@%s", g_psoConf->m_pszDBUser, g_psoConf->m_pszDBPswd, g_psoConf->m_pszDBServer);
		if (0 >= iStrLen) {
			return -10;
		}
		if (sizeof (mcConnString) - 1 <= iStrLen) {
			return -20;
		}
		mcConnString[iStrLen] = '\0';
		p_pcoDBConn->rlogon (mcConnString, 0, NULL, NULL);
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("pcrf_db_pool_connect: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

void pcrf_db_pool_logoff (otl_connect *p_pcoDBConn)
{
	if (p_pcoDBConn) {
		if (p_pcoDBConn->connected) {
			p_pcoDBConn->logoff ();
		}
	}
}

int pcrf_db_pool_check_conn (otl_connect *p_pcoDBConn)
{
	int iRetVal = 0;

	try {
		const char *pcszCheckReq;

		/* если проверочный запрос задан в конфигурации и он непустой */
		if (g_psoConf->m_pszDBDummyReq && g_psoConf->m_pszDBDummyReq[0]) {
			pcszCheckReq = g_psoConf->m_pszDBDummyReq;
		} else {
			/* в противном случае используем запрос по умолчанию */
			pcszCheckReq = g_pcszDefCheckReq;
		}

		otl_stream coStream (1, pcszCheckReq, *p_pcoDBConn);
		char mcResult[32];
		coStream >> mcResult;
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("pcrf_db_pool_check_conn: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}
