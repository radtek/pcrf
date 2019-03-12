#include <freeDiameter/extension.h>

/* module configuration */
struct SAppPCRFConf {
	char *m_pszDBServer;
	char *m_pszDBUser;
	char *m_pszDBPswd;
	char *m_pszDBDummyReq;
	int   m_iDBPoolSize;
	int   m_iDBPoolWait;
	int   m_iDBReqInterval;
	int   m_iOperateRefreshQueue;
	int   m_iLook4StalledSession;
	char *m_pszLogFileMask;
	int   m_iTraceReq;
	int   m_iGenerateCDR;
	char *m_pszCDRMask;
	char *m_pszCDRDir;
	char *m_pszCDRComplDir;
	int   m_iCDRInterval;
	unsigned int m_uiCCATimeoutSec;
	unsigned int m_uiCCATimeoutUSec;
	unsigned int m_uiMaxCCRHandlers;
	unsigned int m_uiDefaultQuota;
	unsigned int m_uiRefreshDefRuleIn;
};

extern struct SAppPCRFConf *g_psoConf;

/* объекты словаря */
extern struct dict_object *g_psoDictApp;
extern struct dict_object *g_psoDictVend;

#ifdef __cplusplus
extern "C" {	/* функции, реализованные на C++ */
#endif

/* Parse the configuration file */
int app_pcrf_conf_handle (char * conffile);

/* Initialize dictionary definitions */
int app_pcrf_dict_init (void);

/* инициализация пула подключений к БД */
int pcrf_db_pool_init (void);
/* деинициализация пула подключений к БД */
void pcrf_db_pool_fin (void);

/* инициализация трейсера */
int pcrf_tracer_rwlock_init();
int pcrf_tracer_init(void);
/* деинициализация трейсера */
void pcrf_tracer_rwlock_fini();
void pcrf_tracer_fini(void);

/* инициализация сервера */
int app_pcrf_serv_init (void);
/* деинициализация сервера */
void app_pcrf_serv_fini (void);

/* инициализация клиента */
int pcrf_cli_init (void);
/* деинициализация клиента */
void pcrf_cli_fini (void);

/* инициализация логгера */
int pcrf_logger_init (void);
/* деинициализаци логгера */
void pcrf_logger_fini(void);

/* инициализация списока клиентов и регистрация функции-валидатора клиента */
int app_pcrf_load_peer (void);

/* инициализация кеша сессий */
int pcrf_session_cache_init( pthread_t *p_ptThread );
/* деинициализация кеша сессий */
void pcrf_session_cache_fini (void);

/* инициализация кэша правил сессий */
int pcrf_session_rule_list_init( pthread_t *p_ptThread );
/* деинициализация кэша правил сессий */
void pcrf_session_rule_list_fini();

/* инициализация очереди sql-запросов */
int pcrf_sql_queue_init();
/* деинициализация очереди sql-запросов */
void pcrf_sql_queue_fini();

int pcrf_cdr_init();
void pcrf_cdr_fini();


/* Some global variables for dictionary */
extern struct dict_object *g_psoDictCCR;
extern struct dict_object *g_psoDictCCA;
extern struct dict_object *g_psoDictRAR;
extern struct dict_object *g_psoDictRAA;
extern struct dict_object *g_psoDictASR;
extern struct dict_object *g_psoDictASA;
extern struct dict_object *g_psoDictAuthApplicationId;
extern struct dict_object *g_psoDictSessionID;
extern struct dict_object *g_psoDictDestHost;
extern struct dict_object *g_psoDictDestRealm;
extern struct dict_object *g_psoDictRARType;
extern struct dict_object *g_psoDictRC;
extern struct dict_object *g_psoDictOrignHost;
extern struct dict_object *g_psoDictOrignRealm;

/* CC-Request-Type */
#define INITIAL_REQUEST     1
#define UPDATE_REQUEST      2
#define TERMINATION_REQUEST 3
#define EVENT_REQUEST       4

/* Subscription-Id-Type */
#define DIAM_END_USER_E164		0
#define DIAM_END_USER_IMSI		1
#define DIAM_END_USER_SIP_URI	2
#define DIAM_END_USER_NAI		3
#define DIAM_END_USER_PRIVATE	4

extern struct dict_object *g_psoDictCCRequestType;

extern struct dict_object *g_psoDictCCRequestNumber;
extern struct dict_object *g_psoDictOriginStateId;

extern struct dict_object *g_psoDictSupportedFeatures;
extern struct dict_object *g_psoDictVendorId;
extern struct dict_object *g_psoDictFeatureListID;
extern struct dict_object *g_psoDictFeatureList;

extern struct dict_object *g_psoDictEventTrigger;

extern struct dict_object *g_psoDictChargingRuleInstall;
extern struct dict_object *g_psoDictChargingRuleRemove;
extern struct dict_object *g_psoDictBearerIdentifier;
extern struct dict_object *g_psoDictChargingRuleDefinition;
extern struct dict_object *g_psoDictChargingRuleBaseName;
extern struct dict_object *g_psoDictChargingRuleName;
extern struct dict_object *g_psoDictRatingGroup;
extern struct dict_object *g_psoDictServiceIdentifier;
extern struct dict_object *g_psoDictAVPFlowStatus;
extern struct dict_object *g_psoDictAVPBearerControlMode;
extern struct dict_object *g_psoDictFlowDescription;
extern struct dict_object *g_psoDictAVPFlowDirection;
extern struct dict_object *g_psoDictSessionReleaseCause;
extern struct dict_object *g_psoDictFlowInformation;
extern struct dict_object *g_psoDicAVPtQoSInformation;
extern struct dict_object *g_psoDictQoSClassIdentifier;
extern struct dict_object *g_psoDictMaxRequestedBandwidthUL;
extern struct dict_object *g_psoDictMaxRequestedBandwidthDL;
extern struct dict_object *g_psoDictGuaranteedBitrateUL;
extern struct dict_object *g_psoDictGuaranteedBitrateDL;
extern struct dict_object *g_psoDictAllocationRetentionPriority;
extern struct dict_object *g_psoDictPriorityLevel;
extern struct dict_object *g_psoDictDefaultEPSBearerQoS;
extern struct dict_object *g_psoDictPreemptionCapability;
extern struct dict_object *g_psoDictPreemptionVulnerability;
extern struct dict_object *g_psoDictAVPReportingLevel;
extern struct dict_object *g_psoDictOnline;
extern struct dict_object *g_psoDictOffline;
extern struct dict_object *g_psoDictMeteringMethod;
extern struct dict_object *g_psoDictPrecedence;
extern struct dict_object *g_psoDictMonitoringKey;
extern struct dict_object *g_psoDictRedirectServer;
extern struct dict_object *g_psoDictRedirectAddressType;
extern struct dict_object *g_psoDictRedirectServerAddress;

extern struct dict_object *g_psoDictXHWMonitoringKey;
extern struct dict_object *g_psoDictXHWUsageReport;
extern struct dict_object *g_psoDictXHWSessionUsage;
extern struct dict_object *g_psoDictXHWServiceUsage;

extern struct dict_object *g_psoDictUsageMonitoringInformation;
extern struct dict_object *g_psoDictGrantedServiceUnit;
extern struct dict_object *g_psoDictCCTotalOctets;
extern struct dict_object *g_psoDictCCInputOctets;
extern struct dict_object *g_psoDictCCOutputOctets;
extern struct dict_object *g_psoDictRatingGroup;
extern struct dict_object *g_psoDictUsageMonitoringLevel;
extern struct dict_object *g_psoDictUsageMonitoringReport;
extern struct dict_object *g_psoDictUsageMonitoringSupport;

extern struct dict_object *g_psoDictCiscoBBPackageInstall;
extern struct dict_object *g_psoDictCiscoBBRTMonitorInstall;
extern struct dict_object *g_psoDictCiscoBBVlinkUStreamInstall;
extern struct dict_object *g_psoDictCiscoBBVlinkDStreamInstall;

extern struct dict_object *g_psoDictSubscriptionId;
extern struct dict_object *g_psoDictSubscriptionIdType;
extern struct dict_object *g_psoDictSubscriptionIdData;

extern struct dict_object *g_psoDictIPCANType;
extern struct dict_object *g_psoDictRATType;

extern struct dict_object *g_psoDictAVPAPNAggregateMaxBitrateDL;
extern struct dict_object *g_psoDicAVPtAPNAggregateMaxBitrateUL;
extern struct dict_object *g_psoDictAVPGxCapabilityList;

#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif
	