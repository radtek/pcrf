#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <vector>

/* handler for CCR req cb */
static struct disp_hdl * app_pcrf_hdl_ccr = NULL;

/* функция рекурсивного копирования содержимого avp */
struct avp * pcrf_copy_avp_recursive (struct avp *p_psoSource, int p_iSkeepBI);

/* функция формирования avp 'QoS-Information' */
struct avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Charging-Rule-Definition */
struct avp * pcrf_make_CRD (otl_connect &p_coDBConn, SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Supported-Features */
struct avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo);

/* функция заполнения avp X-HW-Usage-Report */
struct avp * pcrf_make_HWUR ();

static int app_pcrf_ccr_cb (struct msg ** msg, struct avp * avp, struct session * sess, void * opaque, enum disp_action * act)
{
	int iFnRes;
	struct msg *ans;
	struct avp
		*psoParentAVP = NULL,
		*psoChildAVP = NULL;
	union avp_value soAVPVal;
	SMsgDataForDB soMsgInfoCache;

	if (msg == NULL) {
		return EINVAL;
	}
	TRACE_ENTRY ("%p %p %p %p", msg, avp, sess, act);

	/* запрашиваем объект класса для работы с БД */
	otl_connect *pcoDBConn = NULL;
	CHECK_POSIX_DO (pcrf_db_pool_get ((void **) &pcoDBConn), );

	/* инициализация структуры хранения данных сообщения */
	CHECK_POSIX_DO (pcrf_server_DBstruct_init (&soMsgInfoCache), );

	/* выбираем данные из сообщения */
	msg_or_avp *pMsgOrAVP = *msg;
	pcrf_extract_req_data (pMsgOrAVP, &soMsgInfoCache);

	/* список правил профиля абонента */
	std::vector<SDBAbonRule> vectAbonRules;
	/* список активных правил абонента */
	std::vector<SDBAbonRule> vectActive;
	/* список активных неактуальных правил */
	std::vector<SDBAbonRule> vectNotrelevant;

	/* загружаем идентификатор подписчика */
	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
		/* загружаем идентификтор абонента из профиля абонента */
		CHECK_POSIX_DO (pcrf_server_db_load_abon_id ((*pcoDBConn), soMsgInfoCache), );
		/* загружаем из БД правила абонента */
		CHECK_POSIX_DO (pcrf_server_db_abon_rule (*(pcoDBConn), soMsgInfoCache, vectAbonRules), );
		break;/* INITIAL_REQUEST */
	case 3: /* TERMINATION_REQUEST */
		break;
	default: /* DEFAULT */
		/* загружаем идентификатор абонента из списка активных сессий абонента */
		CHECK_POSIX_DO (pcrf_server_db_load_session_info (*(pcoDBConn), soMsgInfoCache), );
		/* загружаем из БД правила абонента */
		CHECK_POSIX_DO (pcrf_server_db_abon_rule (*(pcoDBConn), soMsgInfoCache, vectAbonRules), );
		/* загружаем список активных правил */
		CHECK_POSIX_DO (pcrf_server_db_load_active_rules (*(pcoDBConn), soMsgInfoCache, vectActive), );
		/* формируем список неактуальных правил */
		CHECK_POSIX_DO (pcrf_server_select_notrelevant_active (*(pcoDBConn), soMsgInfoCache, vectActive, vectAbonRules, vectNotrelevant),);
		break; /* DEFAULT */
	}

	/* сохраняем в БД запрос */
	CHECK_POSIX_DO (pcrf_server_req_db_store (*(pcoDBConn), &soMsgInfoCache), );

	/* Create answer header */
	CHECK_FCT (fd_msg_new_answer_from_req (fd_g_config->cnf_dict, msg, 0));
	ans = *msg;

	/* Auth-Application-Id */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoChildAVP));
		soAVPVal.u32 = 16777238;
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}

	/* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
	CHECK_FCT (fd_msg_rescode_set (ans, (char *) "DIAMETER_SUCCESS", NULL, NULL, 1));

	/* Destination-Host */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictDestHost, 0, &psoChildAVP));
		soAVPVal.os.data = (uint8_t *) soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.c_str ();
		soAVPVal.os.len = soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}

	/* put 'CC-Request-Type' into answer */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictCCRequestType, 0, &psoChildAVP));
		soAVPVal.i32 = soMsgInfoCache.m_psoReqInfo->m_iCCRequestType;
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}
	/* put 'CC-Request-Number' into answer */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictCCRequestNumber, 0, &psoChildAVP));
		soAVPVal.u32 = soMsgInfoCache.m_psoReqInfo->m_coCCRequestNumber.v;
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}
	/* put 'Origin-State-Id' into answer */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictOriginStateId, 0, &psoChildAVP));
		soAVPVal.u32 = soMsgInfoCache.m_psoSessInfo->m_coOriginStateId.v;
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}
	switch (soMsgInfoCache.m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
		/* Supported-Features */
		psoChildAVP = pcrf_make_SF (&soMsgInfoCache);
		if (psoChildAVP) {
			/* put 'Supported-Features' into answer */
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		/* Event-Trigger */
		{
			CHECK_FCT (fd_msg_avp_new (g_psoDictEventTrigger, 0, &psoChildAVP));
			soAVPVal.i32 = 33; /* USAGE_REPORT */
			CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
			/* put 'Event-Trigger' into answer */
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		/* Usage-Monitoring-Information */
		for (std::vector<SDBAbonRule>::iterator iter = vectAbonRules.begin (); iter != vectAbonRules.end (); ++ iter) {
			psoChildAVP = NULL;
			psoChildAVP = pcrf_make_UMI (*iter);
			if (psoChildAVP) {
				/* put 'Usage-Monitoring-Information' into answer */
				CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
			}
		}
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI (*(pcoDBConn), &soMsgInfoCache, vectAbonRules);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		break; /* INITIAL_REQUEST */
	case 2: /* UPDATE_REQUEST */
		/* Usage-Monitoring-Information */
		for (std::vector<SDBAbonRule>::iterator iter = vectAbonRules.begin (); iter != vectAbonRules.end (); ++ iter) {
			psoChildAVP = NULL;
			psoChildAVP = pcrf_make_UMI (*iter, false);
			if (psoChildAVP) {
				/* put 'Usage-Monitoring-Information' into answer */
				CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
			}
		}
		/* Charging-Rule-Remove */
		psoChildAVP = pcrf_make_CRR (*(pcoDBConn), &soMsgInfoCache, vectNotrelevant);
		/* put 'Charging-Rule-Remove' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI ((*pcoDBConn), &soMsgInfoCache, vectAbonRules);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		break; /* UPDATE_REQUEST */
	}

	/* Send the answer */
	CHECK_FCT (fd_msg_send (msg, NULL, NULL));

	pcrf_server_DBStruct_cleanup (&soMsgInfoCache);

	/* освобождаем объект класса взаимодействия с БД */
	CHECK_POSIX_DO (pcrf_db_pool_rel ((void *) pcoDBConn), );

	return 0;
}

int app_pcrf_serv_init (void)
{
	struct disp_when data;

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
	
	return;
}

struct avp * pcrf_copy_avp_recursive (struct avp *p_psoSouce, int p_iSkeepBI)
{
	int iFnRes;
	struct avp *psoAVP = NULL;
	struct avp *psoAVPChild = NULL;
	struct avp *psoAVPFound = NULL;
	struct avp *psoAVPNext = NULL;
	struct avp_hdr *psoAVPHdr = NULL;
	struct dict_object *psoDictObj = NULL;
	struct dict_avp_request soCrit;
	struct dict_avp_data soDictData;
	int iDepth = 0;

	/* получаем заголовок avp-источника */
	CHECK_FCT_DO (fd_msg_avp_hdr (p_psoSouce, &psoAVPHdr), return NULL);
	/* запрашиваем в словаре сведения о avp */
	soCrit.avp_vendor = psoAVPHdr->avp_vendor;
	soCrit.avp_code = psoAVPHdr->avp_code;

	/* если надо пропустить Bearer-Identifier */
	if (p_iSkeepBI
			&& soCrit.avp_vendor == 10415
			&& soCrit.avp_code == 1020) {
		return NULL;
	}

	CHECK_FCT_DO (fd_dict_search (fd_g_config->cnf_dict, DICT_AVP, AVP_BY_CODE_AND_VENDOR, &soCrit, &psoDictObj, ENOENT), return NULL);
	/* создем новый объект avp */
	CHECK_FCT_DO (fd_msg_avp_new (psoDictObj, 0, &psoAVP), return NULL);
	/* запрашиваем данные о avp */
	CHECK_FCT_DO (fd_dict_getval (psoDictObj, &soDictData), return NULL);
	/* анализиуем базовый тип avp */
	switch (soDictData.avp_basetype) {
	case AVP_TYPE_GROUPED:
		/* находим первую дочернюю avp */
		CHECK_FCT_DO (fd_msg_browse_internal (p_psoSouce, MSG_BRW_FIRST_CHILD, (msg_or_avp **) &psoAVPFound, &iDepth), return NULL);
		/* обходим все остальные avp */
		while (psoAVPFound && iDepth >= 0) {
			/* выуживаем дочернюю avp */
			psoAVPChild = pcrf_copy_avp_recursive (psoAVPFound, p_iSkeepBI);
			if (psoAVPChild) {
				CHECK_FCT_DO (fd_msg_avp_add (psoAVP, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			CHECK_FCT_DO (fd_msg_browse_internal (psoAVPFound, MSG_BRW_NEXT, (msg_or_avp **) &psoAVPNext, &iDepth), return NULL);
			psoAVPFound = psoAVPNext;
		}
		break;
	default:
		/* копируем в него исходные данные */
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, psoAVPHdr->avp_value), return NULL);
		break;
	}

	return psoAVP;
}

int pcrf_server_select_notrelevant_active (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive,
	std::vector<SDBAbonRule> &p_vectAbonRules,
	std::vector<SDBAbonRule> &p_vectNotrelevant)
{
	int iRetVal = 0;
	bool bRuleIsRelevant;

	/* обходим все активные правила */
	std::vector<SDBAbonRule>::iterator iterActive = p_vectActive.begin ();
	std::vector<SDBAbonRule>::iterator iterRule = p_vectAbonRules.begin ();
	for (; iterActive != p_vectActive.end (); ++ iterActive) {
		bRuleIsRelevant = false;
		for (; iterRule != p_vectAbonRules.end (); ++ iterRule) {
			/* если имена правил совпадают, значит активное правило актуально */
			if (iterActive->m_coRuleName.v == iterRule->m_coRuleName.v) {
				/* фиксируем, что правило активировано */
				iterRule->m_bIsActivated = true;
				/* запоминаем, что правило актуально */
				bRuleIsRelevant = true;
				break;
			}
		}
		/* если правило неактуально помещаем его в список неактуальных правил */
		if (! bRuleIsRelevant) {
			CHECK_POSIX_DO (load_rule_info (p_coDBConn, p_soMsgInfoCache, iterActive->m_uiRuleId, p_vectNotrelevant), );
		}
	}

	return iRetVal;
}

struct avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule)
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
			soAVPVal.i32 = p_soAbonRule.m_coQoSClassIdentifier.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Max-Requested-Bandwidth-UL */
		if (! p_soAbonRule.m_coMaxRequestedBandwidthDl.is_null ()) {
			ui32Value = p_soAbonRule.m_coMaxRequestedBandwidthDl.v;
			if (! p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.is_null ()) {
				ui32Value = ui32Value > p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v ? p_psoReqInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl.v : ui32Value;
			}
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMaxRequestedBandwidthUL, 0, &psoAVPChild), return NULL);
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
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMaxRequestedBandwidthDL, 0, &psoAVPChild), return NULL);
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
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictGuaranteedBitrateUL, 0, &psoAVPChild), return NULL);
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
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictGuaranteedBitrateDL, 0, &psoAVPChild), return NULL);
			soAVPVal.u32 = ui32Value;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPQoSI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}

		/* Allocation-Retention-Priority */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAllocationRetentionPriority, 0, &psoAVPParent), return NULL);

		/* Priority-Level */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictPriorityLevel, 0, &psoAVPChild), return NULL);
		soAVPVal.u32 = 2;
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

struct avp * pcrf_make_CRR (otl_connect &p_coDBConn, SMsgDataForDB *p_psoReqInfo, std::vector<SDBAbonRule> &p_vectNotRelevantRules)
{
	/* если список пустой выходим ничего не делая */
	if (0 == p_vectNotRelevantRules.size ()) {
		return NULL;
	}

	avp *psoAVPCRR = NULL; /* Charging-Rule-Remove */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	std::vector<SDBAbonRule>::iterator iter = p_vectNotRelevantRules.begin ();

	/* Charging-Rule-Remove */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleRemove, 0, &psoAVPCRR), return NULL);
	/* обходим все элементы списка */
	for (; iter != p_vectNotRelevantRules.end (); ++ iter) {
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
		CHECK_FCT_DO (pcrf_db_close_session_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), iter->m_coRuleName.v), );
	}

	return psoAVPCRR;
}

struct avp * pcrf_make_CRI (otl_connect &p_coDBConn, SMsgDataForDB *p_psoReqInfo, std::vector<SDBAbonRule> &p_vectAbonRules)
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
		if (iter->m_bIsActivated) {
			continue;
		}
		/* Charging-Rule-Install */
		/* создаем avp 'Charging-Rule-Install' только по необходимости */
		if (NULL == psoAVPCRI) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleInstall, 0, &psoAVPCRI), return NULL);
			/* Bearer-Identifier */
			if (0 == p_psoReqInfo->m_psoSessInfo->m_iIPCANType && ! p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictBearerIdentifier, 0, &psoAVPChild), return NULL);
				soAVPVal.os.data = (uint8_t *) p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.c_str ();
				soAVPVal.os.len = p_psoReqInfo->m_psoReqInfo->m_coBearerIdentifier.v.length ();
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'Bearer-Identifier' into 'Charging-Rule-Install' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
		}
		/* Charging-Rule-Definition */
		psoAVPChild = pcrf_make_CRD (p_coDBConn, p_psoReqInfo, *iter);
		if (psoAVPChild) {
			/* put 'Charging-Rule-Definition' into 'Charging-Rule-Install' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
	}

	return psoAVPCRI;
}

struct avp * pcrf_make_CRD (otl_connect &p_coDBConn, SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule)
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
	iIpCanType = p_psoReqInfo->m_psoSessInfo->m_iIPCANType;

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
		if (! p_soAbonRule.m_coKeyName.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *) p_soAbonRule.m_coKeyName.v.c_str ();
			soAVPVal.os.len = (size_t) p_soAbonRule.m_coKeyName.v.length ();
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

	/* сохраняем выданную политику в БД */
	if (psoAVPCRD && pcszRuleName) {
		pcrf_db_insert_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), p_soAbonRule.m_uiRuleId, pcszRuleName);
	}

	return psoAVPCRD;
}

struct avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo)
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

struct avp * pcrf_make_UMI (SDBAbonRule &p_soAbonRule, bool p_bFull)
{
	avp
		*psoAVPUMI = NULL, /* Usage-Monitoring-Information */
		*psoAVPGSU = NULL, /* Granted-Service-Unit */
		*psoAVPChild = NULL;
	struct dict_avp_request soCrit;
	union avp_value soAVPVal;

	/* если для правила не задан ключ */
	if (p_soAbonRule.m_coKeyName.is_null ()) {
		return NULL;
	}

	/* Usage-Monitoring-Information */
	CHECK_FCT_DO (fd_msg_avp_new (g_psoDictUsageMonitoringInformation, 0, &psoAVPUMI), return NULL);
	/* Monitoring-Key */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
		soAVPVal.os.data = (uint8_t *) p_soAbonRule.m_coKeyName.v.c_str ();
		soAVPVal.os.len = (size_t) p_soAbonRule.m_coKeyName.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
	}
	/* если задана хотябы одна квота */
	if (! p_soAbonRule.m_coDosageTotalOctets.is_null ()
			|| ! p_soAbonRule.m_coDosageOutputOctets.is_null ()
			|| ! p_soAbonRule.m_coDosageInputOctets.is_null ()) {
		/* Granted-Service-Unit */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictGrantedServiceUnit, 0, &psoAVPGSU), return NULL);
		/* CC-Total-Octets */
		if (! p_soAbonRule.m_coDosageTotalOctets.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCTotalOctets, 0, &psoAVPChild), return NULL);
			soAVPVal.u64 = p_soAbonRule.m_coDosageTotalOctets.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'CC-Total-Octets' into 'Granted-Service-Unit' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* CC-Input-Octets */
		if (! p_soAbonRule.m_coDosageInputOctets.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
			soAVPVal.u64 = p_soAbonRule.m_coDosageInputOctets.v;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'CC-Input-Octets' into 'Granted-Service-Unit' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* CC-Output-Octets */
		if (! p_soAbonRule.m_coDosageOutputOctets.is_null ()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
			soAVPVal.u64 = 10000000;
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			/* put 'CC-Output-Octets' into 'Granted-Service-Unit' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		/* put 'Granted-Service-Unit' into 'Usage-Monitoring-Information' */
		CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPGSU), return NULL);
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

	return psoAVPUMI;
}

struct avp * pcrf_make_HWUR ()
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
