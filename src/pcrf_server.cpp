#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <vector>
#include <stdio.h>

CLog *g_pcoLog = NULL;
extern struct SAppPCRFConf *g_psoConf;

/* handler for CCR req cb */
static disp_hdl * app_pcrf_hdl_ccr = NULL;

/* функция формирования avp 'QoS-Information' */
avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Charging-Rule-Definition */
avp * pcrf_make_CRD (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Supported-Features */
avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo);

/* функция заполнения Subscription-Id */
int pcrf_make_SI(msg *p_psoMsg, SMsgDataForDB &p_soReqInfo);

/* функция заполнения APN-Aggregate-Max-Bitrate */
int pcrf_make_APNAMBR(msg *p_psoMsg, SRequestInfo &p_soReqInfo);

/* функция заполнения Default-EPS-Bearer-QoS */
int pcrf_make_DefaultEPSBearerQoS(msg *p_psoMsg, SRequestInfo &p_soReqInfo);

/* функция заполнения avp X-HW-Usage-Report */
avp * pcrf_make_HWUR ();

/* выборка идентификатора абонента */
int pcrf_extract_SubscriptionId(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка данных об устройстве абонента */
int pcrf_extract_UEI(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка рапорта о назначении политик */
int pcrf_extract_CRR(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Supported-Features */
int pcrf_extract_SF(avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Usage-Monitoring-Information */
int pcrf_extract_UMI(avp *p_psoAVP, SRequestInfo &p_soReqInfo);
/* выборка значений Used-Service-Unit */
int pcrf_extract_USU(avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo);
/* парсинг Default-EPS-Bearer-QoS */
int pcrf_extract_DefaultEPSBearerQoS(avp *p_soAVPValue, SRequestInfo &p_soReqInfo);
/* парсинг 3GPP-User-Location-Info */
int pcrf_extract_user_location(avp_value &p_soAVPValue, SUserLocationInfo &p_soUserLocationInfo);
/* парсинг RAI */
int pcrf_extract_RAI(avp_value &p_soAVPValue, otl_value<std::string> &p_coValue);

static int app_pcrf_ccr_cb (
	msg ** p_ppsoMsg,
	avp * p_psoAVP,
	session * p_psoSess,
	void * opaque,
	enum disp_action * p_pAct)
{
	int iFnRes;
	msg *ans = NULL;
	avp *psoParentAVP = NULL;
	avp *psoChildAVP = NULL;
	union avp_value soAVPVal;
	SMsgDataForDB soMsgInfoCache;
	/* список правил профиля абонента */
	std::vector<SDBAbonRule> vectAbonRules;
	/* список активных правил абонента */
	std::vector<SDBAbonRule> vectActive;
	otl_connect *pcoDBConn = NULL;

	if (p_ppsoMsg == NULL) {
		return EINVAL;
	}

	/* инициализация структуры хранения данных сообщения */
	CHECK_POSIX_DO (pcrf_server_DBstruct_init (&soMsgInfoCache), /*continue*/);

	/* выбираем данные из сообщения */
	msg_or_avp *pMsgOrAVP = *p_ppsoMsg;
	pcrf_extract_req_data (pMsgOrAVP, &soMsgInfoCache);

	/* запрашиваем объект класса для работы с БД */
	CHECK_POSIX_DO(pcrf_db_pool_get((void **)&pcoDBConn, __func__), goto dummy_answer);

	/* дополняем данные запроса необходимыми параметрами */
	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
		/* загружаем идентификтор абонента из профиля абонента */
		CHECK_POSIX_DO(pcrf_server_db_load_abon_id(pcoDBConn, soMsgInfoCache), /*continue*/);
		/* проверка наличия зависших сессий */
		if (g_psoConf->m_iLook4StalledSession)
			CHECK_POSIX_DO(pcrf_server_db_look4stalledsession(pcoDBConn, soMsgInfoCache.m_psoSessInfo), /*continue*/);
		break;/* INITIAL_REQUEST */
	case 3: /* TERMINATION_REQUEST */
		{
			time_t tSecsSince1970;
			tm soTime;
			if ((time_t)-1 != time(&tSecsSince1970)) {
				if (localtime_r(&tSecsSince1970, &soTime)) {
					fill_otl_datetime(soMsgInfoCache.m_psoSessInfo->m_coTimeEnd.v, soTime);
					soMsgInfoCache.m_psoSessInfo->m_coTimeEnd.set_non_null();
				}
			}
		}
	default: /* DEFAULT */
		/* загружаем идентификатор абонента из списка активных сессий абонента */
		CHECK_POSIX_DO(pcrf_server_db_load_session_info(*(pcoDBConn), soMsgInfoCache), );
		break; /* DEFAULT */
	}

	/* сохраняем в БД запрос */
	CHECK_POSIX_DO(pcrf_server_req_db_store(*(pcoDBConn), &soMsgInfoCache), );

	/* загружаем правила из БД */
	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
	default: /* DEFAULT */
		/* загружаем из БД правила абонента */
		CHECK_POSIX_DO(pcrf_server_db_abon_rule(*(pcoDBConn), soMsgInfoCache, vectAbonRules), );
		break; /* INITIAL_REQUEST */ /* DEFAULT */
	case 3: /* TERMINATION_REQUEST */
		break; /* TERMINATION_REQUEST */
	}

	/* выполняем дополнительные действия с правилами */
	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 3: /* TERMINATION_REQUEST */
		break; /* TERMINATION_REQUEST */
	default: /* DEFAULT */
		/* загружаем список активных правил */
		CHECK_POSIX_DO(pcrf_server_db_load_active_rules(*(pcoDBConn), soMsgInfoCache, vectActive), );
	case 1: /* INITIAL_REQUEST */
		/* формируем список неактуальных правил */
		CHECK_POSIX_DO(pcrf_server_select_notrelevant_active(*(pcoDBConn), soMsgInfoCache, vectAbonRules, vectActive), );
		/* загружаем информацию о мониторинге */
		CHECK_POSIX_DO(pcrf_server_db_monit_key(*(pcoDBConn), *(soMsgInfoCache.m_psoSessInfo)), /* continue */);
		break; /* DEFAULT */ /* INITIAL_REQUEST */
	}

	dummy_answer:
	/* Create answer header */
	CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, p_ppsoMsg, 0), goto cleanup_and_exit);
	ans = *p_ppsoMsg;

	/* Auth-Application-Id */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAuthApplicationId, 0, &psoChildAVP), break);
		soAVPVal.u32 = 16777238;
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);

	/* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
	CHECK_FCT_DO (fd_msg_rescode_set (ans, (char *) "DIAMETER_SUCCESS", NULL, NULL, 1), /*continue*/);

	/* Destination-Realm */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictDestRealm, 0, &psoChildAVP), break);
		soAVPVal.os.data = (uint8_t *)soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.data();
		soAVPVal.os.len = soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);

	/* Destination-Host */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictDestHost, 0, &psoChildAVP), break);
		soAVPVal.os.data = (uint8_t *) soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.data();
		soAVPVal.os.len = soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);

	/* put 'CC-Request-Type' into answer */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictCCRequestType, 0, &psoChildAVP), break);
		soAVPVal.i32 = soMsgInfoCache.m_psoReqInfo->m_iCCRequestType;
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);
	/* put 'CC-Request-Number' into answer */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictCCRequestNumber, 0, &psoChildAVP), break);
		soAVPVal.u32 = soMsgInfoCache.m_psoReqInfo->m_coCCRequestNumber.v;
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);
	/* put 'Origin-State-Id' into answer */
	do {
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictOriginStateId, 0, &psoChildAVP), break);
		soAVPVal.u32 = soMsgInfoCache.m_psoSessInfo->m_coOriginStateId.v;
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal), break);
		CHECK_FCT_DO(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP), break);
	} while (0);

	/* APN-Aggregate-Max-Bitrate */
//	pcrf_make_APNAMBR(ans, *soMsgInfoCache.m_psoReqInfo);

	/* Default-EPS-Bearer-QoS */
//	pcrf_make_DefaultEPSBearerQoS(ans, *soMsgInfoCache.m_psoReqInfo);

	/* Subscription-Id */
//	pcrf_make_SI(ans, soMsgInfoCache);

	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
		/* Supported-Features */
		psoChildAVP = pcrf_make_SF (&soMsgInfoCache);
		if (psoChildAVP) {
			/* put 'Supported-Features' into answer */
			CHECK_FCT_DO (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP), /* continue */);
		}
		/* Event-Trigger */
		CHECK_FCT_DO(set_ULCh_event_trigger(*(soMsgInfoCache.m_psoSessInfo), ans), /* continue */);
		/* Usage-Monitoring-Information */
		CHECK_FCT_DO (pcrf_make_UMI (ans, *(soMsgInfoCache.m_psoSessInfo)), /* continue */ );
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI (pcoDBConn, &soMsgInfoCache, vectAbonRules, ans);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT_DO (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP), /*continue*/);
		}
		break; /* INITIAL_REQUEST */
	case 2: /* UPDATE_REQUEST */
		/* обрабатываем триггеры */
		{
			std::vector<int32_t>::iterator iter = soMsgInfoCache.m_psoReqInfo->m_vectEventTrigger.begin();
			for (; iter != soMsgInfoCache.m_psoReqInfo->m_vectEventTrigger.end(); iter++) {
				switch (*iter) {
				case 13: /* USER_LOCATION_CHANGE */
					/* Event-Trigger */
					CHECK_FCT_DO(set_ULCh_event_trigger(*(soMsgInfoCache.m_psoSessInfo), ans), /* continue */);
					break;
				case 20: /* DEFAULT_EPS_BEARER_QOS_CHANGE */
					/* Default-EPS-Bearer-QoS */
					pcrf_make_DefaultEPSBearerQoS(ans, *soMsgInfoCache.m_psoReqInfo);
					break;
				case 26: /* USAGE_REPORT */ /* Cisco SCE Gx notation */
					if (2 == soMsgInfoCache.m_psoSessInfo->m_uiPeerProto) {
						/* Usage-Monitoring-Information */
						CHECK_FCT_DO(pcrf_make_UMI(ans, *(soMsgInfoCache.m_psoSessInfo), false), /* continue */);
					}
					break;
				case 33: /* USAGE_REPORT */ /* 3GPP notation */
					if (1 == soMsgInfoCache.m_psoSessInfo->m_uiPeerProto) {
						/* Usage-Monitoring-Information */
						CHECK_FCT_DO(pcrf_make_UMI(ans, *(soMsgInfoCache.m_psoSessInfo), false), /* continue */);
					}
					break;
				}
			}
		}
		/* Charging-Rule-Remove */
		psoChildAVP = pcrf_make_CRR (pcoDBConn, &soMsgInfoCache, vectActive);
		/* put 'Charging-Rule-Remove' into answer */
		if (psoChildAVP) {
			CHECK_FCT_DO (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP), /*continue*/);
		}
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI (pcoDBConn, &soMsgInfoCache, vectAbonRules, ans);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT_DO (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP), /*continue*/);
		}
		break; /* UPDATE_REQUEST */
	}

	cleanup_and_exit:
	pcrf_server_DBStruct_cleanup (&soMsgInfoCache);

	/* освобождаем объект класса взаимодействия с БД */
	if (pcoDBConn)
		CHECK_POSIX_DO(pcrf_db_pool_rel((void *)pcoDBConn, __func__), /*continue*/);

	/* если ответ сформирован отправляем его */
	if (ans)
		CHECK_FCT_DO (fd_msg_send (p_ppsoMsg, NULL, NULL), /*continue*/);

	return 0;
}

static void pcrf_tracer(
	fd_hook_type p_eHookType,
	msg * p_psoMsg,
	peer_hdr * p_psoPeer,
	void * p_pOther,
	fd_hook_permsgdata *p_psoPMD,
	void * p_pRegData)
{
	int iFnRes;
	const char *pszPeerName = p_psoPeer ? p_psoPeer->info.pi_diamid : "<unknown peer>";
	char *pmcBuf = NULL;
	size_t stLen;
	msg_hdr *psoMsgHdr;
	otl_nocommit_stream coStream;
	char mcEnumValue[256];
	otl_connect *pcoDBConn = NULL;

	UTL_LOG_D(*g_pcoLog, "parameters dump: %#x:%p:%p:%p:%p:%p", p_eHookType, p_psoMsg, p_psoPeer, p_pOther, p_psoPMD, p_pRegData);

	if (NULL == p_psoMsg) {
		UTL_LOG_E(*g_pcoLog, "NULL pointer to message structure");
		return;
	}

	CHECK_FCT_DO(fd_msg_hdr(p_psoMsg, &psoMsgHdr), return);

	if (p_psoMsg) {
		CHECK_MALLOC_DO(
			fd_msg_dump_treeview(&pmcBuf, &stLen, NULL, p_psoMsg, fd_g_config->cnf_dict, 1, 1),
			{ UTL_LOG_E(*g_pcoLog, "Error while dumping a message"); return; });
	}

	std::string strSessionId;
	std::string strRequestType;
	std::string strCCReqType;
	std::string strOriginHost;
	std::string strOriginReal;
	std::string strDestinHost;
	std::string strDestinReal;
	std::string strResultCode;

	/* формируем Request Type */
	/* тип команды */
	switch (psoMsgHdr->msg_code) {
	case 257: /* Capabilities-Exchange */
		strRequestType += "CE";
		break;
	case 258: /* Re-Auth */
		strRequestType += "RA";
		break;
	case 265: /* AA */
		strRequestType += "AA";
		break;
	case 271: /* Accounting */
		strRequestType += "A";
		break;
	case 272: /* Credit-Control */
		strRequestType += "CC";
		break;
	case 274: /* Abort-Session */
		strRequestType += "AS";
		break;
	case 275: /* Session-Termination */
		strRequestType += "ST";
		break;
	case 280: /* Device-Watchdog */
		strRequestType += "DW";
		break;
	case 282: /* Disconnect-Peer */
		strRequestType += "DP";
		break;
	default:
	{
		char mcCode[256];
		iFnRes = snprintf(mcCode, sizeof(mcCode), "%u", psoMsgHdr->msg_code);
		if (iFnRes > 0) {
			if (iFnRes >= sizeof(mcCode))
				iFnRes = sizeof(mcCode) - 1;
			mcCode[iFnRes] = '\0';
			strRequestType += mcCode;
		}
	}
		break;
	}
	/* тип запроса */
	if (psoMsgHdr->msg_flags & CMD_FLAG_REQUEST) {
		strRequestType += 'R';
	} else {
		strRequestType += 'A';
	}
	/* добываем необходимые значения из запроса */
	msg_or_avp *psoMsgOrAVP;
	int iDepth;
	avp_hdr *psoAVPHdr;
	avp *psoAVP;
	iFnRes = fd_msg_browse_internal(p_psoMsg, MSG_BRW_FIRST_CHILD, &psoMsgOrAVP, &iDepth);
	if (iFnRes)
		goto free_and_exit;
	do {
		psoAVP = (avp*)psoMsgOrAVP;
		/* получаем заголовок AVP */
		if (fd_msg_avp_hdr((avp*)psoMsgOrAVP, &psoAVPHdr)) {
			continue;
		}
		/* нас интересуют лишь вендор Diameter */
		if (psoAVPHdr->avp_vendor != 0 && psoAVPHdr->avp_vendor != (vendor_id_t)-1) {
			continue;
		}
		switch (psoAVPHdr->avp_code) {
		case 263: /* Session-Id */
			strSessionId.insert(0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 264: /* Origin-Host */
			strOriginHost.insert(0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 268: /* Result-Code */
			{
				iFnRes = pcrf_extract_avp_enum_val(psoAVPHdr, mcEnumValue, sizeof(mcEnumValue));
				if (0 == iFnRes)
					strResultCode = mcEnumValue;
			}
			break;
		case 283: /* Destination-Realm */
			strDestinReal.insert(0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 293: /* Destination-Host */
			strDestinHost.insert(0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 296: /* Origin-Realm */
			strOriginReal.insert(0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 416: /* CC-Request-Type */
			{
				iFnRes = pcrf_extract_avp_enum_val(psoAVPHdr, mcEnumValue, sizeof(mcEnumValue));
				if (0 == iFnRes)
					strCCReqType = mcEnumValue;
			}
			break;
		}
	} while (0 == fd_msg_browse_internal(psoAVP, MSG_BRW_NEXT, &psoMsgOrAVP, &iDepth));
	/* опционально для CC определяем тип СС-запроса */
	if (psoMsgHdr->msg_code == 272 && strCCReqType.length()) {
		strRequestType += '-';
		strRequestType += strCCReqType[0];
	}
	/* проверяем наличие обязательных атрибутов */
	if (0 == strOriginHost.length()) {
		switch (p_eHookType)
		{
		case HOOK_MESSAGE_RECEIVED:
			strOriginHost = pszPeerName;
			break;
		case HOOK_MESSAGE_LOCAL:
		case HOOK_MESSAGE_SENT:
			strOriginHost.insert(0, (const char*) fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
			break;
		default:
			break;
		}
	}
	if (0 == strDestinHost.length()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			strDestinHost.insert(0, (const char*)fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
			break;
		case HOOK_MESSAGE_SENT:
			strDestinHost= pszPeerName;
			break;
		default:
			break;
		}
	}
	/* проверяем возможность заполнения опциональных атрибутов */
	if (0 == strOriginReal.length()) {
		switch (p_eHookType)
		{
		case HOOK_MESSAGE_SENT:
			strOriginReal.insert(0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
		default:
			break;
		}
	}
	if (0 == strDestinReal.length()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			strDestinReal.insert(0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
		default:
			break;
		}
	}

	/* пытаемся сохранить данные в БД */
	iFnRes = pcrf_db_pool_get((void**)&pcoDBConn, __func__);
	if (iFnRes)
		goto free_and_exit;
	try {
		otl_null coNull;
		otl_value<std::string> coOTLOriginReal;
		otl_value<std::string> coOTLDestinReal;
		otl_value<std::string> coOTLResultCode;
		if (strOriginReal.length())
			coOTLOriginReal = strOriginReal;
		if (strDestinReal.length())
			coOTLDestinReal = strDestinReal;
		if (strResultCode.length())
			coOTLResultCode = strResultCode;
		coStream.open(1,
			"insert into ps.requestList"
			"(seq_id,session_id,event_date,request_type,origin_host,origin_realm,destination_host,destination_realm,diameter_result,message)"
			"values"
			"(ps.requestlist_seq.nextval,:session_id/*char[255]*/,sysdate,:request_type/*char[10]*/,:origin_host/*char[100]*/,:origin_realm/*char[100]*/,:destination_host/*char[100]*/,:destination_realm/*char[100]*/,:diameter_result/*char[100]*/,:message/*char[32000]*/)",
			*pcoDBConn);
		coStream
			<< strSessionId
			<< strRequestType
			<< strOriginHost
			<< coOTLOriginReal
			<< strDestinHost
			<< coOTLDestinReal
			<< coOTLResultCode
			<< pmcBuf;
		pcoDBConn->commit();
		coStream.close();
	} catch (otl_exception coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		if (coStream.good())
			coStream.close();
	}

	free_and_exit:
	if (pcoDBConn) {
		pcrf_db_pool_rel(pcoDBConn, __func__);
	}

	if (pmcBuf) {
		fd_cleanup_buffer(pmcBuf);
	}
}

fd_hook_hdl *psoHookHandle = NULL;

int pcrf_tracer_init(void)
{
	int iRetVal = 0;

	CHECK_FCT(fd_hook_register(HOOK_MASK(HOOK_MESSAGE_RECEIVED, HOOK_MESSAGE_SENT), pcrf_tracer, NULL, NULL, &psoHookHandle));

	return iRetVal;
}

void pcrf_tracer_fini(void)
{
	if (psoHookHandle) {
		CHECK_FCT_DO(fd_hook_unregister(psoHookHandle), );
	}
}

int app_pcrf_serv_init (void)
{
	disp_when data;

	TRACE_DEBUG (FULL, "Initializing dispatch callbacks for test");

	memset (&data, 0, sizeof(data));
	data.app = g_psoDictApp;
	data.command = g_psoDictCCR;

	/* Now specific handler for CCR */
	CHECK_FCT (fd_disp_register (app_pcrf_ccr_cb, DISP_HOW_CC, &data, NULL, &app_pcrf_hdl_ccr));

	return 0;
}

void app_pcrf_serv_fini (void)
{
	if (app_pcrf_hdl_ccr) {
		(void) fd_disp_unregister (&app_pcrf_hdl_ccr, NULL);
	}
}

int pcrf_logger_init(void)
{
	int iRetVal = 0;

	g_pcoLog = new CLog;

	return g_pcoLog->Init (g_psoConf->m_pszLogFileMask);
}

void pcrf_logger_fini(void)
{
	g_pcoLog->Flush();
	delete g_pcoLog;
	g_pcoLog = NULL;
}

int pcrf_server_select_notrelevant_active (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	std::vector<SDBAbonRule> &p_vectActive)
{
	int iRetVal = 0;

	/* обходим все активные правила */
	std::vector<SDBAbonRule>::iterator iterRule;
	std::vector<SDBAbonRule>::iterator iterActive;
	/* цикл актуальных правил */
	for (iterRule = p_vectAbonRules.begin(); iterRule != p_vectAbonRules.end(); ++iterRule) {
		/* цикл активных правил */
		for (iterActive = p_vectActive.begin(); iterActive != p_vectActive.end(); ++iterActive) {
			/* если имена правил совпадают, значит активное правило актуально */
			if (iterActive->m_coRuleName.v == iterRule->m_coRuleName.v) {
				/* фиксируем, что правило активировано */
				iterRule->m_bIsActivated = true;
				iterActive->m_bIsRelevant = true;
				break;
			}
		}
	}

	return iRetVal;
}

avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule)
{
	avp *psoAVPQoSI = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	uint32_t ui32Value;

	do {
		/* QoS-Information */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictQoSInformation, 0, &psoAVPQoSI), return NULL);

		/* QoS-Class-Identifier */
		if (! p_soAbonRule.m_coQoSClassIdentifier.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictQoSClassIdentifier, 0, &psoAVPChild), return NULL);
			if (!p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.is_null()
					&& !p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_coQoSClassIdentifier.is_null()) {
				soAVPVal.i32 = p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_coQoSClassIdentifier.v;
			} else {
				soAVPVal.i32 = p_soAbonRule.m_coQoSClassIdentifier.v;
			}
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Max-Requested-Bandwidth-UL */
		if (! p_soAbonRule.m_coMaxRequestedBandwidthUl.is_null ()) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthUl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v ? p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictMaxRequestedBandwidthUL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Max-Requested-Bandwidth-DL */
		if (! p_soAbonRule.m_coMaxRequestedBandwidthDl.is_null ()) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthDl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.v ? p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictMaxRequestedBandwidthDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Guaranteed-Bitrate-UL */
		if (! p_soAbonRule.m_coGuaranteedBitrateUl.is_null ()) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateUl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.v ? p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateUl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL.v : ui32Value;
			}
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictGuaranteedBitrateUL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Guaranteed-Bitrate-DL */
		if (! p_soAbonRule.m_coGuaranteedBitrateDl.is_null ()) {
			ui32Value = p_soAbonRule.m_coGuaranteedBitrateDl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.v ? p_psoReqInfo->m_psoReqInfo->m_coGuaranteedBitrateDl.v : ui32Value;
			}
			if (!p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.is_null()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v ? p_psoReqInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL.v : ui32Value;
			}
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictGuaranteedBitrateDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Allocation-Retention-Priority */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAllocationRetentionPriority, 0, &psoAVPParent), return NULL);

		/* Priority-Level */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPriorityLevel, 0, &psoAVPChild), return NULL);
		if (!p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.is_null()
				&& !p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.is_null()
				&& !p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null()) {
			soAVPVal.u32 = p_psoReqInfo->m_psoReqInfo->m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
		} else {
			soAVPVal.u32 = p_soAbonRule.m_coQoSClassIdentifier.v;
		}
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* Pre-emption-Capability */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPreemptionCapability, 0, &psoAVPChild), return NULL);
		soAVPVal.i32 = 1;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* Pre-emption-Vulnerability */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPreemptionVulnerability, 0, &psoAVPChild), return NULL);
		soAVPVal.i32 = 0;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);

		/* put 'Allocation-Retention-Priority' into 'QoS-Information' */
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);

	} while (0);

	return psoAVPQoSI;
}

avp * pcrf_make_CRR (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectActive)
{
	/* если список пустой выходим ничего не делая */
	if (0 == p_vectActive.size()) {
		return NULL;
	}

	avp *psoAVPCRR = NULL; /* Charging-Rule-Remove */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	std::vector<SDBAbonRule>::iterator iter = p_vectActive.begin();

	/* обходим все элементы списка */
	for (; iter != p_vectActive.end(); ++iter) {
		/* если правило актуально переходим к другому */
		if (iter->m_bIsRelevant)
			continue;
		switch (p_psoReqInfo->m_psoSessInfo->m_uiPeerProto) {
		case 1: /* Gx */
			/* Charging-Rule-Remove */
			if (NULL == psoAVPCRR) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleRemove, 0, &psoAVPCRR), return NULL);
			}
			/* если это динамическое правило */
			if (! iter->m_coDynamicRuleFlag.is_null () && iter->m_coDynamicRuleFlag.v) {
				/* Charging-Rule-Name */
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleName, 0, &psoAVPChild), continue);
			}
			/* если это предопределенное правило */
			else {
				/* если это групповое правило */
				if (! iter->m_coRuleGroupFlag.is_null () && iter->m_coRuleGroupFlag.v) {
					/* Charging-Rule-Base-Name */
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleBaseName, 0, &psoAVPChild), continue);
				} else {
					/* Charging-Rule-Name */
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleName, 0, &psoAVPChild), continue);
				}
			}
			soAVPVal.os.data = (uint8_t *) iter->m_coRuleName.v.c_str ();
			soAVPVal.os.len  = (size_t) iter->m_coRuleName.v.length ();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), continue);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRR, MSG_BRW_LAST_CHILD, psoAVPChild), continue);
			if (p_pcoDBConn)
				CHECK_FCT_DO (pcrf_db_close_session_policy (*p_pcoDBConn, *(p_psoReqInfo->m_psoSessInfo), iter->m_coRuleName.v), );
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			if (p_pcoDBConn)
				CHECK_FCT_DO (pcrf_db_close_session_policy (*p_pcoDBConn, *(p_psoReqInfo->m_psoSessInfo), iter->m_coRuleName.v), );
			break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRR;
}

avp * pcrf_make_CRI (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	msg *p_soAns)
{
	/* если в списке нет ни одного правила */
	if (0 == p_vectAbonRules.size ()) {
		return NULL;
	}

	avp *psoAVPCRI = NULL; /* Charging-Rule-Install */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;

	std::vector<SDBAbonRule>::iterator iter = p_vectAbonRules.begin ();
	/* обходим все правила */
	for (; iter != p_vectAbonRules.end (); ++ iter) {
		/* если првило уже активировано переходим к следующей итерации */
		switch (p_psoReqInfo->m_psoSessInfo->m_uiPeerProto) {
		case 1: /* Gx */
			if (iter->m_bIsActivated) {
				continue;
			}
			/* Charging-Rule-Install */
			/* создаем avp 'Charging-Rule-Install' только по необходимости */
			if (NULL == psoAVPCRI) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleInstall, 0, &psoAVPCRI), return NULL);
				/* Bearer-Identifier */
				if (0 == p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType
						&& ! p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.is_null ()) {
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictBearerIdentifier, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.c_str ();
					soAVPVal.os.len = p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Bearer-Identifier' into 'Charging-Rule-Install' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
			}
			/* Charging-Rule-Definition */
			psoAVPChild = pcrf_make_CRD (p_psoReqInfo, *iter);
			if (psoAVPChild) {
				/* put 'Charging-Rule-Definition' into 'Charging-Rule-Install' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				/* сохраняем выданную политику в БД */
				if (p_pcoDBConn)
					CHECK_FCT_DO(pcrf_db_insert_policy(*p_pcoDBConn, *(p_psoReqInfo->m_psoSessInfo), *iter), /* continue */);
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			/* Cisco-SCA BB-Package-Install */
			if (! iter->m_coSCE_PackageId.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBPackageInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_PackageId.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Package-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Real-time-monitor-Install */
			if (! iter->m_coSCE_RealTimeMonitor.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBRTMonitorInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_RealTimeMonitor.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Real-time-monitor-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Vlink-Upstream-Install */
			if (! iter->m_coSCE_UpVirtualLink.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBVlinkUStreamInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_UpVirtualLink.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Vlink-Upstream-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Cisco-SCA BB-Vlink-Downstream-Install */
			if (! iter->m_coSCE_DownVirtualLink.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCiscoBBVlinkDStreamInstall, 0, &psoAVPChild), return NULL);
				soAVPVal.u32 = iter->m_coSCE_DownVirtualLink.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Cisco-SCA BB-Vlink-Downstream-Install' into answer */
				CHECK_FCT_DO (fd_msg_avp_add (p_soAns, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* сохраняем выданную политику в БД */
			if (p_pcoDBConn && !iter->m_bIsActivated)
				CHECK_FCT_DO(pcrf_db_insert_policy(*p_pcoDBConn, *(p_psoReqInfo->m_psoSessInfo), *iter), /* continue */);
			break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRI;
}

avp * pcrf_make_CRD (
	SMsgDataForDB *p_psoReqInfo,
	SDBAbonRule &p_soAbonRule)
{
	avp *psoAVPCRD = NULL;
	avp *psoAVPParent = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	int iIpCanType = -1;
	char mcValue[2048];
	int iFnRes;
	const char *pcszRuleName = NULL; /* имя правила для сохранения в БД */

	/* сохраняем значение IP-CAN-Type в локальной переменной, т.к. оно часто испольуется */
	iIpCanType = p_psoReqInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType;

	/* Charging-Rule-Definition */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleDefinition, 0, &psoAVPCRD), return NULL);
	}
	/* если это динамическое правило */
	if (! p_soAbonRule.m_coDynamicRuleFlag.is_null () && p_soAbonRule.m_coDynamicRuleFlag.v) {
		/* Charging-Rule-Name */
		{
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleName, 0, &psoAVPChild), return NULL);
			pcszRuleName = p_soAbonRule.m_coRuleName.v.c_str ();
			soAVPVal.os.data = (uint8_t *) pcszRuleName;
			soAVPVal.os.len  = (size_t) p_soAbonRule.m_coRuleName.v.length ();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Service-Identifier */
		if (! p_soAbonRule.m_coServiceId.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictServiceIdentifier, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coServiceId.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Rating-Group */
		if (! p_soAbonRule.m_coRatingGroupId.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRatingGroup, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coRatingGroupId.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Flow-Description */
		std::vector<std::string>::iterator iterFD = p_soAbonRule.m_vectFlowDescr.begin ();
		for (; iterFD != p_soAbonRule.m_vectFlowDescr.end (); ++ iterFD) {
			switch (iIpCanType) {
			case 0:
				/* Flow-Description */
				{
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowDescription, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) iterFD->c_str ();
					soAVPVal.os.len  = (size_t) iterFD->length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Flow-Information - in' into 'Charging-Rule-Definition' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
				break;
			case 5:
				/* Flow-Information */
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowInformation, 0, &psoAVPParent), return NULL);
				/* Flow-Description */
				{
					CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFlowDescription, 0, &psoAVPChild), return NULL);
					soAVPVal.os.data = (uint8_t *) iterFD->c_str ();
					soAVPVal.os.len  = (size_t) iterFD->length ();
					CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
					/* put 'Flow-Information - in' into 'Flow-Information' */
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				}
				/* put 'Flow-Information - in' into 'Charging-Rule-Definition' */
				{
					CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);
				}
				break;
			}
		}
		/* QoS-Information */
		psoAVPChild = pcrf_make_QoSI (p_psoReqInfo, p_soAbonRule);
		/* put 'QoS-Information' into 'Charging-Rule-Definition' */
		if (psoAVPChild) {
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Online */
		if (! p_soAbonRule.m_coOnlineCharging.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictOnline, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coOnlineCharging.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Offline */
		if (! p_soAbonRule.m_coOfflineCharging.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictOffline, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coOfflineCharging.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Metering-Method */
		if (! p_soAbonRule.m_coMeteringMethod.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMeteringMethod, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = p_soAbonRule.m_coMeteringMethod.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Precedence */
		if (! p_soAbonRule.m_coPrecedenceLevel.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPrecedence, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soAbonRule.m_coPrecedenceLevel.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Monitoring-Key */
		if (! p_soAbonRule.m_coMonitKey.is_null()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *)p_soAbonRule.m_coMonitKey.v.data();
			soAVPVal.os.len = (size_t)p_soAbonRule.m_coMonitKey.v.length();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Redirect-Server */
		if (! p_soAbonRule.m_coRedirectAddressType.is_null () && ! p_soAbonRule.m_coRedirectServerAddress.is_null ()) {
			/* Redirect-Server */
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectServer, 0, &psoAVPParent), return NULL);
			/* Redirect-Address-Type */
			{
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectAddressType, 0, &psoAVPChild), return NULL);
				soAVPVal.i32 = p_soAbonRule.m_coRedirectAddressType.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Redirect-Address-Type' into 'Redirect-Server' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* Redirect-Server-Address */
			{
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRedirectServerAddress, 0, &psoAVPChild), return NULL);
				soAVPVal.os.data = (uint8_t *) p_soAbonRule.m_coRedirectServerAddress.v.c_str ();
				soAVPVal.os.len  = (size_t) p_soAbonRule.m_coRedirectServerAddress.v.length ();
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Redirect-Server-Address' into 'Redirect-Server' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPParent, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* put 'Redirect-Server' into 'Charging-Rule-Definition' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPParent), return NULL);
		}
	}
	/* если это предопределенное правило */
	else {
		/* если это пакетное правило */
		if (! p_soAbonRule.m_coRuleGroupFlag.is_null () && p_soAbonRule.m_coRuleGroupFlag.v) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleBaseName, 0, &psoAVPChild), return NULL);
		} else {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleName, 0, &psoAVPChild), return NULL);
		}
		pcszRuleName = p_soAbonRule.m_coRuleName.v.c_str ();
		soAVPVal.os.data = (uint8_t *) pcszRuleName;
		soAVPVal.os.len  = (size_t) p_soAbonRule.m_coRuleName.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRD, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	}

	return psoAVPCRD;
}

avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo)
{
	avp * psoAVPSF = NULL;
	avp * psoAVPChild = NULL;
	avp_value soAVPVal;

	if (! p_psoReqInfo->m_psoSessInfo->m_coFeatureListId.is_null () && ! p_psoReqInfo->m_psoSessInfo->m_coFeatureList.is_null ()) {
		/* Supported-Features */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSupportedFeatures, 0, &psoAVPSF), return NULL);
		/* Vendor- Id */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictVendorId, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) 10415;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		/* Feature-List-Id */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFeatureListID, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) p_psoReqInfo->m_psoSessInfo->m_coFeatureListId.v;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		/* Feature-List */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictFeatureList, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = (uint32_t) p_psoReqInfo->m_psoSessInfo->m_coFeatureList.v;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPSF, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	} else {
		return NULL;
	}

	return psoAVPSF;
}

int pcrf_make_SI(msg *p_psoMsg, SMsgDataForDB &p_soReqInfo)
{
	int iRetVal = 0;
	avp *psoSI;
	avp *psoSIType;
	avp *psoSIData;
	avp_value soAVPVal;

	/* END_USER_E164 */
	if (!p_soReqInfo.m_psoSessInfo->m_coEndUserE164.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionId, 0, &psoSI), return 0);
		/* Subscription-Id-Type */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdType, 0, &psoSIType), return 0);
		soAVPVal.u32 = (uint32_t) 0; /* END_USER_E164 */
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIType, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIType), return 0);
		/* Subscription-Id-Data */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdData, 0, &psoSIData), return 0);
		soAVPVal.os.data = (uint8_t*) p_soReqInfo.m_psoSessInfo->m_coEndUserE164.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_coEndUserE164.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIData, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIData), return 0);
		/**/
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoSI), return 0);
	}

	/* END_USER_IMSI */
	if (!p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionId, 0, &psoSI), return 0);
		/* Subscription-Id-Type */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdType, 0, &psoSIType), return 0);
		soAVPVal.u32 = (uint32_t)1; /* END_USER_IMSI */
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIType, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIType), return 0);
		/* Subscription-Id-Data */
		memset(&soAVPVal, 0, sizeof(soAVPVal));
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSubscriptionIdData, 0, &psoSIData), return 0);
		soAVPVal.os.data = (uint8_t*)p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.v.data();
		soAVPVal.os.len = p_soReqInfo.m_psoSessInfo->m_coEndUserIMSI.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoSIData, &soAVPVal), return 0);
		CHECK_FCT_DO(fd_msg_avp_add(psoSI, MSG_BRW_LAST_CHILD, psoSIData), return 0);
		/**/
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoSI), return 0);
	}

	return iRetVal;
}

int pcrf_make_APNAMBR(msg *p_psoMsg, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;
	avp *psoAVPQoSI = NULL;
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;

	if (!p_soReqInfo.m_coAPNAggregateMaxBitrateDL.is_null() || !p_soReqInfo.m_coAPNAggregateMaxBitrateUL.is_null()) {
		/* QoS-Information */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictQoSInformation, 0, &psoAVPQoSI), return NULL);
		/* APN-Aggregate-Max-Bitrate-UL */
		if (!p_soReqInfo.m_coAPNAggregateMaxBitrateUL.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateUL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soReqInfo.m_coAPNAggregateMaxBitrateUL.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* APN-Aggregate-Max-Bitrate-DL */
		if (!p_soReqInfo.m_coAPNAggregateMaxBitrateDL.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAPNAggregateMaxBitrateDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = p_soReqInfo.m_coAPNAggregateMaxBitrateDL.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* put 'QoS-Information' into 'answer' */
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoAVPQoSI), return 0);
	}

	return iRetVal;
}

int pcrf_make_DefaultEPSBearerQoS(msg *p_psoMsg, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;
	avp *psoDEPSBQoS;
	avp *psoARP;
	avp *psoAVPChild;
	avp_value soAVPVal;

	/* Default-EPS-Bearer-QoS */
	if (!p_soReqInfo.m_coDEPSBQoS.is_null()) {
		/* Subscription-Id */
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictDefaultEPSBearerQoS, 0, &psoDEPSBQoS), return 0);
		/* QoS-Class-Identifier */
		if (!p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.is_null()) {
			memset(&soAVPVal, 0, sizeof(soAVPVal));
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictQoSClassIdentifier, 0, &psoAVPChild), return 0);
			soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
			CHECK_FCT_DO(fd_msg_avp_add(psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
		}
		/* Allocation-Retention-Priority */
		if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.is_null()) {
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAllocationRetentionPriority, 0, &psoARP), return 0);
			/* Priority-Level */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPriorityLevel, 0, &psoAVPChild), return 0);
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			/* Pre-emption-Capability */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPreemptionCapability, 0, &psoAVPChild), return 0);
				soAVPVal.i32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			/* Pre-emption-Vulnerability */
			if (!p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.is_null()) {
				memset(&soAVPVal, 0, sizeof(soAVPVal));
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictPreemptionVulnerability, 0, &psoAVPChild), return 0);
				soAVPVal.u32 = p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability.v;
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return 0);
				CHECK_FCT_DO(fd_msg_avp_add(psoARP, MSG_BRW_LAST_CHILD, psoAVPChild), return 0);
			}
			CHECK_FCT_DO(fd_msg_avp_add(psoDEPSBQoS, MSG_BRW_LAST_CHILD, psoARP), return 0);
		}
		CHECK_FCT_DO(fd_msg_avp_add(p_psoMsg, MSG_BRW_LAST_CHILD, psoDEPSBQoS), return 0);
	}

	return iRetVal;
}

int pcrf_make_UMI (
	msg_or_avp *p_psoMsgOrAVP,
	SSessionInfo &p_soSessInfo,
	bool p_bFull)
{
	/* если список пуст выходим из функции */
	if (0 == p_soSessInfo.m_mapMonitInfo.size())
		return 0;

	avp *psoAVPUMI = NULL; /* Usage-Monitoring-Information */
	avp *psoAVPGSU = NULL; /* Granted-Service-Unit */
	avp *psoAVPET = NULL;  /* Event-Trigger */
	avp *psoAVPChild = NULL;
	dict_avp_request soCrit;
	union avp_value soAVPVal;
	int iRetVal = 0;
	bool bEvenTriggerInstalled = false;

	std::map<std::string,SDBMonitoringInfo>::iterator iterMonitInfo = p_soSessInfo.m_mapMonitInfo.begin ();
	for (; iterMonitInfo != p_soSessInfo.m_mapMonitInfo.end (); ++iterMonitInfo) {
		/* если не задана ни одна квота */
		if (iterMonitInfo->second.m_coDosageTotalOctets.is_null ()
				&& iterMonitInfo->second.m_coDosageOutputOctets.is_null()
				&& iterMonitInfo->second.m_coDosageInputOctets.is_null()) {
			continue;
		}
		/* Usage-Monitoring-Information */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictUsageMonitoringInformation, 0, &psoAVPUMI), return NULL);
		/* Monitoring-Key */
		{
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *) iterMonitInfo->first.data();
			soAVPVal.os.len = (size_t) iterMonitInfo->first.length();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* Granted-Service-Unit */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictGrantedServiceUnit, 0, &psoAVPGSU), return NULL);
		/* CC-Total-Octets */
		if (! iterMonitInfo->second.m_coDosageTotalOctets.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCTotalOctets, 0, &psoAVPChild), return NULL);
			soAVPVal.u64 = iterMonitInfo->second.m_coDosageTotalOctets.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'CC-Total-Octets' into 'Granted-Service-Unit' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		} else {
			/* CC-Input-Octets */
			if (! iterMonitInfo->second.m_coDosageInputOctets.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
				soAVPVal.u64 = iterMonitInfo->second.m_coDosageInputOctets.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'CC-Input-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* CC-Output-Octets */
			if (! iterMonitInfo->second.m_coDosageOutputOctets.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
				soAVPVal.u64 = iterMonitInfo->second.m_coDosageOutputOctets.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'CC-Output-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
		}
		/* put 'Granted-Service-Unit' into 'Usage-Monitoring-Information' */
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPGSU), return NULL);
		/* допольнительные параметры */
		if (!p_bFull && 2 == p_soSessInfo.m_uiPeerProto) {
			/* Usage-Monitoring-Level */
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictUsageMonitoringLevel, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = 1;  /* PCC_RULE_LEVEL */
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return NULL);
			/* put 'Usage-Monitoring-Level' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		if (p_bFull) {
			/* Usage-Monitoring-Level */
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictUsageMonitoringLevel, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = 1;  /* PCC_RULE_LEVEL */
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'Usage-Monitoring-Level' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			/* Usage-Monitoring-Report */
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictUsageMonitoringReport, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = 0; /* USAGE_MONITORING_REPORT_REQUIRED */
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'Usage-Monitoring-Report' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		if (psoAVPUMI) {
			/* Event-Trigger */
			if (!bEvenTriggerInstalled) {
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictEventTrigger, 0, &psoAVPET), return NULL);
				switch (p_soSessInfo.m_uiPeerProto) {
				default:
				case 1: /* Gx */
					soAVPVal.i32 = 33;
					break;
				case 2: /* Gx Cisco SCE */
					soAVPVal.i32 = 26;
					break;
				}
				CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPET, &soAVPVal), return NULL);
				CHECK_FCT(fd_msg_avp_add(p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVPET));
				bEvenTriggerInstalled = true;
			}
			CHECK_FCT (fd_msg_avp_add (p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVPUMI));
		}
	}

	return iRetVal;
}

avp * pcrf_make_HWUR ()
{
	avp
		*psoAVPHWUR = NULL, /* X-HW-Usage-Report */
		*psoAVPHWSrvU = NULL, /* X-HW-Service-Usage */
		*psoAVPHWSsnU = NULL, /* X-HW-Session-Usage */
		*psoAVPChild = NULL;
	union avp_value soVal;

	/* X-HW-Usage-Report */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWUsageReport, 0, &psoAVPHWUR), return NULL);

	/* X-HW-Service-Usage */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWServiceUsage, 0, &psoAVPHWSrvU), return NULL);
	/* Rating-Group */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRatingGroup, 0, &psoAVPChild), return NULL);
	soVal.u32 = (uint32_t) 1;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'Rating-Group' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Input-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Input-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Output-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Output-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSrvU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* put 'X-HW-Service-Usage' into 'Usage-Monitoring-Information' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWUR, MSG_BRW_LAST_CHILD, psoAVPHWSrvU), return NULL);

	/* X-HW-Session-Usage */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictXHWSessionUsage, 0, &psoAVPHWSsnU), return NULL);
	/* CC-Input-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Input-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSsnU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* CC-Output-Octets */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
	soVal.u64 = (uint64_t) 10000000;
	CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soVal), return NULL);
	/* put 'CC-Output-Octets' into 'X-HW-Service-Usage' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWSsnU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	/* put 'X-HW-Service-Usage' into 'Usage-Monitoring-Information' */
	CHECK_FCT_DO (fd_msg_avp_add (psoAVPHWUR, MSG_BRW_LAST_CHILD, psoAVPHWSsnU), return NULL);

	/* X-HW-Session-Usage */

	return psoAVPHWUR;
}

int set_ULCh_event_trigger (
	SSessionInfo &p_soSessInfo,
	msg_or_avp *p_psoMsgOrAVP)
{
	int iRetVal = 0;
	avp *psoAVP;
	avp_value soAVPValue;

	/* USER_LOCATION_CHANGE */
	switch (p_soSessInfo.m_uiPeerProto) {
	case 1: /* Gx */
		CHECK_FCT(fd_msg_avp_new(g_psoDictEventTrigger, 0, &psoAVP));
		soAVPValue.i32 = 13;
		CHECK_FCT(fd_msg_avp_setvalue(psoAVP, &soAVPValue));
		CHECK_FCT(fd_msg_avp_add(p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVP));
		break; /* Gx */
	}

	return iRetVal;
}

int pcrf_extract_req_data(msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;

	/* проверка параметров */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
		|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	int iDepth;
	struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	char mcValue[0x10000];
	vendor_id_t tVenId;

	/* ищем первую AVP */
	iRetVal = fd_msg_browse_internal(p_psoMsgOrAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		if (AVP_FLAG_VENDOR & psoAVPHdr->avp_flags) {
			tVenId = psoAVPHdr->avp_vendor;
		} else {
			tVenId = (vendor_id_t)-1;
		}
		switch (tVenId) {
		case (vendor_id_t)-1: /* vendor undefined */
		case 0: /* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 8: /* Framed-IP-Address */
				if (4 == psoAVPHdr->avp_value->os.len) {
					int iStrLen;
					iStrLen = snprintf(mcValue, sizeof(mcValue) - 1, "%u.%u.%u.%u", psoAVPHdr->avp_value->os.data[0], psoAVPHdr->avp_value->os.data[1], psoAVPHdr->avp_value->os.data[2], psoAVPHdr->avp_value->os.data[3]);
					if (iStrLen > 0) {
						if (iStrLen >= sizeof(mcValue)) {
							mcValue[sizeof(mcValue) - 1] = '\0';
						}
						p_psoMsgInfo->m_psoSessInfo->m_coFramedIPAddress = mcValue;
					}
				}
				break;
			case 30: /* Called-Station-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.set_non_null();
				break;
			case 263: /* Session-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.set_non_null();
				break;
			case 264: /* Origin-Host */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.set_non_null();
				/* определяем протокол пира */
				pcrf_peer_proto(*p_psoMsgInfo->m_psoSessInfo);
				break;
			case 278: /* Origin-State-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginStateId = psoAVPHdr->avp_value->u32;
				break;
			case 296: /* Origin-Realm */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.set_non_null();
				/* определяем протокол пира */
				pcrf_peer_proto(*p_psoMsgInfo->m_psoSessInfo);
				break;
			case 295: /* Termination-Cause */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoSessInfo->m_coTermCause = mcValue;
				}
				break;
			case 416: /* CC-Request-Type */
				p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coCCRequestType = mcValue;
				}
				break;
			case 443: /* Subscription-Id */
				pcrf_extract_SubscriptionId(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 415: /* CC-Request-Number */
				p_psoMsgInfo->m_psoReqInfo->m_coCCRequestNumber = psoAVPHdr->avp_value->u32;
				break;
			case 458: /* User-Equipment-Info */
				pcrf_extract_UEI(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			}
			break; /* Diameter */ /* vendor undefined */
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 6: /* 3GPP-SGSN-Address */
			{
				char mcIPAddr[16];
				sprintf(mcIPAddr, "%u.%u.%u.%u", psoAVPHdr->avp_value->os.data[0], psoAVPHdr->avp_value->os.data[1], psoAVPHdr->avp_value->os.data[2], psoAVPHdr->avp_value->os.data[3]);
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNAddress = mcIPAddr;
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
			}
			break;
			case 18: /* 3GPP-SGSN-MCC-MNC */
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coSGSNMCCMNC.set_non_null();
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				break;
			case 21: /* 3GPP-RAT-Type */
				if (!psoAVPHdr->avp_value->os.len)
					break;
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				switch (psoAVPHdr->avp_value->os.data[0]) {
				case 1:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "UTRAN";
					break;
				case 2:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = " GERAN";
					break;
				case 3:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "WLAN";
					break;
				case 4:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "GAN";
					break;
				case 5:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = "HSPA Evolution";
					break;
				default:
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = false;
					break;
				}
				break;
			case 22: /* 3GPP-User-Location-Info */
				pcrf_extract_user_location(*psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo);
				break;
			case 515: /* Max-Requested-Bandwidth-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl = psoAVPHdr->avp_value->u32;
				break;
			case 516: /* Max-Requested-Bandwidth-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl = psoAVPHdr->avp_value->u32;
				break;
			case 628: /* Supported-Features */
				pcrf_extract_SF(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 909: /* RAI */
				pcrf_extract_RAI(*psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRAI);
				break;
			case 1000: /* Bearer-Usage */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerUsage = mcValue;
				}
				break;
			case 1006: /* Event-Trigger */
				{
					p_psoMsgInfo->m_psoReqInfo->m_vectEventTrigger.push_back(psoAVPHdr->avp_value->i32);
				}
				break;
			case 1009: /* Online */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOnlineCharging = mcValue;
				}
				break;
			case 1008: /* Offline */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOfflineCharging = mcValue;
				}
				break;
			case 1016: /* QoS-Information */
				pcrf_extract_req_data((void *)psoAVP, p_psoMsgInfo);
				break;
			case 1018: /* Charging-Rule-Report */
				pcrf_extract_CRR(psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 1020: /* Bearer-Identifier */
				if (p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.is_null()) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.set_non_null();
				}
				break;
			case 1021: /* Bearer-Operation */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerOperation = mcValue;
				}
				break;
			case 1025: /* Guaranteed-Bitrate-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coGuaranteedBitrateDl = psoAVPHdr->avp_value->u32;
				break;
			case 1026: /* Guaranteed-Bitrate-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coGuaranteedBitrateUl = psoAVPHdr->avp_value->u32;
				break;
			case 1027: /* IP-CAN-Type */
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType = psoAVPHdr->avp_value->i32;
				p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_bLoaded = true;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType = mcValue;
				}
				break;
			case 1029: /* QoS-Negotiation */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSNegotiation = mcValue;
				}
				break;
			case 1030: /* QoS-Upgrade */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSUpgrade = mcValue;
				}
				break;
			case 1032: /* RAT-Type */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_soUserLocationInfo.m_coRATType = mcValue;
				}
				break;
			case 1040: /* APN-Aggregate-Max-Bitrate-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateDL = psoAVPHdr->avp_value->u32;
				break;
			case 1041: /* APN-Aggregate-Max-Bitrate-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coAPNAggregateMaxBitrateUL = psoAVPHdr->avp_value->u32;
				break;
			case 1049: /* Default-EPS-Bearer-QoS */
				p_psoMsgInfo->m_psoReqInfo->m_coDEPSBQoS.set_non_null();
				pcrf_extract_DefaultEPSBearerQoS(psoAVP, *p_psoMsgInfo->m_psoReqInfo);
				break;
			case 1067: /* Usage-Monitoring-Information */
				pcrf_extract_UMI(psoAVP, *(p_psoMsgInfo->m_psoReqInfo));
				break;
			}
			break; /* 3GPP */
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_extract_SubscriptionId(avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	int iSubscriptionIdType = -1;
	std::string strSubscriptionIdData;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 450: /* Subscription-Id-Type */
				iSubscriptionIdType = psoAVPHdr->avp_value->i32;
				break;
			case 444: /* Subscription-Id-Data */
				strSubscriptionIdData.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	/* исправляем косяк циски */
	if (2 == p_soSessInfo.m_uiPeerProto /* Gx Cisco SCE */
			&& iSubscriptionIdType == 0) { /* END_USER_E164 */
		iSubscriptionIdType = 1; /* END_USER_IMSI */
	}

	/* исправляем косяк циски */
	if (2 == p_soSessInfo.m_uiPeerProto /* Gx Cisco SCE */
			&& iSubscriptionIdType == 1) { /* END_USER_IMSI */
		if (strSubscriptionIdData.length() > 15) {
			size_t stPos;
			stPos = strSubscriptionIdData.find("_");
			if (stPos != std::string::npos)
				strSubscriptionIdData.resize(stPos);
		}
	}

	if (strSubscriptionIdData.length()) {
		switch (iSubscriptionIdType) {
		case 0: /* END_USER_E164 */
			p_soSessInfo.m_coEndUserE164 = strSubscriptionIdData;
			break;
		case 1: /* END_USER_IMSI */
			p_soSessInfo.m_coEndUserIMSI = strSubscriptionIdData;
			break;
		case 2: /* END_USER_SIP_URI */
			p_soSessInfo.m_coEndUserSIPURI = strSubscriptionIdData;
			break;
		case 3: /* END_USER_NAI */
			p_soSessInfo.m_coEndUserNAI = strSubscriptionIdData;
			break;
		case 4: /* END_USER_PRIVATE */
			p_soSessInfo.m_coEndUserPrivate = strSubscriptionIdData;
			break;
		}
	}

	return iRetVal;
}

int pcrf_extract_UEI(avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	int iType = -1;
	std::string strInfo;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 459: /* User-Equipment-Info-Type */
				iType = psoAVPHdr->avp_value->i32;
				break;
			case 460: /* User-Equipment-Info-Value */
				strInfo.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	switch (iType) {
	case 0: /* IMEISV */
		p_soSessInfo.m_coIMEI = strInfo;
		break;
	}

	return iRetVal;
}

int pcrf_extract_CRR(avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	SSessionPolicyInfo soPolicy;
	char mcValue[0x10000];

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1005: /* Charging-Rule-Name */
				soPolicy.m_coChargingRuleName.v.insert(0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soPolicy.m_coChargingRuleName.set_non_null();
				break;
			case 1019: /* PCC-Rule-Status */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coPCCRuleStatus = mcValue;
				}
				break;
			case 1031: /* Rule-Failure-Code */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					soPolicy.m_coRuleFailureCode = mcValue;
				}
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	p_soSessInfo.m_vectCRR.push_back(soPolicy);

	return iRetVal;
}

int pcrf_extract_SF(avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	char mcValue[0x10000];

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 629: /* Feature-List-Id */
				p_soSessInfo.m_coFeatureListId = psoAVPHdr->avp_value->u32;
				break;
			case 630: /* Feature-List */
				p_soSessInfo.m_coFeatureList = psoAVPHdr->avp_value->u32;
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_extract_UMI(avp *p_psoAVP, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	SSessionUsageInfo soUsageInfo;
	bool bDone = false;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1066: /* Monitoring-Key */
				bDone = true;
				soUsageInfo.m_coMonitoringKey.v.assign((const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soUsageInfo.m_coMonitoringKey.set_non_null();
				break; /* Monitoring-Key */
			}
			break; /* 3GPP */
		case 0:	/* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 446: /* Used-Service-Unit */
				pcrf_extract_USU(psoAVP, soUsageInfo);
				break;
			}
			break;	/* Diameter */
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	if (bDone) {
		p_soReqInfo.m_vectUsageInfo.push_back(soUsageInfo);
	}

	return iRetVal;
}

int pcrf_extract_USU(avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;

	iRetVal = fd_msg_browse_internal((void *)p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			break;
		case 0:
			switch (psoAVPHdr->avp_code) {
			case 412: /* CC-Input-Octets */
				p_soUsageInfo.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
				break;
			case 414: /* CC-Output-Octets */
				p_soUsageInfo.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
				break;
			case 421: /* CC-Total-Octets */
				p_soUsageInfo.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_extract_DefaultEPSBearerQoS(avp *p_soAVP, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;

	iRetVal = fd_msg_browse_internal((void *)p_soAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr(psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1028: /* QoS-Class-Identifier */
				p_soReqInfo.m_coDEPSBQoS.v.m_coQoSClassIdentifier = psoAVPHdr->avp_value->i32;
				break;
			case 1034: /* Allocation-Retention-Priority */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.set_non_null();
				pcrf_extract_DefaultEPSBearerQoS(psoAVP, p_soReqInfo);
				break;
			case 1046: /* Priority-Level */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPriorityLevel = psoAVPHdr->avp_value->u32;
				break;
			case 1047: /* Pre-emption-Capability */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionCapability = psoAVPHdr->avp_value->i32;
				break;
			case 1048: /* Pre-emption-Vulnerability */
				p_soReqInfo.m_coDEPSBQoS.v.m_soARP.v.m_coPreemptionVulnerability = psoAVPHdr->avp_value->u32;
				break;
			}
			break;
		case 0:
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, &iDepth));

	return iRetVal;
}

#pragma pack(push, 1)
struct SMCCMNC {
	unsigned m_uiMCC1 : 4;
	unsigned m_uiMCC2 : 4;
	unsigned m_uiMCC3 : 4;
	unsigned m_uiMNC3 : 4;
	unsigned m_uiMNC1 : 4;
	unsigned m_uiMNC2 : 4;
};
struct SLAC {
	unsigned m_uiLAC1 : 8;
	unsigned m_uiLAC2 : 8;
};
struct SCI {
	unsigned m_uiCI1 : 8;
	unsigned m_uiCI2 : 8;
};
struct SSAS {
	unsigned m_uiSAS1 : 8;
	unsigned m_uiSAS2 : 8;
};
struct SRAC {
	unsigned m_uiRAC : 8;
	unsigned m_uiPadding : 8;
};
struct STAC {
	unsigned m_uiTAC1 : 8;
	unsigned m_uiTAC2 : 8;
};
struct SECI {
	unsigned m_uiECI1 : 4;
	unsigned m_uiPadding : 4;
	unsigned m_uiECI2 : 8;
	unsigned m_uiECI3 : 8;
	unsigned m_uiECI4 : 8;
};
struct SCGI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SCI m_soCI;
};
struct SSAI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SSAS m_soSAS;
};
struct SRAI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SRAC m_soRAC;
};
struct STAI {
	SMCCMNC m_soMCCMNC;
	STAC m_soTAC;
};
struct SECGI {
	SMCCMNC m_soMCCMNC;
	SECI m_soECI;
};
struct STAI_ECGI {
	STAI m_soTAI;
	SECGI m_soECGI;
};
#pragma pack(pop)

enum EUserLocationType {
	eCGI = 0,
	eSAI = 1,
	eRAI = 2,
	eTAI = 128,
	eECGI = 129,
	eTAI_ECGI = 130
};

void format_CGI(SCGI &p_soCGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_SAI(SSAI &p_soSAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_RAI(SRAI &p_soRAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_TAI(STAI &p_soTAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_ECGI(SECGI &p_soECGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);

int pcrf_extract_user_location(avp_value &p_soAVPValue, SUserLocationInfo &p_soUserLocationInfo)
{
	int iRetVal = 0;
	int iFnRes;
	char mcDigit[32];

	SCGI soCGI;
	SSAI soSAI;
	SRAI soRAI;
	STAI soTAI;
	SECGI soECGI;
	STAI_ECGI soTAI_ECGI;

	SMCCMNC *psoMCCMNC = NULL;
	char mcMCCMNC[8];

	switch (p_soAVPValue.os.data[0]) {
	case eCGI:
		if (p_soAVPValue.os.len < sizeof(soCGI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SCGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soCGI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soCGI.m_soMCCMNC);
		break;
	case eSAI:
		if (p_soAVPValue.os.len < sizeof(soSAI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SSAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soSAI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soSAI.m_soMCCMNC);
		break;
	case eRAI:
		if (p_soAVPValue.os.len < sizeof(soRAI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SRAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soRAI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soRAI.m_soMCCMNC);
		break;
	case eTAI:
		if (p_soAVPValue.os.len < sizeof(soTAI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of STAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soTAI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soTAI.m_soMCCMNC);
		break;
	case eECGI:
		if (p_soAVPValue.os.len < sizeof(soECGI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SECGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soECGI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soECGI.m_soMCCMNC);
		break;
	case eTAI_ECGI:
		if (p_soAVPValue.os.len < sizeof(soTAI_ECGI)) {
			UTL_LOG_E(*g_pcoLog, "value length less than size of soTAI_ECGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soTAI_ECGI, &(p_soAVPValue.os.data[1]), p_soAVPValue.os.len - 1);
		psoMCCMNC = &(soTAI_ECGI.m_soTAI.m_soMCCMNC);
		break;
	}

	if (iRetVal)
		return iRetVal;
	if (NULL == psoMCCMNC) {
		UTL_LOG_E(*g_pcoLog, "unexpected error: NULL pointer to MCCMNC");
		return -2;
	}

	/* формируем MCCMNC */
	iFnRes = snprintf(
		mcMCCMNC, sizeof(mcMCCMNC),
		"%u%u%u-%u%u",
		psoMCCMNC->m_uiMCC1, psoMCCMNC->m_uiMCC2, psoMCCMNC->m_uiMCC3,
		psoMCCMNC->m_uiMNC1, psoMCCMNC->m_uiMNC2);
	if (iFnRes < 0) {
		iRetVal = errno;
		UTL_LOG_E(*g_pcoLog, "snprintf error code: '%d'", iRetVal);
		mcMCCMNC[0] = '\0';
	}
	if (iFnRes > sizeof(mcMCCMNC))
		mcMCCMNC[sizeof(mcMCCMNC) - 1] = '\0';
	p_soUserLocationInfo.m_coSGSNMCCMNC = mcMCCMNC;

	/* что-то полезное уже имеем, ставим метку, что данные получены */
	p_soUserLocationInfo.m_bLoaded = true;

	/* формируем опциональные данные */
	switch (p_soAVPValue.os.data[0]) {
	case eCGI:
		format_CGI(soCGI, mcMCCMNC, p_soUserLocationInfo.m_coCGI);
		break;
	case eSAI:
		break;
	case eRAI:
		format_RAI(soRAI, mcMCCMNC, p_soUserLocationInfo.m_coRAI);
		break;
	case eTAI:
		format_TAI(soTAI, mcMCCMNC, p_soUserLocationInfo.m_coTAI);
		break;
	case eECGI:
		format_ECGI(soECGI, mcMCCMNC, p_soUserLocationInfo.m_coECGI);
		break;
	case eTAI_ECGI:
		format_TAI(soTAI_ECGI.m_soTAI, mcMCCMNC, p_soUserLocationInfo.m_coTAI);
		format_ECGI(soTAI_ECGI.m_soECGI, mcMCCMNC, p_soUserLocationInfo.m_coECGI);
		break;
	default:
		iFnRes = 0;
		break;
	}

	return iRetVal;
}

void format_CGI(SCGI &p_soCGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];
	std::string strSector;

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soCGI.m_soLAC.m_uiLAC1 << 8) + (p_soCGI.m_soLAC.m_uiLAC2),
		(p_soCGI.m_soCI.m_uiCI1 << 8 ) + (p_soCGI.m_soCI.m_uiCI2));
	if (iFnRes < 0)
		return;
	if (iFnRes > sizeof(mcValue))
		mcValue[sizeof(mcValue) - 1] = '\0';
	else
		/* выбираем сектор - последняя цифра CI */
		strSector = mcValue[iFnRes - 1];

	p_coValue.v += '-';
	p_coValue.v += mcValue;
	/* формируем сектор */
	if (strSector.length()) {
		p_coValue.v[p_coValue.v.length() - 1] = '-';
		p_coValue.v += strSector;
	}
}

void format_SAI(SSAI &p_soSAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soSAI.m_soLAC.m_uiLAC1 << 8) + (p_soSAI.m_soLAC.m_uiLAC2),
		(p_soSAI.m_soSAS.m_uiSAS1 << 8) + (p_soSAI.m_soSAS.m_uiSAS2));
	if (iFnRes < 0)
		return;
	if (iFnRes > sizeof(mcValue))
		mcValue[sizeof(mcValue) - 1] = '\0';
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_RAI(SRAI &p_soRAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soRAI.m_soLAC.m_uiLAC1 << 8) + (p_soRAI.m_soLAC.m_uiLAC2),
		p_soRAI.m_soRAC.m_uiRAC);
	if (iFnRes < 0)
		return;
	if (iFnRes > sizeof(mcValue))
		mcValue[sizeof(mcValue) - 1] = '\0';
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_TAI(STAI &p_soTAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u",
		(p_soTAI.m_soTAC.m_uiTAC1 << 8) + (p_soTAI.m_soTAC.m_uiTAC2));
	if (iFnRes < 0)
		return;
	if (iFnRes > sizeof(mcValue))
		mcValue[sizeof(mcValue) - 1] = '\0';
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_ECGI(SECGI &p_soECGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soECGI.m_soECI.m_uiECI1 << 16) + (p_soECGI.m_soECI.m_uiECI2 << 8) + (p_soECGI.m_soECI.m_uiECI3),
		p_soECGI.m_soECI.m_uiECI4);
	if (iFnRes < 0)
		return;
	if (iFnRes > sizeof(mcValue))
		mcValue[sizeof(mcValue) - 1] = '\0';
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

int pcrf_extract_RAI(avp_value &p_soAVPValue, otl_value<std::string> &p_coValue)
{
	int iRetVal = 0;
	int iFnRes;
	char mcMCCMNC[128];
	SRAI soRAI;

	/* проверяем размер данных */
	if (p_soAVPValue.os.len < 5 + sizeof(soRAI.m_soLAC) + sizeof(soRAI.m_soRAC))
		return EINVAL;

	iFnRes = snprintf(
		mcMCCMNC, sizeof(mcMCCMNC),
		"%c%c%c-%c%c",
		p_soAVPValue.os.data[0], p_soAVPValue.os.data[1], p_soAVPValue.os.data[2],
		p_soAVPValue.os.data[3], p_soAVPValue.os.data[4]);
	if (iFnRes < 0)
		return errno;
	if (iFnRes > sizeof(mcMCCMNC))
		mcMCCMNC[sizeof(mcMCCMNC) - 1] = '\0';

	memcpy(((char*)(&soRAI)) + sizeof(soRAI.m_soMCCMNC), &(p_soAVPValue.os.data[5]), sizeof(soRAI.m_soLAC) + sizeof(soRAI.m_soRAC));

	format_RAI(soRAI, mcMCCMNC, p_coValue);

	return iRetVal;
}

extern std::vector<SPeerInfo> g_vectPeerList;

int pcrf_peer_proto(SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin();

	while (iterPeerList != g_vectPeerList.end()) {
		if (iterPeerList->m_coHostName.v == p_soSessInfo.m_coOriginHost.v
				&& iterPeerList->m_coHostReal.v == iterPeerList->m_coHostReal.v) {
			p_soSessInfo.m_uiPeerProto = iterPeerList->m_uiPeerProto;
			break;
		}
		++iterPeerList;
	}

	return iRetVal;
}
