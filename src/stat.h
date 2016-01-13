#ifndef _STAT_H_
#define _STAT_H_

#ifdef __cplusplus
extern "C" {	/* функции, реализованные на C++ */
#endif

#include <time.h>
#include <stdint.h>

/* модуль статистики */
struct SStat;
struct CTimeMeasurer;

/* инициализация модуля статистики */
int stat_init();
int stat_fin();
/* запуск и завершение подсчета метрик */
/* фиксирует показания метрик по ветке */
struct SStat * stat_get_branch (const char *p_pszObjName);
/* фиксирует показания метрик по объекту */
void stat_measure (struct SStat *p_psoStat, const char *p_pszObjName, struct CTimeMeasurer *p_pcoTM);


#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif

#endif /* _STAT_H_ */
