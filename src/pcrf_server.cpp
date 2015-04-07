#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <vector>
#include <stdio.h>

/* handler for CCR req cb */
static disp_hdl * app_pcrf_hdl_ccr = NULL;

/* функция формирования avp 'QoS-Information' */
avp * pcrf_make_QoSI (SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Charging-Rule-Definition */
avp * pcrf_make_CRD (otl_connect &p_coDBConn, SMsgDataForDB *p_psoReqInfo, SDBAbonRule &p_soAbonRule);

/* функция заполнения avp Supported-Features */
avp * pcrf_make_SF (SMsgDataForDB *p_psoReqInfo);

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
/* парсинг 3GPP-User-Location-Info */
int pcrf_extract_user_location(avp_value &p_soAVPValue, std::string &p_strString);

static int app_pcrf_ccr_cb (
	msg ** p_ppsoMsg,
	avp * p_psoAVP,
	session * p_psoSess,
	void * opaque,
	enum disp_action * p_pAct)
{
	int iFnRes;
	msg *ans;
	avp
		*psoParentAVP = NULL,
		*psoChildAVP = NULL;
	union avp_value soAVPVal;
	SMsgDataForDB soMsgInfoCache;

	if (p_ppsoMsg == NULL) {
		return EINVAL;
	}
	TRACE_ENTRY ("%p %p %p %p", p_ppsoMsg, p_psoAVP, p_psoSess, p_pAct);

	/* запрашиваем объект класса для работы с БД */
	otl_connect *pcoDBConn = NULL;
	CHECK_POSIX_DO (pcrf_db_pool_get ((void **) &pcoDBConn), );

	/* инициализация структуры хранения данных сообщения */
	CHECK_POSIX_DO (pcrf_server_DBstruct_init (&soMsgInfoCache), );

	/* выбираем данные из сообщения */
	msg_or_avp *pMsgOrAVP = *p_ppsoMsg;
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
	CHECK_FCT (fd_msg_new_answer_from_req (fd_g_config->cnf_dict, p_ppsoMsg, 0));
	ans = *p_ppsoMsg;

	/* Auth-Application-Id */
	{
		CHECK_FCT (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoChildAVP));
		soAVPVal.u32 = 16777238;
		CHECK_FCT (fd_msg_avp_setvalue (psoChildAVP, &soAVPVal));
		CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}

	/* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
	CHECK_FCT (fd_msg_rescode_set (ans, (char *) "DIAMETER_SUCCESS", NULL, NULL, 1));

	/* Destination-Realm */
	{
		CHECK_FCT(fd_msg_avp_new(g_psoDictDestRealm, 0, &psoChildAVP));
		soAVPVal.os.data = (uint8_t *)soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.c_str();
		soAVPVal.os.len = soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v.length();
		CHECK_FCT(fd_msg_avp_setvalue(psoChildAVP, &soAVPVal));
		CHECK_FCT(fd_msg_avp_add(ans, MSG_BRW_LAST_CHILD, psoChildAVP));
	}

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
			CHECK_FCT_DO (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP), /* continue */);
		}
		/* Event-Trigger */
		CHECK_FCT_DO(set_event_trigger(pcoDBConn, *(soMsgInfoCache.m_psoSessInfo), ans), /* continue */);
		/* Usage-Monitoring-Information */
		{
			bool bEvenTriggerInstalled = false;
			for (std::vector<SDBAbonRule>::iterator iter = vectAbonRules.begin (); iter != vectAbonRules.end (); ++ iter) {
				psoChildAVP = NULL;
				CHECK_FCT_DO (pcrf_make_UMI (ans, *iter, bEvenTriggerInstalled), /* continue */ );
			}
		}
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI (*(pcoDBConn), &soMsgInfoCache, vectAbonRules, ans);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		break; /* INITIAL_REQUEST */
	case 2: /* UPDATE_REQUEST */
		/* Event-Trigger */
		CHECK_FCT_DO(set_event_trigger(pcoDBConn, *(soMsgInfoCache.m_psoSessInfo), ans), /* continue */);
		/* Usage-Monitoring-Information */
		{
			bool bEvenTriggerInstalled = false;
			for (std::vector<SDBAbonRule>::iterator iter = vectAbonRules.begin(); iter != vectAbonRules.end(); ++iter) {
				psoChildAVP = NULL;
				CHECK_FCT_DO(pcrf_make_UMI(ans, *iter, bEvenTriggerInstalled, false, &(soMsgInfoCache.m_psoReqInfo->m_vectUsageInfo)), /* continue */);
			}
		}
		/* Charging-Rule-Remove */
		psoChildAVP = pcrf_make_CRR (*(pcoDBConn), &soMsgInfoCache, vectNotrelevant);
		/* put 'Charging-Rule-Remove' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		/* Charging-Rule-Install */
		psoChildAVP = pcrf_make_CRI ((*pcoDBConn), &soMsgInfoCache, vectAbonRules, ans);
		/* put 'Charging-Rule-Install' into answer */
		if (psoChildAVP) {
			CHECK_FCT (fd_msg_avp_add (ans, MSG_BRW_LAST_CHILD, psoChildAVP));
		}
		break; /* UPDATE_REQUEST */
	}

	/* Send the answer */
	CHECK_FCT (fd_msg_send (p_ppsoMsg, NULL, NULL));

	pcrf_server_DBStruct_cleanup (&soMsgInfoCache);

	/* освобождаем объект класса взаимодействия с БД */
	CHECK_POSIX_DO (pcrf_db_pool_rel ((void *) pcoDBConn), );

	return 0;
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
	
	return;
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
	/* цикл активных правил */
	for (; iterActive != p_vectActive.end (); ++ iterActive) {
		bRuleIsRelevant = false;
		/* цикл актуальных правил */
		for (iterRule = p_vectAbonRules.begin (); iterRule != p_vectAbonRules.end (); ++ iterRule) {
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
			CHECK_POSIX_DO (load_rule_info (p_coDBConn, p_soMsgInfoCache, iterActive->m_soRuleId, p_vectNotrelevant), );
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

avp * pcrf_make_CRR (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoReqInfo,
	std::vector<SDBAbonRule> &p_vectNotRelevantRules)
{
	/* если список пустой выходим ничего не делая */
	if (0 == p_vectNotRelevantRules.size ()) {
		return NULL;
	}

	avp *psoAVPCRR = NULL; /* Charging-Rule-Remove */
	avp *psoAVPChild = NULL;
	avp_value soAVPVal;
	std::vector<SDBAbonRule>::iterator iter = p_vectNotRelevantRules.begin ();

	/* обходим все элементы списка */
	for (; iter != p_vectNotRelevantRules.end (); ++ iter) {
		switch (iter->m_soRuleId.m_uiProtocol) {
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
			CHECK_FCT_DO (pcrf_db_close_session_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), iter->m_soRuleId), );
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			CHECK_FCT_DO (pcrf_db_close_session_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), iter->m_soRuleId), );
			break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRR;
}

avp * pcrf_make_CRI (
	otl_connect &p_coDBConn,
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
		switch (iter->m_soRuleId.m_uiProtocol) {
		case 1: /* Gx */
			/* если првило уже активировано переходим к следующей итерации */
			if (iter->m_bIsActivated) {
				continue;
			}
			/* Charging-Rule-Install */
			/* создаем avp 'Charging-Rule-Install' только по необходимости */
			if (NULL == psoAVPCRI) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictChargingRuleInstall, 0, &psoAVPCRI), return NULL);
				/* Bearer-Identifier */
				if (0 == p_psoReqInfo->m_psoSessInfo->m_iIPCANType
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
			psoAVPChild = pcrf_make_CRD (p_coDBConn, p_psoReqInfo, *iter);
			if (psoAVPChild) {
				/* put 'Charging-Rule-Definition' into 'Charging-Rule-Install' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPCRI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
				/* сохраняем выданную политику в БД */
				CHECK_FCT_DO (pcrf_db_insert_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), *iter), /* continue */);
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
			CHECK_FCT_DO (pcrf_db_insert_policy (p_coDBConn, *(p_psoReqInfo->m_psoSessInfo), *iter), /* continue */ );
			break; /* Gx Cisco SCE */
		}
	}

	return psoAVPCRI;
}

avp * pcrf_make_CRD (
	otl_connect &p_coDBConn,
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
		std::vector<SDBMonitoringInfo>::iterator iterMK = p_soAbonRule.m_vectMonitInfo.begin ();
		if (iterMK != p_soAbonRule.m_vectMonitInfo.end () && iterMK->m_coKeyName.v.length()) {
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *) iterMK->m_coKeyName.v.c_str ();
			soAVPVal.os.len = (size_t) iterMK->m_coKeyName.v.length ();
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

int pcrf_make_UMI (
	msg_or_avp *p_psoMsgOrAVP,
	SDBAbonRule &p_soAbonRule,
	bool &p_bEvenTriggerInstalled,
	bool p_bFull,
	std::vector<SSessionUsageInfo> *p_pvectUsageInfo)
{
	avp
		*psoAVPUMI = NULL, /* Usage-Monitoring-Information */
		*psoAVPGSU = NULL, /* Granted-Service-Unit */
		*psoAVPET = NULL,  /* Event-Trigger */
		*psoAVPChild = NULL;
	dict_avp_request soCrit;
	union avp_value soAVPVal;
	int iRetVal = 0;
	bool bQuotaIsExhausted;

	std::vector<SDBMonitoringInfo>::iterator iterMonitInfo = p_soAbonRule.m_vectMonitInfo.begin ();
	for (; iterMonitInfo != p_soAbonRule.m_vectMonitInfo.end (); ++iterMonitInfo) {
		/* если для правила не задан ключ */
		if (iterMonitInfo->m_coKeyName.is_null ()) {
			continue;
		}
		/* если не задана ни одна квота */
		if (! iterMonitInfo->m_coDosageTotalOctets.is_null ()
				&& ! iterMonitInfo->m_coDosageOutputOctets.is_null ()
				&& ! iterMonitInfo->m_coDosageInputOctets.is_null ()) {
			continue;
		}
		/* проверяем не исчерпана ли квота */
		bQuotaIsExhausted = false;
		if (p_pvectUsageInfo) {
			for (std::vector<SSessionUsageInfo>::iterator iterSU = p_pvectUsageInfo->begin(); iterSU != p_pvectUsageInfo->end(); ++iterSU) {
				if (iterSU->m_coMonitoringKey.v == iterMonitInfo->m_coKeyName.v) {
					if ((iterSU->m_coCCInputOctets.is_null () || iterSU->m_coCCInputOctets.v == 0)
							&& (iterSU->m_coCCOutputOctets.is_null() || iterSU->m_coCCOutputOctets.v == 0)
							&& (iterSU->m_coCCTotalOctets.is_null() || iterSU->m_coCCTotalOctets.v == 0)) {
						bQuotaIsExhausted = true;
					}
					break;
				}
			}
		}
		/* Usage-Monitoring-Information */
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictUsageMonitoringInformation, 0, &psoAVPUMI), return NULL);
		/* Monitoring-Key */
		{
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictMonitoringKey, 0, &psoAVPChild), return NULL);
			soAVPVal.os.data = (uint8_t *) iterMonitInfo->m_coKeyName.v.c_str ();
			soAVPVal.os.len = (size_t) iterMonitInfo->m_coKeyName.v.length ();
			CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		}
		if (bQuotaIsExhausted) { /* если квота исчерпана */
			/* Usage-Monitoring-Support */
			CHECK_FCT_DO(fd_msg_avp_new(g_psoDictUsageMonitoringSupport, 0, &psoAVPChild), return NULL);
			soAVPVal.i32 = 0;  /* USAGE_MONITORING_DISABLED */
			CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPVal), return NULL);
			/* put 'Usage-Monitoring-Support' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO(fd_msg_avp_add(psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
		} else {
			/* Granted-Service-Unit */
			CHECK_FCT_DO (fd_msg_avp_new (g_psoDictGrantedServiceUnit, 0, &psoAVPGSU), return NULL);
			/* CC-Total-Octets */
			if (! iterMonitInfo->m_coDosageTotalOctets.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCTotalOctets, 0, &psoAVPChild), return NULL);
				soAVPVal.u64 = iterMonitInfo->m_coDosageTotalOctets.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'CC-Total-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* CC-Input-Octets */
			if (! iterMonitInfo->m_coDosageInputOctets.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCInputOctets, 0, &psoAVPChild), return NULL);
				soAVPVal.u64 = iterMonitInfo->m_coDosageInputOctets.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'CC-Input-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* CC-Output-Octets */
			if (! iterMonitInfo->m_coDosageOutputOctets.is_null ()) {
				CHECK_FCT_DO (fd_msg_avp_new (g_psoDictCCOutputOctets, 0, &psoAVPChild), return NULL);
				soAVPVal.u64 = iterMonitInfo->m_coDosageOutputOctets.v;
				CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVPChild, &soAVPVal), return NULL);
				/* put 'CC-Output-Octets' into 'Granted-Service-Unit' */
				CHECK_FCT_DO (fd_msg_avp_add (psoAVPGSU, MSG_BRW_LAST_CHILD, psoAVPChild), return NULL);
			}
			/* put 'Granted-Service-Unit' into 'Usage-Monitoring-Information' */
			CHECK_FCT_DO (fd_msg_avp_add (psoAVPUMI, MSG_BRW_LAST_CHILD, psoAVPGSU), return NULL);
			if (p_bFull || 2 == p_soAbonRule.m_soRuleId.m_uiProtocol) {
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
		}
		if (psoAVPUMI) {
			/* Event-Trigger */
			if (!p_bEvenTriggerInstalled && !bQuotaIsExhausted) {
				CHECK_FCT_DO(fd_msg_avp_new(g_psoDictEventTrigger, 0, &psoAVPET), return NULL);
				switch (p_soAbonRule.m_soRuleId.m_uiProtocol) {
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
				p_bEvenTriggerInstalled = true;
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

int set_event_trigger (
	otl_connect *p_pcoDBConn,
	SSessionInfo &p_soSessInfo,
	msg_or_avp *p_psoMsgOrAVP)
{
	int iRetVal = 0;
	avp *psoAVP;
	avp_value soAVPValue;
	unsigned int uiProtocolId;

	otl_stream coStream;
	try {
		/* определяем по какому протоколу работает пир */
		coStream.open (
			1,
			"select "
				"protocol_id "
			"from "
				"ps.peer "
			"where "
				"host_name = :host_name /* char[100] */ "
				"and realm = :realm /* char[100] */",
			*p_pcoDBConn);
		coStream
			<< p_soSessInfo.m_coOriginHost
			<< p_soSessInfo.m_coOriginRealm;
		coStream
			>> uiProtocolId;
		coStream.close ();
		/* USER_LOCATION_CHANGE */
		switch (uiProtocolId) {
		case 1: /* Gx */
			CHECK_FCT(fd_msg_avp_new(g_psoDictEventTrigger, 0, &psoAVP));
			soAVPValue.i32 = 13;
			CHECK_FCT(fd_msg_avp_setvalue(psoAVP, &soAVPValue));
			CHECK_FCT(fd_msg_avp_add(p_psoMsgOrAVP, MSG_BRW_LAST_CHILD, psoAVP));
			break; /* Gx */
		}
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
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

	/* определяем время запроса */
	time_t tSecsSince1970;
	tm soTime;
	if ((time_t)-1 != time(&tSecsSince1970)) {
		if (localtime_r(&tSecsSince1970, &soTime)) {
			fill_otl_datetime(p_psoMsgInfo->m_psoSessInfo->m_coTimeLast.v, soTime);
			p_psoMsgInfo->m_psoSessInfo->m_coTimeLast.set_non_null();
		}
	}

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
		default: /* vendor undefined */
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
				break;
			case 278: /* Origin-State-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginStateId = psoAVPHdr->avp_value->u32;
				break;
			case 296: /* Origin-Realm */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.set_non_null();
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
				p_psoMsgInfo->m_psoSessInfo->m_coSGSNAddress = mcIPAddr;
			}
			break;
			case 18: /* 3GPP-SGSN-MCC-MNC */
				p_psoMsgInfo->m_psoReqInfo->m_coSGSNMCCMNC.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coSGSNMCCMNC.set_non_null();
				break;
			case 21: /* 3GPP-RAT-Type */
				p_psoMsgInfo->m_psoReqInfo->m_coRATType.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coRATType.set_non_null();
				break;
			case 22: /* 3GPP-User-Location-Info */
				if (0 == pcrf_extract_user_location(*psoAVPHdr->avp_value, p_psoMsgInfo->m_psoReqInfo->m_coUserLocationInfo.v)) {
					p_psoMsgInfo->m_psoReqInfo->m_coUserLocationInfo.set_non_null();
				}
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
				p_psoMsgInfo->m_psoReqInfo->m_coRAI.v.insert(0, (const char *)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coRAI.set_non_null();
				break;
			case 1000: /* Bearer-Usage */
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerUsage = mcValue;
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
				p_psoMsgInfo->m_psoSessInfo->m_iIPCANType = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoSessInfo->m_coIPCANType = mcValue;
				}
				break;
			case 1028: /* QoS-Class-Identifier */
				p_psoMsgInfo->m_psoReqInfo->m_iQoSClassIdentifier = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val(psoAVPHdr, mcValue, sizeof(mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSClassIdentifier = mcValue;
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
					p_psoMsgInfo->m_psoReqInfo->m_coRATType = mcValue;
				}
				break;
			case 1049: /* Default-EPS-Bearer-QoS */
				pcrf_extract_req_data((void *)psoAVP, p_psoMsgInfo);
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

int pcrf_extract_user_location(avp_value &p_soAVPValue, std::string &p_strString)
{
	int iRetVal = 0;
	char mcDigit[32];
	int iFnRes;

	for (int i = 0; i < p_soAVPValue.os.len; ++i) {
		iFnRes = snprintf(mcDigit, sizeof(mcDigit), "%02X", p_soAVPValue.os.data[i]);
		if (iFnRes > 0 && iFnRes < sizeof(mcDigit)) {
			p_strString += mcDigit;
		} else {
			iRetVal = errno;
			break;
		}
	}

	return iRetVal;
}
