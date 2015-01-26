#define DEBUG

#include <freeDiameter/extension.h>

/* module configuration */
struct SAppPCRFConf {
	char *m_pszDBServer;
	char *m_pszDBUser;
	char *m_pszDBPswd;
	char *m_pszDBDummyReq;
	int m_iDBPoolSize;
	int m_iDBPoolWait;
	int m_iDBReqInterval;
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

/* инициализация сервера */
int app_pcrf_serv_init (void);
/* деинициализация сервера */
void app_pcrf_serv_fini (void);

/* инициализация клиента */
int pcrf_cli_init (void);
/* деинициализация клиента */
void pcrf_cli_fini (void);


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
extern struct dict_object *g_psoDictFlowDescription;
extern struct dict_object *g_psoDictFlowInformation;
extern struct dict_object *g_psoDictQoSInformation;
extern struct dict_object *g_psoDictQoSClassIdentifier;
extern struct dict_object *g_psoDictMaxRequestedBandwidthUL;
extern struct dict_object *g_psoDictMaxRequestedBandwidthDL;
extern struct dict_object *g_psoDictGuaranteedBitrateUL;
extern struct dict_object *g_psoDictGuaranteedBitrateDL;
extern struct dict_object *g_psoDictAllocationRetentionPriority;
extern struct dict_object *g_psoDictPriorityLevel;
extern struct dict_object *g_psoDictPreemptionCapability;
extern struct dict_object *g_psoDictPreemptionVulnerability;
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

#ifdef __cplusplus
}				/* функции, реализованные на C++ */
#endif
