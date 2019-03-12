#ifndef __PCRF_TIMED_OPER_H__
#define __PCRF_TIMED_OPER_H__

#include <string>
#include <map>
#include <list>

#include "../app_pcrf_header.h"
#include "../pcrf_otl.h"

#ifdef __cplusplus
extern "C" {
#endif

	/* инициализация модуля */
	int  pcrf_subscriber_data_init();
	void pcrf_subscriber_data_fini();

#ifdef __cplusplus
}
#endif

struct SSubscriberData {
	/* const data */
	int32_t										m_i32CCRType;
	std::string									m_strSubscriberId;
	unsigned int								m_uiPeerDialect;
	std::vector<SSessionUsageInfo>				m_vectUsageInfo;
	SUserEnvironment							m_soUserEnvironment;
	otl_value<std::string>						m_coCalledStationId;
	otl_value<std::string>						m_coIMEI;
	std::vector<SDBAbonRule>					m_vectActive;
	/* viriable data (result of operation) */
	unsigned int								&m_uiActionSet;
	std::list<SDBAbonRule>						&m_listAbonRules;
	std::map<std::string, SDBMonitoringInfo>	&m_mapMonitInfo;
	/* constructor */
	SSubscriberData( int32_t &p_iCCRType,
					 std::string &p_strSubscriberId,
					 unsigned int &p_uiPeerDialect,
					 std::vector<SSessionUsageInfo> &p_vectUsageInfo,
					 SUserEnvironment &p_coUserEnvironment,
					 otl_value<std::string> &p_coCalledStationId,
					 otl_value<std::string> &p_coIMEI,
					 std::vector<SDBAbonRule> &p_vectActive,
					 unsigned int &p_uiActionSet,
					 std::list<SDBAbonRule> &p_listAbonRules,
					 std::map<std::string, SDBMonitoringInfo> &p_mapMonitInfo );
};

/* инициализация структуры */
struct SSubscriberData *
	pcrf_subscriber_data_prepare( int32_t									&p_iCCRType,
								  std::string								&p_strSubscriberId,
								  unsigned int								&p_uiPeerDialect,
								  std::vector<SSessionUsageInfo>			&p_vectUsageInfo,
								  SUserEnvironment							&p_coUserEnvironment,
								  otl_value<std::string>					&p_coCalledStationId,
								  otl_value<std::string>					&p_coIMEI,
								  std::vector<SDBAbonRule>					&p_vectActive,
								  unsigned int								&p_uiActionSet,
								  std::list<SDBAbonRule>					&p_listAbonRules,
								  std::map<std::string, SDBMonitoringInfo>	&p_mapMonitInfo );

/* исполнение действий по подготовке данных */
int pcrf_subscriber_data_proc( SSubscriberData *p_psoSubscriberData );

#endif /* __PCRF_TIMED_OPER_H__ */
