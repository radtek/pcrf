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
extern "C" {	/* �������, ������������� �� C++ */
#endif

/* ������� ������� */
/* ������������ ������ �� ������� */
struct SSessionPolicyInfo {
	otl_value<std::string> m_coChargingRuleName;
	otl_value<std::string> m_coPCCRuleStatus;
	otl_value<std::string> m_coRuleFailureCode;
};
struct SSessionInfo {
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
	otl_value<otl_datetime> m_coTimeStart;
	otl_value<otl_datetime> m_coTimeEnd;
	otl_value<otl_datetime> m_coTimeLast;
	otl_value<std::string> m_coTermCause;
	otl_value<uint32_t> m_coFeatureListId;	/* Feature-List-Id */
	otl_value<uint32_t> m_coFeatureList;	/* Feature-List */
	std::vector<SSessionPolicyInfo> m_vectCRR; /* Charging-Rule-Report */
	otl_value<std::string> m_coCalledStationId; /* Called-Station-Id */
	otl_value<std::string> m_coSGSNAddress; /* 3GPP-SGSN-Address */
	int32_t m_iIPCANType;
	otl_value<std::string> m_coIPCANType;
};
struct SSessionUsageInfo {
	otl_value<std::string> m_coMonitoringKey;
	otl_value<uint64_t> m_coCCInputOctets;
	otl_value<uint64_t> m_coCCOutputOctets;
};
struct SRequestInfo {
	int32_t m_iCCRequestType;
	otl_value<std::string> m_coCCRequestType;
	otl_value<uint32_t> m_coCCRequestNumber;
	otl_value<std::string> m_coBearerIdentifier;
	otl_value<std::string> m_coOnlineCharging;
	otl_value<std::string> m_coOfflineCharging;
	otl_value<std::string> m_coQoSUpgrade;
	int32_t m_iQoSClassIdentifier;
	otl_value<std::string> m_coQoSClassIdentifier;
	otl_value<uint32_t> m_coMaxRequestedBandwidthUl;
	otl_value<uint32_t> m_coMaxRequestedBandwidthDl;
	otl_value<uint32_t> m_coGuaranteedBitrateUl;
	otl_value<uint32_t> m_coGuaranteedBitrateDl;
	otl_value<std::string> m_coRATType;
	otl_value<std::string> m_coQoSNegotiation;
	otl_value<std::string> m_coSGSNMCCMNC;
	otl_value<std::string> m_coUserLocationInfo;
	otl_value<std::string> m_coRAI;
	otl_value<std::string> m_coBearerUsage;
	otl_value<std::string> m_coRuleFailureCode;
	otl_value<std::string> m_coBearerOperation;
	std::vector<SSessionUsageInfo> m_vectUsageInfo;
};
struct SMsgDataForDB {
	struct SSessionInfo *m_psoSessInfo;
	struct SRequestInfo *m_psoReqInfo;
};
/* ���������� ������� */
struct SRuleId {
	/* ������������������� ������������� �������.
	   ���� m_uiProtocol = 1, �� m_uiRuleId �������� ps.rule.id 
	   ���� m_uiProtocol = 2, �� m_uiRuleId �������� ps.SCE_rule.id */
	unsigned int m_uiRuleId;
	unsigned int m_uiProtocol;
	/* ����������� ��������� */
	SRuleId () { m_uiRuleId = 0; m_uiProtocol = 0; }
};
/* ��������� ��� ��������� ���������� � ����������� */
struct SDBMonitoringInfo {
	otl_value<std::string> m_coKeyName;
	otl_value<uint64_t> m_coDosageTotalOctets;
	otl_value<uint64_t> m_coDosageOutputOctets;
	otl_value<uint64_t> m_coDosageInputOctets;
};
/* ��������� ��� ��������� ������ �������� �� �� */
struct SDBAbonRule {
	bool m_bIsActivated;
	SRuleId m_soRuleId;
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
	std::vector<SDBMonitoringInfo> m_vectMonitInfo;
	/* ����������� ��������� */
	SDBAbonRule () { m_bIsActivated = false; }
};
/* ���������� ������� � �� */
int pcrf_server_DBstruct_init (struct SMsgDataForDB *p_psoMsgToDB);
int pcrf_extract_req_data (msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo);
int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo);
int pcrf_server_policy_db_store (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoMsgInfo);
void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo);
/* ��������� ������ � ������� �������� ������� */
int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SRuleId &p_soRuleId);
/* ���������� ������ � ������� ������� ������� */
int pcrf_db_insert_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SDBAbonRule &p_soRule);

/* ������� ��������� ���������� ������� */
struct SRefQueue {
	std::string m_strSubscriberId;
	otl_datetime m_coRefreshDate;
};

/* �������� ������� � �� */
/* ������������ ������� ��������� ������� */
int pcrf_client_db_refqueue (otl_connect &p_coDBConn, std::vector<SRefQueue> &p_vectQueue);
/* ���������� �������� ������ */
int pcrf_client_db_fix_staled_sess (const char *p_pcszSessionId);
/* ������������ ������ ������ �������� */
int pcrf_client_db_load_session_list (
	otl_connect &p_coDBConn,
	const char *p_pcszSubscriberId,
	std::vector<std::string> &p_vectSessionList);

/* ������ ���������� ����������� � �� */
int pcrf_db_pool_get (void **p_ppcoDBConn);
/* ������� ����������� � �� */
int pcrf_db_pool_rel (void *p_pcoDBConn);
/* �������������� ����������� � �� */
/* ������������ ��������:
	-1 - ����������� ���� ���������������� � ������������ ��� �� �������
	0 - ����������� �������������� � ��� �������������� �� ������������� 
	1 - ����������� ���� ���������������� � ��� ������������� */
int pcrf_db_pool_restore (void *p_pcoDBConn);

/* ������� ������ � AVP */
/* ������� ��������� �������� ������������� ���� */
int pcrf_extract_avp_enum_val (struct avp_hdr *p_psoAVPHdr, char *p_pszBuf, int p_iBufSize);

/* �������� �������������� �������� �� �� */
int pcrf_server_db_load_abon_id (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo);
/* �������� ������ �������� ������ �������� */
int pcrf_server_db_load_active_rules (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive);
/* �������� �������� ������� */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	SRuleId &p_soRuleId,
	std::vector<SDBAbonRule> &p_vectAbonRules);
/* �������� �������������� �������� �� Session-Id */
int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo);
/* �������� ������ ������ �������� �� �� */
int pcrf_server_db_abon_rule (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules);

/* ������� ������������ ������ ������������ ������ */
int pcrf_server_select_notrelevant_active (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	std::vector<SDBAbonRule> &p_vectNotrelevant);

/* ������� ���������� avp Charging-Rule-Remove */
struct avp * pcrf_make_CRR (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectNotRelevantRules);
/* ������� ���������� avp Charging-Rule-Install */
struct avp * pcrf_make_CRI (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	msg *p_soAns);
/* ������� ���������� avp Usage-Monitoring-Information */
int pcrf_make_UMI (
	msg_or_avp *p_psoMsgOrAVP,
	SDBAbonRule &p_soAbonRule,
	bool p_bFull = true);
/* ������ �������� Event-Trigger */
int set_event_trigger (
	otl_connect *p_pcoDBConn,
	SSessionInfo &p_soSessInfo,
	msg_or_avp *p_psoMsgOrAVP);

/* ������� ��������� ������ � ������� ���������� ������� */
int pcrf_server_db_insert_refqueue (
	otl_connect &p_coDBConn,
	otl_datetime &p_coDateTime,
	std::string &p_strSubscriberId);
/* ������� ������� ������ �� ������� ���������� ������� */
int pcrf_client_db_delete_refqueue (
	otl_connect &p_coDBConn,
	SRefQueue &p_soRefQueue);

#ifdef __cplusplus
}				/* �������, ������������� �� C++ */
#endif
