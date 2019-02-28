#ifndef __PCRF_PROCERA_H__
#define __PCRF_PROCERA_H__

#include <string>

#include "../pcrf_otl.h"
#include "../app_pcrf_header.h"

/* формирование запроса на завершение сессии Procera */
int pcrf_procera_terminate_session( std::string &p_strIPCANSessionId );

/* посылает команду на изменение локации пользователя на сервер Procera */
int pcrf_procera_change_uli(
	otl_connect *p_pcoDBConn,
	std::string &p_strSessionId,
	otl_value<std::string> &p_coECGI,
	otl_value<std::string> &p_coCGI );

/* добавляет в ответ Subscription-Id для Procera */
int pcrf_procera_make_subscription_id( msg *p_soAns, otl_value<std::string> &p_coEndUserIMSI, otl_value<std::string> &p_coEndUserE164 );

/* формирование запроса на завершение сессии Procera */
int pcrf_procera_oper_thetering_report( SMsgDataForDB &p_soRequestInfo, std::list<SDBAbonRule> &p_listAbonRule, std::vector<SDBAbonRule> &p_vectActiveRule );

/* дополнительные статические правила */
int pcrf_procera_additional_rules(
	otl_value<std::string> &p_coIMEI,
	otl_value<std::string> &p_coCalledStationId,
	otl_value<std::string> &p_coECGI,
	otl_value<std::string> &p_coCGI,
	std::list<SDBAbonRule> &p_listAbonRules );

#endif /* __PCRF_PROCERA_H__ */
