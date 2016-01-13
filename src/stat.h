#ifndef _STAT_H_
#define _STAT_H_

#ifdef __cplusplus
extern "C" {	/* �������, ������������� �� C++ */
#endif

#include <time.h>
#include <stdint.h>

/* ������ ���������� */
struct SStat;
struct CTimeMeasurer;

/* ������������� ������ ���������� */
int stat_init();
int stat_fin();
/* ������ � ���������� �������� ������ */
/* ��������� ��������� ������ �� ����� */
struct SStat * stat_get_branch (const char *p_pszObjName);
/* ��������� ��������� ������ �� ������� */
void stat_measure (struct SStat *p_psoStat, const char *p_pszObjName, struct CTimeMeasurer *p_pcoTM);


#ifdef __cplusplus
}				/* �������, ������������� �� C++ */
#endif

#endif /* _STAT_H_ */
