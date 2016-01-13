#include "stat.h"
#include "log.h"
#include "timemeasurer.h"

#include <map>
#include <string>
#include <new>
#include <pthread.h>
#include <errno.h>
#include <string.h>
#include <stdio.h>

extern CLog *g_pcoLog;
std::map<std::string,SStat*> *g_pmapStat = NULL;
pthread_t *g_ptThread = NULL;
pthread_mutex_t *g_pmutexThread = NULL;
pthread_mutex_t *g_pmutexStat = NULL;
bool *g_bStopThread;

void * stat_output (void *p_pArg);

#define MYDELETE(a) if (a) { delete a; a = NULL; }

struct SStat {
	std::string m_strObjName;		/* имя объекта статистики */
	uint64_t m_ui64Count;			/* количество выполнений */
	timeval m_soTmMin;				/* минимальная продолжительность выполнения */
	timeval m_soTmMax;				/* максимальная продолжительность выполнения */
	timeval m_soTmTotal;			/* суммарная продожительность выполнения */
	timeval m_soTmLast;				/* последнее значение времени выполнения */
	pthread_mutex_t m_mutexStat;
	bool m_bInitialized;
	bool m_bFirst;					/* фиксируется первое значение */
	SStat *m_psoNext;				/* указатель на следующий объект ветви */
	/* конструктор */
	SStat(const char *p_pszObjName);
	/* деструктор */
	~SStat();
};

/* инициализация модуля статистики */
int stat_init()
{
	int iRetVal = 0;

	if (g_pmapStat) {
		UTL_LOG_D (*g_pcoLog, "abnormal pointer value: 'g_pmapStat'");
	}
	if (g_pmutexStat) {
		UTL_LOG_D (*g_pcoLog, "abnormal pointer value: 'g_pmutexStat'");
	}
	try {
		g_pmapStat = new std::map<std::string,SStat*>;
		g_pmutexStat = new pthread_mutex_t;
		g_pmutexThread = new pthread_mutex_t;
		g_ptThread = new pthread_t;
		g_bStopThread = new bool;
		*g_bStopThread = false;
	} catch (std::bad_alloc &ba) {
		iRetVal = ENOMEM;
		MYDELETE (g_pmapStat);
		MYDELETE (g_pmutexStat);
		MYDELETE (g_pmutexThread);
		MYDELETE (g_ptThread);
		MYDELETE (g_bStopThread);
		UTL_LOG_F (*g_pcoLog, "bad alloc: %s", ba.what());
	}

	pthread_mutex_init (g_pmutexStat, NULL);
	pthread_mutex_init (g_pmutexThread, NULL);
	pthread_mutex_lock (g_pmutexThread);
	pthread_create (g_ptThread, NULL, stat_output, g_bStopThread);

	return iRetVal;
}

/* деинициализация модуля статистики */
int stat_fin()
{
	int iRetVal = 0;

	if (g_ptThread) {
		if (g_bStopThread) {
			*g_bStopThread = true;
		}
		if (g_pmutexThread) {
			pthread_mutex_unlock (g_pmutexThread);
		}
		pthread_join (*g_ptThread, NULL);
		MYDELETE (g_ptThread);
	}
	iRetVal = pthread_mutex_lock (g_pmutexStat);
	if (g_pmapStat) {
		std::map<std::string,SStat*>::iterator iter;
		iter = g_pmapStat->begin();
		/* обходим все ветки */
		for (; iter != g_pmapStat->end(); ++iter) {
			/* обходим все объекты ветки */
			delete iter->second;
		}
		MYDELETE (g_pmapStat);
	}
	iRetVal = pthread_mutex_unlock (g_pmutexStat);

	iRetVal = pthread_mutex_destroy (g_pmutexStat);
	MYDELETE (g_pmutexStat);
	MYDELETE (g_pmutexThread);
	MYDELETE (g_bStopThread);

	return iRetVal;
}

SStat * stat_get_branch (const char *p_pszObjName)
{
	if (NULL == g_pmapStat) {
		UTL_LOG_D (*g_pcoLog, "'g_pmapStat' NULL pointer value");
		return NULL;
	}

	std::map<std::string,SStat*>::iterator iter;
	std::string strObjName = p_pszObjName;
	SStat *psoStat;
	int iFnRes;

	/* ищем соответствующую ветку */
	iFnRes = pthread_mutex_lock (g_pmutexStat);
	iter = g_pmapStat->find(strObjName);
	if (iter == g_pmapStat->end()) {
		/* создаем новый объект */
		psoStat = new SStat(p_pszObjName);
		g_pmapStat->insert(std::make_pair(strObjName, psoStat));
		UTL_LOG_D (*g_pcoLog, "branch was not found: '%s'; new branch was created", p_pszObjName);
		UTL_LOG_D (*g_pcoLog, "map size: '%u'", g_pmapStat->size());
	} else {
		/* используем найденный */
		psoStat = iter->second;
	}
	iFnRes = pthread_mutex_unlock (g_pmutexStat);

	return psoStat;
}

void stat_measure (SStat *p_psoStat, const char *p_pszObjName, CTimeMeasurer *p_pcoTM)
{
	/* проверка параметров */
	if (NULL == p_psoStat) {
		UTL_LOG_D (*g_pcoLog, "it has got NULL pointer 'p_psoStat'");
		return;
	}
	if (NULL == p_pszObjName) {
		UTL_LOG_D (*g_pcoLog, "it has got NULL pointer 'p_pszObjName'");
		return;
	}

	SStat *psoLast = p_psoStat;
	SStat *psoWanted = NULL;
	timeval tvDif;

	/* фиксируем продолжительность исполнения */
	p_pcoTM->GetDifference (&tvDif, NULL, 0);

	pthread_mutex_lock(&p_psoStat->m_mutexStat);
	/* ищем соответствующий объект */
	while (psoLast) {
		/* объект найден */
		if (0 == psoLast->m_strObjName.compare(p_pszObjName)) {
			psoWanted = psoLast;
			break;
		}
		/* дошли до конца списка, но не нашли необходимый объект */
		if (NULL == psoLast->m_psoNext) {
			psoWanted = new SStat (p_pszObjName);
			psoLast->m_psoNext = psoWanted;
			break;
		}
		psoLast = psoLast->m_psoNext;
	}
	pthread_mutex_unlock(&p_psoStat->m_mutexStat);

	pthread_mutex_lock(&psoWanted->m_mutexStat);
	psoWanted->m_ui64Count++;
	if (psoWanted->m_bFirst) {
		psoWanted->m_bFirst = false;
		psoWanted->m_soTmMin = tvDif;
		psoWanted->m_soTmMax = tvDif;
	} else {
		p_pcoTM->GetMin(&psoWanted->m_soTmMin, &tvDif);
		p_pcoTM->GetMax(&psoWanted->m_soTmMax, &tvDif);
	}
	p_pcoTM->Add(&psoWanted->m_soTmTotal, &tvDif);
	psoWanted->m_soTmLast = tvDif;
	pthread_mutex_unlock(&psoWanted->m_mutexStat);

}

SStat::SStat(const char *p_pszObjName)
{
	m_bInitialized = false;
	m_bFirst = true;
	m_strObjName = p_pszObjName;
	m_ui64Count = 0ULL;
	memset(&m_soTmMin, 0, sizeof(m_soTmMin));
	memset(&m_soTmMax, 0, sizeof(m_soTmMax));
	memset(&m_soTmTotal, 0, sizeof(m_soTmTotal));
	memset(&m_soTmLast, 0, sizeof(m_soTmLast));
	m_psoNext = NULL;
	if (0 == pthread_mutex_init (&m_mutexStat, NULL))
		m_bInitialized = true;
}

SStat::~SStat()
{
	if (m_psoNext)
		delete m_psoNext;
	pthread_mutex_destroy (&m_mutexStat);
}

void * stat_output (void *p_pArg)
{
	bool *bStop = (bool*)p_pArg;
	timespec soTmSpec;
	SStat *psoTmp;
	bool bFirst = true;
	std::string strMsg;
	char mcBuf[0x1000], mcMin[64], mcMax[64], mcTotal[64], mcLast[64];
	CTimeMeasurer coTM;

	clock_gettime (CLOCK_REALTIME_COARSE, &soTmSpec);
	soTmSpec.tv_sec += 60;

	while (*bStop == false) {
		pthread_mutex_timedlock (g_pmutexThread, &soTmSpec);
		/**/
		pthread_mutex_lock (g_pmutexStat);
		strMsg.clear();
		for (std::map<std::string,SStat*>::iterator iter = g_pmapStat->begin(); iter != g_pmapStat->end(); ++iter) {
			strMsg += "\r\n";
			bFirst = true;
			psoTmp = iter->second;
			while (psoTmp) {
				pthread_mutex_lock (&psoTmp->m_mutexStat);
				if (bFirst) {
					bFirst = false;
				} else {
					strMsg += "\r\n\t";
				}
				strMsg += "branch name: ";
				strMsg += psoTmp->m_strObjName;
				coTM.ToString (&psoTmp->m_soTmMin, mcMin, sizeof(mcMin));
				coTM.ToString (&psoTmp->m_soTmMax, mcMax, sizeof(mcMax));
				coTM.ToString (&psoTmp->m_soTmTotal, mcTotal, sizeof(mcTotal));
				coTM.ToString (&psoTmp->m_soTmLast, mcLast, sizeof(mcLast));
				snprintf (mcBuf, sizeof(mcBuf), ": EC: %u; MIN: %s; MAX: %s; TOTAL: %s; LAST: %s", psoTmp->m_ui64Count, mcMin, mcMax, mcTotal, mcLast);
				strMsg += mcBuf;
				pthread_mutex_unlock (&psoTmp->m_mutexStat);
				psoTmp = psoTmp->m_psoNext;
			}
		}
		pthread_mutex_unlock (g_pmutexStat);
		g_pcoLog->WriteLog (strMsg.c_str());
		/**/
		if (*bStop) {
			break;
		}
		clock_gettime (CLOCK_REALTIME_COARSE, &soTmSpec);
		soTmSpec.tv_sec += 60;
	}

	pthread_exit (NULL);
}
