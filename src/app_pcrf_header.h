#include <freeDiameter/extension.h>
#include <stdint.h>

#include <string.h>
#include <time.h>
#include <vector>
#include <map>

#include "log.h"

#define DEBUG

#ifdef WIN32
	typedef char uint8_t;
	typedef __int32 int32_t;
	typedef __int64 int64_t;
	typedef unsigned __int32 uint32_t;
	typedef unsigned __int64 uint64_t;
#endif

#define OTL_ORA11G_R2
#define OTL_STL
#define OTL_UBIGINT long unsigned int
#define OTL_STREAM_NO_PRIVATE_UNSIGNED_LONG_OPERATORS
#include "otlv4.h"

#ifdef __cplusplus
extern "C" {	/* функции, реализованные на C++ */
#endif

/* функции сервера */
/* кешированные данные из запроса */
struct SSessionPolicyInfo {
	otl_value<std::string> m_coChargingRuleName;
	otl_value<std::string> m_coPCCRuleStatus;
	otl_value<std::string> m_coRuleFailureCode;
};
/* структура для получения информации о мониторинге */
struct SDBMonitoringInfo {
	/*otl_value<std::string> m_coKeyName; */ /* в качестве имени ключа используется ключ std::map */
	bool m_bDataLoaded;
	otl_value<uint64_t> m_coDosageTotalOctets;
	otl_value<uint64_t> m_coDosageOutputOctets;
	otl_value<uint64_t> m_coDosageInputOctets;
	SDBMonitoringInfo() { m_bDataLoaded = false; }
};
struct SSessionInfo {
	unsigned int m_uiPeerProto;
	otl_value<std::string> m_coSessionId;
	std::string m_strSubscriberId;
	otl_value<std::string> m_coOriginHost;
	otl_value<std::string> m_coOriginRealm;
	otl_value<uint32_t> m_coOriginStateId;
	otl_value<std::string> m_coEndUserE164;
	otl_value<std::string> m_coEndUserIMSI;
	otl_value<std::string> m_coEndUserSIPURI;
	otl_value<std::string> m_coEndUserNAI;
	otl_value<std::string> m_coEndUserPrivate;
	otl_value<std::string> m_coIMEI;
	otl_value<std::string> m_coFramedIPAddress;
	otl_value<otl_datetime> m_coTimeEnd;
	otl_value<std::string> m_coTermCause;
	otl_value<uint32_t> m_coFeatureListId;	/* Feature-List-Id */
	otl_value<uint32_t> m_coFeatureList;	/* Feature-List */
	std::vector<SSessionPolicyInfo> m_vectCRR; /* Charging-Rule-Report */
	otl_value<std::string> m_coCalledStationId; /* Called-Station-Id */
	std::map<std::string,SDBMonitoringInfo> m_mapMonitInfo;
};
struct SSessionUsageInfo {
	otl_value<std::string> m_coMonitoringKey;
	otl_value<uint64_t> m_coCCInputOctets;
	otl_value<uint64_t> m_coCCOutputOctets;
	otl_value<uint64_t> m_coCCTotalOctets;
};
struct SUserLocationInfo {
	bool m_bLoaded;
	otl_value<std::string> m_coSGSNMCCMNC;
	otl_value<std::string> m_coSGSNAddress; /* 3GPP-SGSN-Address */
	otl_value<std::string> m_coSGSNIPv6Address;
	otl_value<std::string> m_coRATType;
	otl_value<std::string> m_coIPCANType;
	int32_t m_iIPCANType;
	otl_value<std::string> m_coRAI;
	otl_value<std::string> m_coCGI;
	otl_value<std::string> m_coECGI;
	otl_value<std::string> m_coTAI;
	SUserLocationInfo() { m_bLoaded = false; }
};
struct SAllocationRetentionPriority {
	otl_value<uint32_t> m_coPriorityLevel;
	otl_value<int32_t> m_coPreemptionCapability;
	otl_value<uint32_t> m_coPreemptionVulnerability;
};
struct SDefaultEPSBearerQoS {
	otl_value<int32_t> m_coQoSClassIdentifier;
	otl_value<SAllocationRetentionPriority> m_soARP;
};
struct SRequestInfo {
	int32_t m_iCCRequestType;
	otl_value<std::string> m_coCCRequestType;
	otl_value<uint32_t> m_coCCRequestNumber;
	otl_value<std::string> m_coBearerIdentifier;
	otl_value<std::string> m_coOnlineCharging;
	otl_value<std::string> m_coOfflineCharging;
	otl_value<std::string> m_coQoSUpgrade;
	otl_value<uint32_t> m_coMaxRequestedBandwidthUl;
	otl_value<uint32_t> m_coMaxRequestedBandwidthDl;
	otl_value<uint32_t> m_coAPNAggregateMaxBitrateUL;
	otl_value<uint32_t> m_coAPNAggregateMaxBitrateDL;
	otl_value<uint32_t> m_coGuaranteedBitrateUl;
	otl_value<uint32_t> m_coGuaranteedBitrateDl;
	otl_value<std::string> m_coQoSNegotiation;
	SUserLocationInfo m_soUserLocationInfo;
	otl_value<std::string> m_coBearerUsage;
	otl_value<std::string> m_coBearerOperation;
	otl_value<SDefaultEPSBearerQoS> m_coDEPSBQoS;
	std::vector<SSessionUsageInfo> m_vectUsageInfo;
	std::vector<int32_t> m_vectEventTrigger;
};
struct SMsgDataForDB {
	struct SSessionInfo *m_psoSessInfo;
	struct SRequestInfo *m_psoReqInfo;
};
struct SPeerInfo {
	otl_value<std::string> m_coHostName;
	otl_value<std::string> m_coHostReal;
	unsigned int m_uiPeerProto;
};
/* структура для получения правил абонента из БД */
struct SDBAbonRule {
	bool m_bIsActivated;
	bool m_bIsRelevant;
	otl_value<std::string> m_coRuleName;
	otl_value<int32_t> m_coDynamicRuleFlag;
	otl_value<int32_t> m_coRuleGroupFlag;
	otl_value<int32_t> m_coPrecedenceLevel;
	otl_value<uint32_t> m_coRatingGroupId;
	otl_value<uint32_t> m_coServiceId;
	otl_value<int32_t> m_coMeteringMethod;
	otl_value<int32_t> m_coOnlineCharging;
	otl_value<int32_t> m_coOfflineCharging;
	otl_value<int32_t> m_coQoSClassIdentifier;
	otl_value<uint32_t> m_coMaxRequestedBandwidthUl;
	otl_value<uint32_t> m_coMaxRequestedBandwidthDl;
	otl_value<uint32_t> m_coGuaranteedBitrateUl;
	otl_value<uint32_t> m_coGuaranteedBitrateDl;
	otl_value<int32_t> m_coRedirectAddressType;
	otl_value<std::string> m_coRedirectServerAddress;
	std::vector<std::string> m_vectFlowDescr;
	otl_value<int32_t> m_coSCE_PackageId;
	otl_value<int32_t> m_coSCE_RealTimeMonitor;
	otl_value<int32_t> m_coSCE_UpVirtualLink;
	otl_value<int32_t> m_coSCE_DownVirtualLink;
	otl_value<std::string> m_coMonitKey;
	/* конструктор структуры */
	SDBAbonRule() { m_bIsActivated = false; m_bIsRelevant = false; }
};
/* выборка данных из пакета */
int pcrf_extract_req_data(msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo);
/* сохранение запроса в БД */
void fill_otl_datetime(otl_datetime &p_coOtlDateTime, tm &p_soTime);
int pcrf_server_DBstruct_init(struct SMsgDataForDB *p_psoMsgToDB);
int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo);
int pcrf_server_policy_db_store (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoMsgInfo);
void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo);
/* закрываем запись в таблице выданных политик */
int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	std::string &p_strRuleName);
/* добавление записи в таблицу выданых политик */
int pcrf_db_insert_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SDBAbonRule &p_soRule);

/* очередь элементов обновления политик */
struct SRefQueue {
	std::string m_strRowId;
	std::string m_strIdentifier;
	std::string m_strIdentifierType;
	otl_value<std::string> m_coAction;
	otl_datetime m_coRefreshDate;
};

/* операции клиента с БД */
/* формирование очереди изменения политик */
int pcrf_client_db_refqueue (otl_connect &p_coDBConn, std::vector<SRefQueue> &p_vectQueue);
/* завершение зависшей сессии */
int pcrf_client_db_fix_staled_sess (const char *p_pcszSessionId);
/* формирование списка сессий абонента */
int pcrf_client_db_load_session_list (
	otl_connect &p_coDBConn,
	SRefQueue &p_soReqQueue,
	std::vector<std::string> &p_vectSessionList);

/* запрос свободного подключения к БД */
int pcrf_db_pool_get (void **p_ppcoDBConn, const char *p_pszClient);
/* возврат подключения к БД */
int pcrf_db_pool_rel(void *p_pcoDBConn, const char *p_pszClient);

/* функции работы с AVP */
/* функция получения значения перечислимого типа */
int pcrf_extract_avp_enum_val (struct avp_hdr *p_psoAVPHdr, char *p_pszBuf, int p_iBufSize);

/* загрузка идентификатора абонента из БД */
int pcrf_server_db_load_abon_id (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB &p_soMsgInfo);
/* проверка зависших сессий */
int pcrf_server_db_look4stalledsession(otl_connect *p_pcoDBConn, SSessionInfo *p_psoSessInfo);
/* загрузка списка активных правил абонента */
int pcrf_server_db_load_active_rules (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive);
/* загрузка описания правила */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::string &p_strRuleName,
	std::vector<SDBAbonRule> &p_vectAbonRules);
/* загрузка идентификатора абонента по Session-Id */
int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo);
/* загрузка списка правил абонента из БД */
int pcrf_server_db_abon_rule (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules);
/* загрузка Monitoring Key из БД */
int pcrf_server_db_monit_key(
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo);
/* функция сохраняет в БД данные о локации абонента */
int pcrf_server_db_user_location(
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo);

/* функция формирования списка неактуальных правил */
int pcrf_server_select_notrelevant_active (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	std::vector<SDBAbonRule> &p_vectActive);

/* функция заполнения avp Charging-Rule-Remove */
struct avp * pcrf_make_CRR (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectActive);
/* функция заполнения avp Charging-Rule-Install */
struct avp * pcrf_make_CRI (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	msg *p_soAns);
/* функция заполнения avp Usage-Monitoring-Information */
int pcrf_make_UMI (
	msg_or_avp *p_psoMsgOrAVP,
	SSessionInfo &p_soSessInfo,
	bool p_bFull = true);
/* задает значение Event-Trigger */
int set_ULCh_event_trigger (
	SSessionInfo &p_soSessInfo,
	msg_or_avp *p_psoMsgOrAVP);

/* функция добавляет запись в очередь обновления политик */
int pcrf_server_db_insert_refqueue (
	otl_connect &p_coDBConn,
	const char *p_pszIdentifierType,
	const std::string &p_strIdentifier,
	otl_datetime *p_pcoDateTime,
	const char *p_pszAction);
/* функция удаляет запись из очереди обновления политик */
int pcrf_client_db_delete_refqueue (
	otl_connect &p_coDBConn,
	SRefQueue &p_soRefQueue);
/* функция определяет протокол пира */
int pcrf_peer_proto(SSessionInfo &p_soSessInfo);

#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif
