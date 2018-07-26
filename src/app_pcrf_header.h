#ifndef __APP_PCRF_HEADER_H__
#define __APP_PCRF_HEADER_H__

#include <freeDiameter/extension.h>
#include <stdint.h>

#include <time.h>
#include <string>
#include <vector>
#include <map>
#include <list>

#include "pcrf_common_data_types.h"
#include "pcrf_otl.h"
#include "utils/log/log.h"
#include "utils/timemeasurer/timemeasurer.h"
#include "utils/stat/stat.h"

#ifdef __cplusplus
extern "C" {
#endif

/* идентификаторы диалектов */
#define GX_UNDEF      0
#define GX_HW_UGW     1
#define GX_CISCO_SCE  2
#define GX_PROCERA    3
#define GX_ERICSSN    4
#define RX_IMS       10

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
	bool m_bIsReported;
	otl_value<uint64_t> m_coDosageTotalOctets;
	otl_value<uint64_t> m_coDosageOutputOctets;
	otl_value<uint64_t> m_coDosageInputOctets;
	SDBMonitoringInfo() { m_bIsReported = false; }
};
struct SSessionInfo {
	unsigned int m_uiPeerDialect;
	std::string  m_strSessionId;
	std::string  m_strSubscriberId;
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
  uint32_t               m_ui32FramedIPAddress;
	otl_value<otl_datetime> m_coTimeEnd;
	otl_value<std::string> m_coTermCause;
  std::list<SSF>         m_listSF; /* Supported-Features */
	std::vector<SSessionPolicyInfo> m_vectCRR; /* Charging-Rule-Report */
	otl_value<std::string> m_coCalledStationId; /* Called-Station-Id */
	std::map<std::string,SDBMonitoringInfo> m_mapMonitInfo;
  SSessionInfo()
  {
    m_uiPeerDialect = GX_UNDEF;
    m_ui32FramedIPAddress = static_cast<uint32_t>( -1 );
  };
};
struct SSessionUsageInfo {
	otl_value<std::string> m_coMonitoringKey;
	otl_value<uint64_t> m_coCCInputOctets;
	otl_value<uint64_t> m_coCCOutputOctets;
	otl_value<uint64_t> m_coCCTotalOctets;
};
struct SUserLocation {
  otl_value<std::string> m_coCGI;
  otl_value<std::string> m_coRAI;
  otl_value<std::string> m_coTAI;
  otl_value<std::string> m_coECGI;
};
/* парсинг 3GPP-User-Location-Info */
int pcrf_parse_user_location( avp_value *p_psoAVPValue, SUserLocation &p_soUserLocationInfo, bool *p_pbLoaded = NULL );
struct SUserEnvironment {
  bool m_bLoaded;
  otl_value<std::string> m_coSGSNMCCMNC;
  otl_value<std::string> m_coSGSNAddress; /* 3GPP-SGSN-Address */
  otl_value<std::string> m_coSGSNIPv6Address;
  otl_value<std::string> m_coRATType;
  int32_t m_iRATType;
  otl_value<std::string> m_coIPCANType;
  int32_t m_iIPCANType;
  SUserLocation          m_soUsrLoc;
  SUserEnvironment() : m_bLoaded(false), m_iRATType (0), m_iIPCANType( 0 ) { }
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
	SUserEnvironment m_soUserEnvironment;
	otl_value<std::string> m_coBearerUsage;
	otl_value<std::string> m_coBearerOperation;
	otl_value<SDefaultEPSBearerQoS> m_coDEPSBQoS;
  otl_value<uint32_t> m_coTetheringFlag;
	std::vector<SSessionUsageInfo> m_vectUsageInfo;
	std::vector<int32_t> m_vectEventTrigger;
  SRequestInfo() { m_iCCRequestType = 0; }
};
struct SMsgDataForDB {
	struct SSessionInfo *m_psoSessInfo;
	struct SRequestInfo *m_psoReqInfo;
  SMsgDataForDB() : m_psoSessInfo(NULL), m_psoReqInfo(NULL) {}
};
struct SPeerInfo {
	otl_value<std::string> m_coHostName;
	otl_value<std::string> m_coHostReal;
	unsigned int m_uiPeerDialect;
	int m_iIsConnected;
	SPeerInfo () { m_iIsConnected = 0; m_uiPeerDialect = GX_UNDEF; }
};
/* структура для получения правил абонента из БД */
struct SDBAbonRule {
	bool m_bIsActive;
	bool m_bIsRelevant;
	otl_value<std::string> m_coRuleName;
	otl_value<int32_t> m_coDynamicRuleFlag;
	otl_value<int32_t> m_coRuleGroupFlag;
	otl_value<int32_t> m_coPrecedenceLevel;
  otl_value<int32_t> m_coFlowStatus;
	otl_value<uint32_t> m_coRatingGroupId;
	otl_value<uint32_t> m_coServiceId;
	otl_value<int32_t> m_coMeteringMethod;
	otl_value<int32_t> m_coOnlineCharging;
	otl_value<int32_t> m_coOfflineCharging;
	otl_value<int32_t> m_coQoSClassIdentifier;
	SAllocationRetentionPriority m_soARP;
	otl_value<uint32_t> m_coMaxRequestedBandwidthUl;
	otl_value<uint32_t> m_coMaxRequestedBandwidthDl;
	otl_value<uint32_t> m_coGuaranteedBitrateUl;
	otl_value<uint32_t> m_coGuaranteedBitrateDl;
	otl_value<int32_t> m_coRedirectAddressType;
	otl_value<std::string> m_coRedirectServerAddress;
	std::vector<std::string> m_vectFlowDescr;
	otl_value<int32_t> m_coSCE_PackageId;
	otl_value<int32_t> m_coSCE_RealTimeMonitor;
	otl_value<uint32_t> m_coSCE_UpVirtualLink;
	otl_value<uint32_t> m_coSCE_DownVirtualLink;
	std::vector<std::string> m_vectMonitKey;
	/* конструктор структуры */
	SDBAbonRule() : m_bIsActive(false), m_bIsRelevant(false) { }
  SDBAbonRule( bool p_bIsActive, bool p_bIsRelevant ) : m_bIsActive( p_bIsActive ), m_bIsRelevant( p_bIsRelevant ) { }
};
/* выборка данных из пакета */
int pcrf_extract_req_data (msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo);

/* включение Supported-Features в запрос */
void pcrf_make_SF( msg_or_avp *p_psoAns, std::list<SSF> &p_listSupportedFeatures );

/* сохранение запроса в БД */
/* формирование даты/времени в структуре OTL, если параметр p_psoTime не задан используется текущее время */
void pcrf_fill_otl_datetime( otl_value<otl_datetime> &p_coOtlDateTime, tm *p_psoTime );
int pcrf_server_DBstruct_init(struct SMsgDataForDB *p_psoMsgToDB);
int pcrf_server_req_db_store( struct SMsgDataForDB *p_psoMsgInfo );
void pcrf_server_policy_db_store( SMsgDataForDB *p_psoMsgInfo );
void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo);
/* закрываем запись в таблице выданных политик */
void pcrf_db_close_session_rule (
	SSessionInfo *p_psoSessInfo,
	std::string &p_strRuleName,
  std::string *p_pstrRuleFailureCode = NULL);
/* закрывает все записи сессии в таблице выданных политик */
void pcrf_db_close_session_rule_all( std::string &p_strSessionId );
/* закрывает все открыте записи сессии в таблице локаций пользователя */
void pcrf_server_db_close_user_loc( std::string &p_strSessionId );
/* добавление записи в таблицу выданых политик */
void pcrf_db_insert_rule( SSessionInfo &p_soSessInfo, SDBAbonRule &p_soRule );

/* запись информации о потребленном трафике в БД  */
int pcrf_db_session_usage( otl_connect *p_pcoDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo, int &p_iUpdateRule );

/* очередь элементов обновления политик */
struct SRefQueue {
	std::string m_strRowId;
	std::string m_strIdentifier;
	std::string m_strIdentifierType;
	otl_value<std::string> m_coAction;
};

/* формирование полного списка правил */
int pcrf_server_create_abon_rule_list( otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo, std::vector<SDBAbonRule> &p_vectAbonRules );

/* операции клиента с БД */
/* формирование очереди изменения политик */
int pcrf_client_db_refqueue( otl_connect *p_pcoDBConn, std::vector<SRefQueue> &p_vectQueue );
/* завершение зависшей сессии */
void pcrf_client_db_fix_staled_sess( std::string &p_coSessionId );
/* формирование списка сессий абонента */
int pcrf_client_db_load_session_list( otl_connect *p_pcoDBConn, SRefQueue &p_soReqQueue, std::vector<std::string> &p_vectSessionList );
/* обновление записи в таблице сессий */
void pcrf_db_update_session(
  std::string &p_strSessionId,
  otl_value<otl_datetime> &p_coTimeEnd,
  otl_value<otl_datetime> &p_coTimeLast,
  otl_value<std::string> &p_coTermCause );

/* запрос свободного подключения к БД */
int pcrf_db_pool_get( otl_connect **p_ppcoDBConn, const char *p_pszClient, unsigned int p_uiWaitUSec );
/* возврат подключения к БД */
int pcrf_db_pool_rel(void *p_pcoDBConn, const char *p_pszClient);
/* восстановление подключения к БД */
/* возвращаемые значения:
* -1 - подключение было неработоспособно и восстановить его не удалось
*  0 - подключение работоспособно и его восстановление не потребовалось
*  1 - подключение было неработоспособно и оно восстановлено
*/
int pcrf_db_pool_restore( otl_connect *p_pcoDBConn );

/* функции работы с AVP */
/* функция получения значения перечислимого типа */
int pcrf_extract_avp_enum_val (struct avp_hdr *p_psoAVPHdr, char *p_pszBuf, int p_iBufSize);

/* загрузка идентификатора абонента из БД */
int pcrf_server_db_load_subscriber_id(otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo);
/* проверка зависших сессий */
int pcrf_server_look4stalledsession( SSessionInfo *p_psoSessInfo );
/* загрузка описания правила */
int pcrf_rule_cache_get_rule_info(
	std::string &p_strRuleName,
	SDBAbonRule &p_soRule);
/* поиск сессии в ядре для загрузки данных для SCE */
int pcrf_server_find_core_session( otl_connect *p_pcoDBConn, std::string &p_strSubscriberId, std::string &p_strFramedIPAddress, std::string &p_strUGWSessionId );
/* поиск сессии в ядре для загрузки данных для Procera */
int pcrf_server_find_core_sess_byframedip( std::string &p_strFramedIPAddress, SSessionInfo &p_soSessInfo );
/* поиск IP-CAN сессии */
int pcrf_server_find_IPCAN_sess_byframedip( otl_value<std::string> &p_coIPAddr, SSessionInfo &p_soIPCANSessInfo );
/* загрузка списка правил абонента из БД */
int pcrf_load_abon_rule_list( otl_connect *p_pcoDBConn, SMsgDataForDB &p_soMsgInfo, std::vector<std::string> &p_vectRuleList );
/* загрузка Monitoring Key из БД */
int pcrf_server_db_monit_key( otl_connect *p_pcoDBConn, SSessionInfo &p_soSessInfo );
/* функция сохраняет в БД данные о локации абонента */
void pcrf_server_db_user_location( SMsgDataForDB &p_soMsgInfo );

/* функция формирования списка неактуальных правил */
void pcrf_server_select_notrelevant_active(std::vector<SDBAbonRule> &p_vectAbonRules, std::vector<SDBAbonRule> &p_vectActive);

/* функция формирования списка ключей мониторинга */
void pcrf_make_mk_list( std::vector<SDBAbonRule> &p_vectAbonRules, SSessionInfo *p_psoSessInfo );

/* функция заполнения avp Charging-Rule-Remove */
struct avp * pcrf_make_CRR( SSessionInfo *p_psoSessInfo, std::vector<SDBAbonRule> &p_vectActive );
/* функция заполнения avp Charging-Rule-Install */
struct avp * pcrf_make_CRI( SSessionInfo *p_psoSessInfo, SRequestInfo *p_psoReqInfo, std::vector<SDBAbonRule> &p_vectAbonRules, msg *p_soAns );
/* функция заполнения avp Usage-Monitoring-Information */
int pcrf_make_UMI( msg_or_avp *p_psoMsgOrAVP, SSessionInfo &p_soSessInfo, bool p_bIsNeedUMR = false );
/* запись TETHERING_REPORT в БД */
void pcrf_server_db_insert_tethering_info( SMsgDataForDB &p_soMsgInfo );
/* задает значение Event-Trigger */
int set_event_trigger( msg_or_avp *p_psoMsgOrAVP, int32_t p_iTrigId );

/* функция добавляет запись в очередь обновления политик */
void pcrf_server_db_insert_refqueue(
	const char *p_pszIdentifierType,
	const std::string &p_strIdentifier,
	otl_datetime *p_pcoDateTime,
	const char *p_pszAction);

/* функция удаляет запись из очереди обновления политик */
int pcrf_client_db_delete_refqueue( otl_connect *p_pcoDBConn, SRefQueue &p_soRefQueue );

/* функция определяет протокол пира */
int pcrf_peer_dialect(SSessionInfo &p_soSessInfo);
/* определяет подключен ли пер */
int pcrf_peer_is_connected (SSessionInfo &p_soSessInfo);
/* определяет есть ли подключенные пиры заданного диалекта */
int pcrf_peer_is_dialect_used (unsigned int p_uiPeerDialect);

struct SRARResult {
  pthread_mutex_t m_mutexWait;
  int m_iResultCode;
  int Init ()
  {
    /* инициализируем мьютекс */
    CHECK_FCT( pthread_mutex_init( &m_mutexWait, NULL ) );
    /* блокируем его, т.к. он создается разблокированным */
    CHECK_FCT( pthread_mutex_lock( &m_mutexWait ) );

    return 0;
  }
  void Fini()
  {
    pthread_mutex_destroy( &m_mutexWait );
  }
  SRARResult() : m_iResultCode( 0 ) { }
  ~SRARResult() { }
};

/* функция для посылки RAR */
int pcrf_client_gx_rar(
  SSessionInfo *p_psoSessInfo,
  SRequestInfo *p_psoReqInfo,
  std::vector<SDBAbonRule> *p_pvectActiveRules,
  std::vector<SDBAbonRule> *p_pvectAbonRules,
  std::list<int32_t> *p_plistTrigger,
  SRARResult *p_psoRARRes,
  uint32_t p_uiUsec );

/* функция для Procera - формирование значения правила о локации пользователя */
int pcrf_procera_make_uli_rule (otl_value<std::string> &p_coULI, SDBAbonRule &p_soAbonRule);

/* загрузка активных сессий Procera по ip-адресу */
int pcrf_procera_db_load_sess_list( std::string &p_strUGWSessionId, std::vector<SSessionInfo> &p_vectSessList );

/* функция для закрытия всех правил локации сессии Procera */
int pcrf_procera_db_load_location_rule (otl_connect *p_pcoDBConn, std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectRuleList);

/* функция для добавления элемента в локальную очередь обновления политик */
void pcrf_local_refresh_queue_add( std::string &p_strSessionId );

#define NSEC_PER_USEC   1000L     
#define USEC_PER_SEC    1000000L
#define NSEC_PER_SEC    1000000000L
/* функция добавляет заданное значение p_uiAddUSec (мксек) к p_soTimeVal и записывает полученное значние в p_soTimeSpec */
int pcrf_make_timespec_timeout(timespec &p_soTimeSpec, uint32_t p_uiSec, uint32_t p_uiAddUSec);

/* преобразование ip-адреса к десятичному виду точками с качестве разделителей */
void pcrf_ip_addr_to_string(uint8_t *p_puiIPAddress, size_t p_stLen, otl_value<std::string> &p_coIPAddress);

/* кэш правил сессии */
int pcrf_session_rule_cache_get(std::string &p_strSessionId, std::vector<SDBAbonRule> &p_vectActive);
void pcrf_session_rule_cache_insert( std::string &p_strSessionId, std::string &p_strRuleName );
void pcrf_session_rule_cache_insert_local( std::string &p_strSessionId, std::string &p_strRuleName );
void pcrf_session_rule_cache_remove_rule(std::string &p_strSessionId, std::string &p_strRuleName);
void pcrf_session_rule_cache_remove_rule_local(std::string &p_strSessionId, std::string &p_strRuleName);
void pcrf_session_rule_cache_remove_sess_local(std::string &p_strSessionId);

#ifdef __cplusplus
}
#endif

struct SSQLQueueParam {
  virtual void push_data( otl_stream &p_coStream ) = 0;
  virtual ~SSQLQueueParam() { }
};

template<class T>
struct SSQLData : public SSQLQueueParam {
  T m_tParam;
  SSQLData( T &p_tParam ) : m_tParam( p_tParam ) { }
  void push_data( otl_stream &p_coStream ) { p_coStream << m_tParam; }
};

void pcrf_sql_queue_enqueue( const char *p_pszSQLRequest, std::list<SSQLQueueParam*> *p_plistParameters, const char *p_pszReqName, std::string *p_pstrSessionId = NULL );

template <class T>
void pcrf_sql_queue_add_param( std::list<SSQLQueueParam*> *p_plistParameters, T &p_tParam )
{
  SSQLData<T> *ptParam = new SSQLData<T>( p_tParam );
  p_plistParameters->push_back( ptParam );
}

/* отправка запроса на предоставление данных Usage-Monitoring */
int pcrf_send_umi_rar( otl_value<std::string> &p_coSubscriberId, std::list<std::string> *p_plistMonitKey );

/* функция генерации cdr */
int pcrf_cdr_write_cdr( SMsgDataForDB &p_soReqData );

#endif /* __APP_PCRF_HEADER_H__ */
