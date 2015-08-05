#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "timemeasurer.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <semaphore.h>
#include <pthread.h>
#include <stdio.h>
#include <errno.h>
#include <sys/time.h>
#include <sys/types.h>

extern CLog *g_pcoLog;

/* ��������� ��� �������� �������� � ���� */
struct SDBPoolInfo {
	unsigned int m_uiNumber;
	otl_connect *m_pcoDBConn;
	volatile int m_iIsBusy;
	SDBPoolInfo *m_psoNext;
	CTimeMeasurer *m_pcoTM;
};

/* �������������� ����������� � �� */
/* ������������ ��������:
-1 - ����������� ���� ���������������� � ������������ ��� �� �������
0 - ����������� �������������� � ��� �������������� �� �������������
1 - ����������� ���� ���������������� � ��� ������������� */
int pcrf_db_pool_restore(SDBPoolInfo *p_psoDBConnInfo);

/* ��������� �� ��� ����������� � �� */
static SDBPoolInfo *g_psoDBPoolHead = NULL;

/* ������� ��� ����������� ������� �� ��������� ��������� ����������� � �� */
static sem_t g_tDBPoolSem;
/* ������� ��� ����������� ������ ���������� ����������� */
static pthread_mutex_t g_tMutex;
static int g_iMutexInitialized = 0;

/* ����������� ������ �� ��������� */
static const char *g_pcszDefCheckReq = "select to_char(sysdate,'ddmmyyyy') from dual";
/* ������ ���� ����������� � �� �� ��������� */
#define DB_POOL_SIZE_DEF 1
#define DB_POOL_WAIT_DEF 1

/* ������� ����������� � �� */
int pcrf_db_pool_connect(SDBPoolInfo *p_psoDBConnInfo);
/* ������� ���������� �� �� */
void pcrf_db_pool_logoff (otl_connect *p_pcoDBConn);
/* ������� �������� ����������������� ����������� � �� */
int pcrf_db_pool_check_conn(SDBPoolInfo *p_psoDBConnInfo);

int pcrf_db_pool_init (void)
{
	int iRetVal = 0;

	/* ���� � ���������������� ����� �� ����� ������ ���� �� ���������� �������� �� ��������� */
	int iPoolSize = g_psoConf->m_iDBPoolSize ? g_psoConf->m_iDBPoolSize : DB_POOL_SIZE_DEF;

	/* ������������� �������� */
	CHECK_POSIX_DO (sem_init (&g_tDBPoolSem, 0, iPoolSize), goto fn_error);

	/* ������������� �������� ������ ���������� ����������� */
	CHECK_POSIX_DO (pthread_mutex_init (&g_tMutex, NULL), goto fn_error);

	g_iMutexInitialized = 1;
	/* ������������� ���� �� */
	SDBPoolInfo *psoTmp;
	SDBPoolInfo *psoLast;

	try {
		for (unsigned int iInd = 0; iInd < iPoolSize; ++iInd) {
			psoTmp = new SDBPoolInfo;
			/* ������������� �������� */
			memset (psoTmp, 0, sizeof (*psoTmp));
			psoTmp->m_uiNumber = iInd;
			/* ���������� ���� � ������ */
			if (NULL == g_psoDBPoolHead) {
				g_psoDBPoolHead = psoTmp;
			} else {
				psoLast->m_psoNext = psoTmp;
			}
			psoLast = psoTmp;
			/* ������� ������ ������ ����������� � �� */
			psoTmp->m_pcoDBConn = new otl_connect;
			psoTmp->m_pcoDBConn->otl_initialize(1);
			psoTmp->m_pcoTM = new CTimeMeasurer();
			if (psoTmp->m_pcoDBConn) {
				CHECK_POSIX_DO (pcrf_db_pool_connect (psoTmp), goto fn_error);
			}
		}
	} catch (std::bad_alloc &coBadAlloc) {
		UTL_LOG_F(*g_pcoLog, "memory allocftion error: '%s';", coBadAlloc.what());
		int iRetVal = ENOMEM;
		goto fn_error;
	}

	/* ���� �� ��������� ��� ������ ������� �� ������� */
	goto fn_return;

	fn_error:
	pcrf_db_pool_fin ();
	iRetVal = -1600;

	fn_return:
	return iRetVal;
}

void pcrf_db_pool_fin (void)
{
	/* ����������� �������, ������� ��������� */
	sem_close (&g_tDBPoolSem);
	/* ����������� ��� ����������� � �� */
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
			delete g_psoDBPoolHead->m_pcoTM;
			g_psoDBPoolHead->m_pcoTM = NULL;
		}
		delete g_psoDBPoolHead;
		g_psoDBPoolHead = psoTmp;
	}
	/* ����������� �������, ������� ��������� */
	if (g_iMutexInitialized) {
		pthread_mutex_destroy (&g_tMutex);
		g_iMutexInitialized = 0;
	}
}

int pcrf_db_pool_get(void **p_ppcoDBConn, const char *p_pszClient)
{
	int iRetVal = 0;
	int iFnRes;

	/* ������������� �������� */
	*p_ppcoDBConn = NULL;

	timeval soCurTime;
	timespec soWaitTime;

	/* ����������� ������� ����� */
	gettimeofday (&soCurTime, NULL);
	/* ������ ����� ���������� �������� �������� */
	soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBPoolWait ? g_psoConf->m_iDBPoolWait : DB_POOL_WAIT_DEF);
	soWaitTime.tv_nsec = soCurTime.tv_usec * 1000;

	/* ���� ����� ����������� ������� ��� ������� ������� */
	if (sem_timedwait (&g_tDBPoolSem, &soWaitTime)) {
		UTL_LOG_F (*g_pcoLog, "failed waiting for a free DB connection: '%s'", p_pszClient);
		return errno;
	}

	/* �������� ����� ���������� ����������� */
	/* ��������� ������ � ������� ���� ��� ����������� ������ */
	/* ��� �������� ���������� �� �� ��������� �����, ����� ������ �������� �� ��������� ��������� �������� �������� */
	iRetVal = pthread_mutex_lock (&g_tMutex);
	if (iRetVal) {
		sem_post (&g_tDBPoolSem);
		UTL_LOG_F (*g_pcoLog, "can not enter into critical section: error code: '%u'", iRetVal);
		return iRetVal;
	}

	SDBPoolInfo *psoTmp = g_psoDBPoolHead;
	/* ������� ���� ��� ������� � ������ ���� �� ������ �� ����� */
	while (psoTmp) {
		/* ���� ����������� ������ ���� ������ */
		if (psoTmp->m_iIsBusy) {
			psoTmp = psoTmp->m_psoNext;
		} else {
			/* � ��������� ������ ��������� ����� */
			break;
		}
	}
	/* �� ������ ������, �������� ��������� */
	if (psoTmp) {
		/* ��������� ����������������� ����������� */
		if (0 > pcrf_db_pool_restore(psoTmp)) {
			/* ���������� ���������������� � ������������ ��� �� ������� */
		} else {
			/* �������� ����������� ��� ������� */
			psoTmp->m_iIsBusy = 1;
			psoTmp->m_pcoTM->Set();
			*p_ppcoDBConn = psoTmp->m_pcoDBConn;
			UTL_LOG_D(*g_pcoLog, "selected DB connection: '%u'; '%x:%s';", psoTmp->m_uiNumber, pthread_self(), p_pszClient);
		}
	} else {
		iRetVal = -2222;
		UTL_LOG_F(*g_pcoLog, "unexpected error: free db connection not found");
	}
	iFnRes = pthread_mutex_unlock (&g_tMutex);
	if (iFnRes)
		UTL_LOG_F (*g_pcoLog, "can not enter into critical section: error code: '%u'", iFnRes);

	return iRetVal;
}

int pcrf_db_pool_rel(void *p_pcoDBConn, const char *p_pszClient)
{
	int iRetVal = 0;
	SDBPoolInfo *psoTmp = g_psoDBPoolHead;

	/* ������������� ���������� �� ������� ���� */
	iRetVal = pthread_mutex_lock (&g_tMutex);
	/* ���� �������� ������ �������� �������� */
	if (iRetVal) {
		return iRetVal;
	}
	/* ������� ���� ������ */
	while (psoTmp) {
		/* ���� ����� ������� ����������� */
		if (psoTmp->m_pcoDBConn == p_pcoDBConn) {
			break;
		} else {
			psoTmp = psoTmp->m_psoNext;
		}
	}
	/* �� ������ ������ ��������� ��������� */
	if (psoTmp) {
		/* ����� ����������� ��� ��������� */
		if (psoTmp->m_iIsBusy) {
			char mcTimeInterval[256];
			psoTmp->m_iIsBusy = 0;
			psoTmp->m_pcoTM->GetDifference(NULL, mcTimeInterval, sizeof(mcTimeInterval));
			UTL_LOG_D(*g_pcoLog, "released DB connection: '%u'; '%x:%s' in '%s';", psoTmp->m_uiNumber, pthread_self(), p_pszClient, mcTimeInterval);
		} else {
			UTL_LOG_F(*g_pcoLog, "connection is already freely: %p", psoTmp->m_pcoDBConn);
		}
		/* ����������� ������� */
		sem_post (&g_tDBPoolSem);
	} else {
		/* ������ ���� �� ������ */
		UTL_LOG_F(*g_pcoLog, "connection is not exists: %p", p_pcoDBConn);
		iRetVal = -2000;
	}
	/* ������� ���������� ������� ���� */
	iRetVal = pthread_mutex_unlock (&g_tMutex);
	/* ���� ������������� �������� ����������� �������� */
	if (iRetVal) {
		/* ��� �����, ��� ������ � ������ ������? */
	}

	return iRetVal;
}

int pcrf_db_pool_restore(SDBPoolInfo *p_psoDBConnInfo)
{
	int iFnRes;

	/* ��� ������ ����� ������� �������� */
	/* ����� ������ �� ��������� � �� */
	if (! p_psoDBConnInfo->m_pcoDBConn->connected) {
		UTL_LOG_E(*g_pcoLog, "DB not connected");
		iFnRes = pcrf_db_pool_connect(p_psoDBConnInfo);
		/* ����������� ������������� */
		if (0 == iFnRes) {
			return 1;
		} else {
			/* ����������� ������������ �� ������� */
			return -1;
		}
	}
	/* ��������� ����������� �� ����������������� */
	iFnRes = pcrf_db_pool_check_conn (p_psoDBConnInfo);
	/* ���� ����������� ���������������� */
	if (iFnRes) {
		pcrf_db_pool_logoff (p_psoDBConnInfo->m_pcoDBConn);
		iFnRes = pcrf_db_pool_connect (p_psoDBConnInfo);
		if (0 == iFnRes) {
			/* ����������� ������������� */
			UTL_LOG_N(*g_pcoLog, "DB connection '%u' restored", p_psoDBConnInfo->m_uiNumber);
			return 1;
		} else {
			/* ����������� ������������ �� ������� */
			UTL_LOG_E(*g_pcoLog, "can not to restore DB connection '%u'", p_psoDBConnInfo->m_uiNumber);
			return -1;
		}
	} else {
		/* ���� ����������� �������������� � �� ������� �������������� */
		return 0;
	}
}

int pcrf_db_pool_connect(SDBPoolInfo *p_psoDBConnInfo)
{
	int iRetVal = 0;

	try {
		char mcConnString[0x1000];
		int iStrLen;

		iStrLen = snprintf (mcConnString, sizeof (mcConnString) - 1, "%s/%s@%s", g_psoConf->m_pszDBUser, g_psoConf->m_pszDBPswd, g_psoConf->m_pszDBServer);
		if (iStrLen < 0) {
			iRetVal = errno;
			return iRetVal;
		}
		if (iStrLen >= sizeof (mcConnString)) {
			return -20;
		}
		mcConnString[iStrLen] = '\0';
		p_psoDBConnInfo->m_pcoDBConn->rlogon (mcConnString, 0, NULL, NULL);
		p_psoDBConnInfo->m_pcoDBConn->auto_commit_off();
		UTL_LOG_N(*g_pcoLog, "DB connection '%u' is established successfully", p_psoDBConnInfo->m_uiNumber);
	} catch (otl_exception &coExcept) {
		UTL_LOG_F(*g_pcoLog, "DB connection: '%u: error code: '%d'; message: '%s'; query: '%s'", p_psoDBConnInfo->m_uiNumber, coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
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

int pcrf_db_pool_check_conn(SDBPoolInfo *p_psoDBConnInfo)
{
	int iRetVal = 0;
	otl_nocommit_stream coStream;
	try {
		const char *pcszCheckReq;

		/* ���� ����������� ������ ����� � ������������ � �� �������� */
		if (g_psoConf->m_pszDBDummyReq && g_psoConf->m_pszDBDummyReq[0]) {
			pcszCheckReq = g_psoConf->m_pszDBDummyReq;
		} else {
			/* � ��������� ������ ���������� ������ �� ��������� */
			pcszCheckReq = g_pcszDefCheckReq;
		}

		coStream.open (1, pcszCheckReq, *p_psoDBConnInfo->m_pcoDBConn);
		char mcResult[32];
		coStream >> mcResult;
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "DB connection: '%u': error code: '%d'; message: '%s'; query: '%s'", p_psoDBConnInfo->m_uiNumber, coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}
