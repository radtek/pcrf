#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <string.h>
#include <time.h>
#include <vector>

/* копирование даты/времени */
void fill_otl_datetime (otl_datetime &p_coOtlDateTime, tm &p_soTime);
/* выборка идентификатора абонента */
int pcrf_extract_SubscriptionId (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка данных об устройстве абонента */
int pcrf_extract_UEI (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка рапорта о назначении политик */
int pcrf_extract_CRR (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Supported-Features */
int pcrf_extract_SF (avp *p_psoAVP, SSessionInfo &p_soSessInfo);
/* выборка значений Usage-Monitoring-Information */
int pcrf_extract_UMI (avp *p_psoAVP, SRequestInfo &p_soReqInfo);
/* выборка значений Used-Service-Unit */
int pcrf_extract_USU (avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo);
/* добавление записи в список сессий */
int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* обновление записи в таблице сессий */
int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo);
/* добавление записи в таблицу запросов */
int pcrf_db_insert_request (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo);

/* обновление записи в таблице выданых политик */
int pcrf_db_update_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SSessionPolicyInfo &p_soPoliciInfo);
/* закрываем записи в таблице выданных политик */
int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo);

int pcrf_server_DBstruct_init (struct SMsgDataForDB *p_psoMsgToDB)
{
	int iRetVal = 0;

	/* инициализируем структуру */
	memset (p_psoMsgToDB, 0, sizeof (*p_psoMsgToDB));

	/* виделяем память для хранения данных запроса */
	p_psoMsgToDB->m_psoSessInfo = new SSessionInfo;
	p_psoMsgToDB->m_psoReqInfo = new SRequestInfo;

	return iRetVal;
}

int pcrf_extract_req_data (msg_or_avp *p_psoMsgOrAVP, struct SMsgDataForDB *p_psoMsgInfo)
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

	/* определяем время запроса */
	time_t tSecsSince1970;
	tm soTime;
	if ((time_t) -1 != time (&tSecsSince1970)) {
		if (localtime_r (&tSecsSince1970, &soTime)) {
			fill_otl_datetime (p_psoMsgInfo->m_psoSessInfo->m_coTimeLast.v, soTime);
			p_psoMsgInfo->m_psoSessInfo->m_coTimeLast.set_non_null ();
		}
	}

	/* ищем первую AVP */
	iRetVal = fd_msg_browse_internal (p_psoMsgOrAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 0: /* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 8: /* Framed-IP-Address */
				if (4 == psoAVPHdr->avp_value->os.len) {
					int iStrLen;
					iStrLen = snprintf (mcValue, sizeof (mcValue) - 1,"%u.%u.%u.%u", psoAVPHdr->avp_value->os.data[0], psoAVPHdr->avp_value->os.data[1], psoAVPHdr->avp_value->os.data[2], psoAVPHdr->avp_value->os.data[3]);
					if (0 < iStrLen) {
						mcValue[sizeof (mcValue) - 1] = '\0';
						p_psoMsgInfo->m_psoSessInfo->m_coFramedIPAddress = mcValue;
					}
				}
				break;
			case 30: /* Called-Station-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coCalledStationId.set_non_null ();
				break;
			case 263: /* Session-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coSessionId.set_non_null ();
				break;
			case 264: /* Origin-Host */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginHost.set_non_null ();
				break;
			case 278: /* Origin-State-Id */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginStateId = psoAVPHdr->avp_value->u32;
				break;
			case 296: /* Origin-Realm */
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoSessInfo->m_coOriginRealm.set_non_null ();
				break;
			case 295: /* Termination-Cause */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoSessInfo->m_coTermCause = mcValue;
				}
				break;
			case 416: /* CC-Request-Type */
				p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coCCRequestType = mcValue;
				}
				break;
			case 443: /* Subscription-Id */
				pcrf_extract_SubscriptionId (psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 415: /* CC-Request-Number */
				p_psoMsgInfo->m_psoReqInfo->m_coCCRequestNumber = psoAVPHdr->avp_value->u32;
				break;
			case 458: /* User-Equipment-Info */
				pcrf_extract_UEI (psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			} /* Diameter */
			break;
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 6: /* 3GPP-SGSN-Address */
				{
					char mcIPAddr[16];
					sprintf (mcIPAddr, "%u.%u.%u.%u", psoAVPHdr->avp_value->os.data[0],psoAVPHdr->avp_value->os.data[1],psoAVPHdr->avp_value->os.data[2],psoAVPHdr->avp_value->os.data[3]);
					p_psoMsgInfo->m_psoSessInfo->m_coSGSNAddress = mcIPAddr;
				}
				break;
			case 18: /* 3GPP-SGSN-MCC-MNC */
				p_psoMsgInfo->m_psoReqInfo->m_coSGSNMCCMNC.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coSGSNMCCMNC.set_non_null ();
				break;
			case 21: /* 3GPP-RAT-Type */
				p_psoMsgInfo->m_psoReqInfo->m_coRATType.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coRATType.set_non_null ();
				break;
			case 22: /* 3GPP-User-Location-Info */
				p_psoMsgInfo->m_psoReqInfo->m_coUserLocationInfo.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coUserLocationInfo.set_non_null ();
				break;
			case 515: /* Max-Requested-Bandwidth-DL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthDl = psoAVPHdr->avp_value->u32;
				break;
			case 516: /* Max-Requested-Bandwidth-UL */
				p_psoMsgInfo->m_psoReqInfo->m_coMaxRequestedBandwidthUl = psoAVPHdr->avp_value->u32;
				break;
			case 628: /* Supported-Features */
				pcrf_extract_SF (psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 909: /* RAI */
				p_psoMsgInfo->m_psoReqInfo->m_coRAI.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				p_psoMsgInfo->m_psoReqInfo->m_coRAI.set_non_null ();
				break;
			case 1000: /* Bearer-Usage */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerUsage = mcValue;
				}
				break;
			case 1009: /* Online */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOnlineCharging = mcValue;
				}
				break;
			case 1008: /* Offline */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coOfflineCharging = mcValue;
				}
				break;
			case 1016: /* QoS-Information */
				pcrf_extract_req_data ((void *) psoAVP, p_psoMsgInfo);
				break;
			case 1018: /* Charging-Rule-Report */
				pcrf_extract_CRR (psoAVP, *(p_psoMsgInfo->m_psoSessInfo));
				break;
			case 1020: /* Bearer-Identifier */
				if (p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.is_null ()) {
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.v.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
					p_psoMsgInfo->m_psoReqInfo->m_coBearerIdentifier.set_non_null ();
				}
				break;
			case 1021: /* Bearer-Operation */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
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
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoSessInfo->m_coIPCANType = mcValue;
				}
				break;
			case 1028: /* QoS-Class-Identifier */
				p_psoMsgInfo->m_psoReqInfo->m_iQoSClassIdentifier = psoAVPHdr->avp_value->i32;
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSClassIdentifier = mcValue;
				}
				break;
			case 1029: /* QoS-Negotiation */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSNegotiation = mcValue;
				}
				break;
			case 1030: /* QoS-Upgrade */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coQoSUpgrade = mcValue;
				}
				break;
			case 1032: /* RAT-Type */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					p_psoMsgInfo->m_psoReqInfo->m_coRATType = mcValue;
				}
				break;
			case 1049: /* Default-EPS-Bearer-QoS */
				pcrf_extract_req_data ((void *) psoAVP, p_psoMsgInfo);
				break;
			case 1067: /* Usage-Monitoring-Information */
				pcrf_extract_UMI (psoAVP, *(p_psoMsgInfo->m_psoReqInfo));
				break;
			}
			break; /* 3GPP */
		}
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_server_req_db_store (otl_connect &p_coDBConn, struct SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;

	/* проверка параметров */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
			|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	int iFnRes;

	dict_avp_request_ex soAVPParam;
	char mcValue[0x10000];

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case 1: /* INITIAL_REQUEST */
		/* фиксируем дату начала сессии */
		p_psoMsgInfo->m_psoSessInfo->m_coTimeStart = p_psoMsgInfo->m_psoSessInfo->m_coTimeLast;
		iFnRes = pcrf_db_insert_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
		if (iFnRes) {
			printf ("pcrf_server_req_db_store: pcrf_db_insert_session: result code: '%d'\n", iFnRes);
		}
		break;
	case 3: /* TERMINATION_REQUEST */
		/* фиксируем дату завершения сессии */
		p_psoMsgInfo->m_psoSessInfo->m_coTimeEnd = p_psoMsgInfo->m_psoSessInfo->m_coTimeLast;
	case 2: /* UPDATE_REQUEST */
	case 4: /* EVENT_REQUEST */
		/* выполянем запрос на обновление записи */
		iFnRes = pcrf_db_update_session (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
		if (iFnRes) {
			printf ("pcrf_server_req_db_store: pcrf_db_update_session: result code: '%d'\n", iFnRes);
		}
		/* обрабатываем информацию о выданных политиках */
		iFnRes = pcrf_server_policy_db_store (p_coDBConn, p_psoMsgInfo);
		if (iFnRes) {
			printf ("pcrf_server_req_db_store: pcrf_server_policy_db_store: result code: '%d'\n", iFnRes);
		}
		break;
	default:
		break;
	}

	iFnRes = pcrf_db_insert_request (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *(p_psoMsgInfo->m_psoReqInfo));
	if (iFnRes) {
		printf ("pcrf_server_req_db_store: pcrf_db_insert_request: result code: '%d'\n", iFnRes);
	}

	return iRetVal;
}

int pcrf_server_policy_db_store (
	otl_connect &p_coDBConn,
	SMsgDataForDB *p_psoMsgInfo)
{
	int iRetVal = 0;
	int iFnRes;

	/* проверка параметров */
	if (NULL == p_psoMsgInfo->m_psoSessInfo
			|| NULL == p_psoMsgInfo->m_psoReqInfo) {
		return EINVAL;
	}

	switch (p_psoMsgInfo->m_psoReqInfo->m_iCCRequestType) {
	case 3: /* TERMINATION_REQUEST */
		/* сначала фиксируем информацию, полученную в запросе */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			iFnRes = pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
			if (iFnRes) {
				printf ("pcrf_server_policy_db_store: pcrf_db_update_policy: error code: '%d'\n", iFnRes);
			}
		}
		/* потом закрываем оставшиеся сессии */
		iFnRes = pcrf_db_close_session_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo));
		if (iFnRes) {
			printf ("pcrf_server_policy_db_store : pcrf_db_close_session_policy: result code: '%d'\n", iFnRes);
		}
		break;
	case 2: /* UPDATE_REQUEST */
		for (std::vector<SSessionPolicyInfo>::iterator iter = p_psoMsgInfo->m_psoSessInfo->m_vectCRR.begin (); iter != p_psoMsgInfo->m_psoSessInfo->m_vectCRR.end (); ++ iter) {
			iFnRes = pcrf_db_update_policy (p_coDBConn, *(p_psoMsgInfo->m_psoSessInfo), *iter);
			if (iFnRes) {
				printf ("pcrf_server_policy_db_store: pcrf_db_update_policy: error code: '%d'\n", iFnRes);
			}
		}
		break;
	case 4: /* EVENT_REQUEST */
		break;
	default:
		break;
	}

	return iRetVal;
}

void pcrf_server_DBStruct_cleanup (struct SMsgDataForDB *p_psoMsgInfo)
{
	/* освобождаем занятую память */
	if (p_psoMsgInfo->m_psoSessInfo) {
		p_psoMsgInfo->m_psoSessInfo->m_vectCRR.clear ();
		delete p_psoMsgInfo->m_psoSessInfo;
		p_psoMsgInfo->m_psoSessInfo = NULL;
	}
	if (p_psoMsgInfo->m_psoReqInfo) {
		delete p_psoMsgInfo->m_psoReqInfo;
		p_psoMsgInfo->m_psoReqInfo = NULL;
	}
}

void fill_otl_datetime (otl_datetime &p_coOtlDateTime, tm &p_soTime)
{
	p_coOtlDateTime.year = p_soTime.tm_year + 1900;
	p_coOtlDateTime.month = p_soTime.tm_mon + 1;
	p_coOtlDateTime.day = p_soTime.tm_mday;
	p_coOtlDateTime.hour = p_soTime.tm_hour;
	p_coOtlDateTime.minute = p_soTime.tm_min;
	p_coOtlDateTime.second = p_soTime.tm_sec;
}

int pcrf_extract_SubscriptionId (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	int iSubscriptionIdType = -1;
	std::string strSubscriptionIdData;

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
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
				strSubscriptionIdData.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

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

	return iRetVal;
}

int pcrf_extract_UEI (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	int iType = -1;
	std::string strInfo;

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
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
				strInfo.insert (0, (const char *) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				break;
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	switch (iType) {
	case 0: /* IMEISV */
		p_soSessInfo.m_coIMEI = strInfo;
		break;
	}

	return iRetVal;
}

int pcrf_extract_CRR (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	SSessionPolicyInfo soPolicy;
	char mcValue[0x10000];

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1005: /* Charging-Rule-Name */
				soPolicy.m_coChargingRuleName.v.insert (0, (const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soPolicy.m_coChargingRuleName.set_non_null ();
				break;
			case 1019: /* PCC-Rule-Status */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
					soPolicy.m_coPCCRuleStatus = mcValue;
				}
				break;
			case 1031: /* Rule-Failure-Code */
				if (0 == pcrf_extract_avp_enum_val (psoAVPHdr, mcValue, sizeof (mcValue))) {
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
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	p_soSessInfo.m_vectCRR.push_back (soPolicy);

	return iRetVal;
}

int pcrf_extract_SF (avp *p_psoAVP, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	char mcValue[0x10000];

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
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
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_extract_UMI (avp *p_psoAVP, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;
	SSessionUsageInfo soUsageInfo;
	bool bDone = false;

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
		if (iRetVal) {
			break;
		}
		switch (psoAVPHdr->avp_vendor) {
		case 10415: /* 3GPP */
			switch (psoAVPHdr->avp_code) {
			case 1066: /* Monitoring-Key */
				bDone = true;
				soUsageInfo.m_coMonitoringKey.v.assign ((const char*) psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
				soUsageInfo.m_coMonitoringKey.set_non_null ();
				break; /* Monitoring-Key */
			}
			break; /* 3GPP */
		case 0:	/* Diameter */
			switch (psoAVPHdr->avp_code) {
			case 446: /* Used-Service-Unit */
				pcrf_extract_USU (psoAVP, soUsageInfo);
				break;
			}
			break;	/* Diameter */
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	if (bDone) {
		p_soReqInfo.m_vectUsageInfo.push_back (soUsageInfo);
	}

	return iRetVal;
}

int pcrf_extract_USU (avp *p_psoAVP, SSessionUsageInfo &p_soUsageInfo)
{
	int iRetVal = 0;

	avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	int iDepth;

	iRetVal = fd_msg_browse_internal ((void *) p_psoAVP, MSG_BRW_FIRST_CHILD, (void **) &psoAVP, &iDepth);
	if (iRetVal) {
		return iRetVal;
	}

	do {
		/* получаем заголовок AVP */
		iRetVal = fd_msg_avp_hdr (psoAVP, &psoAVPHdr);
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
			}
			break;
		default:
			break;
		}
	} while (0 == fd_msg_browse_internal ((void *) psoAVP, MSG_BRW_NEXT, (void **) &psoAVP, &iDepth));

	return iRetVal;
}

int pcrf_db_insert_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		/* выполняем запрос на добавление записи о сессии */
		coStream.open (
			1,
			"insert into ps.SessionList (session_id, subscriber_id, Origin_Host, Origin_Realm, END_USER_IMSI, END_USER_E164, IMEISV, time_start, time_last_req, framed_ip_address, called_station_id,sgsn_address,ip_can_type)"
			" values (:1<char[64]>, :2<char[64]>, :3<char[255]>, :4<char[255]>, :5<char[16]>, :6<char[16]>, :7<char[20]>, :8<timestamp>, :9<timestamp>, :10<char[16]>, :11<char[255]>,:14<char[16]>,:15<char[64]>)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_strSubscriberId
			<< p_soSessInfo.m_coOriginHost
			<< p_soSessInfo.m_coOriginRealm
			<< p_soSessInfo.m_coEndUserIMSI
			<< p_soSessInfo.m_coEndUserE164
			<< p_soSessInfo.m_coIMEI
			<< p_soSessInfo.m_coTimeStart
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coFramedIPAddress
			<< p_soSessInfo.m_coCalledStationId
			<< p_soSessInfo.m_coSGSNAddress
			<< p_soSessInfo.m_coIPCANType;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("pcrf_db_insert_session: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_db_update_session (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		/* запрос на обновление записи о сессии */
		coStream.open (
			1,
			"update ps.SessionList"
			" set time_end = :1<timestamp>, time_last_req = :2<timestamp>, termination_cause = :3<char[64]>"
			" where session_id = :4<char[255]>",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeEnd
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coTermCause
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("pcrf_db_update_session: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}
}

int pcrf_db_insert_request (otl_connect &p_coDBConn, SSessionInfo &p_soSessInfo, SRequestInfo &p_soReqInfo)
{
	int iRetVal = 0;

	try {
		std::vector<SSessionUsageInfo>::iterator iter;
		otl_stream coStream;
		coStream.set_commit (0);
		/* запрос на добавление записи о запросе */
		coStream.open (
			1,
			"insert into ps.RequestList"
			" (session_id,time_event,cc_request_type,cc_request_number,bearer_identifier,online_charging,offline_charging,qos_upgrade,qos_class_identifier,max_requested_bandwidth_ul,max_requested_bandwidth_dl,guaranteed_bitrate_ul,guaranteed_bitrate_dl,rat_type,qos_negotiation,sgsn_mcc_mnc,user_location_info,rai,bearer_usage,bearer_operation)"
			" values (:1<char[255]>,:2<timestamp>,:3<char[64]>,:4<unsigned>,:5<char[255]>,:6<char[64]>,:7<char[64]>,:8<char[64]>,:9<char[64]>,:10<unsigned>,:11<unsigned>,:12<unsigned>,:13<unsigned>,:14<char[64]>,:15<char[64]>,:16<char[16]>,:17<char[255]>,:18<char[255]>,:19<char[64]>,:20<char[64]>)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_coTimeLast
			<< p_soReqInfo.m_coCCRequestType
			<< p_soReqInfo.m_coCCRequestNumber
			<< p_soReqInfo.m_coBearerIdentifier
			<< p_soReqInfo.m_coOnlineCharging
			<< p_soReqInfo.m_coOfflineCharging
			<< p_soReqInfo.m_coQoSUpgrade
			<< p_soReqInfo.m_coQoSClassIdentifier
			<< p_soReqInfo.m_coMaxRequestedBandwidthUl
			<< p_soReqInfo.m_coMaxRequestedBandwidthDl
			<< p_soReqInfo.m_coGuaranteedBitrateUl
			<< p_soReqInfo.m_coGuaranteedBitrateDl
			<< p_soReqInfo.m_coRATType
			<< p_soReqInfo.m_coQoSNegotiation
			<< p_soReqInfo.m_coSGSNMCCMNC
			<< p_soReqInfo.m_coUserLocationInfo
			<< p_soReqInfo.m_coRAI
			<< p_soReqInfo.m_coBearerUsage
			<< p_soReqInfo.m_coBearerOperation;
		p_coDBConn.commit ();
		coStream.close ();
		coStream.open (
			10,
			"insert into ps.SessionUsage"
			"(Session_Id,CC_Request_Type,CC_Request_Number,Monitoring_Key,CC_Input_Octets,CC_Output_Octets)"
			"values(:1<char[255]>,:2<char[64]>,:3<unsigned>,:4<char[32]>,:5<ubigint>,:6<ubigint>)",
			p_coDBConn);
		iter = p_soReqInfo.m_vectUsageInfo.begin ();
		while (iter != p_soReqInfo.m_vectUsageInfo.end ()) {
			coStream
				<< p_soSessInfo.m_coSessionId
				<< p_soReqInfo.m_coCCRequestType
				<< p_soReqInfo.m_coCCRequestNumber
				<< iter->m_coMonitoringKey
				<< iter->m_coCCInputOctets
				<< iter->m_coCCOutputOctets;
			++ iter;
		}
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("pcrf_db_insert_request: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_db_insert_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SDBAbonRule &p_soRule)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		char mcRuleName[255];
		size_t stStrLen;
		switch (p_soRule.m_soRuleId.m_uiProtocol) {
		case 1: /* Gx */
			stStrLen = p_soRule.m_coRuleName.v.length () > sizeof (mcRuleName) - 1 ? sizeof (mcRuleName) - 1 : p_soRule.m_coRuleName.v.length ();
			p_soRule.m_coRuleName.v.copy (
				mcRuleName,
				stStrLen);
			mcRuleName[stStrLen] = '\0';
			break; /* Gx */
		case 2: /* Cisco SCE Gx */
			sprintf (mcRuleName, "%u", p_soRule.m_coSCE_PackageId.v);
			break; /* Cisco SCE Gx */
		}
		coStream.open (
			1,
			"insert into ps.SessionPolicy "
			"(session_id,time_start,rule_id,rule_name,protocol_id) "
			"values (:session_id /* char[255] */, :time_start /* timestamp */, :rule_id /* unsigned */, :rule_name /* char[255] */, :protocol_id /* unsigned */)",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coSessionId
			<< p_soSessInfo.m_coTimeLast
			<< p_soRule.m_soRuleId.m_uiRuleId
			<< mcRuleName
			<< p_soRule.m_soRuleId.m_uiProtocol;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("pcrf_db_insert_policy: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_db_update_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SSessionPolicyInfo &p_soPoliciInfo)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		coStream.open (
			1,
			"update ps.SessionPolicy "
				"set time_end = :time_end /* timestamp */, "
				"charging_rule_name = :charging_rule_name /* char[255] */, "
				"pcc_rule_status = :pcc_rule_status /* char[64] */, "
				"rule_failure_code = :rule_failure_code /* char[64]*/ "
			"where "
				"session_id = :session_id /* char[255]*/ "
				"and rule_name = :rule_name /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soPoliciInfo.m_coChargingRuleName
			<< p_soPoliciInfo.m_coPCCRuleStatus
			<< p_soPoliciInfo.m_coRuleFailureCode
			<< p_soSessInfo.m_coSessionId
			<< p_soPoliciInfo.m_coChargingRuleName;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("pcrf_db_update_policy: error: code: '%d'; description: '%s';\n", coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		coStream.open (
			1,
			"update "
				"ps.SessionPolicy "
			"set "
				"time_end = :time_end /* timestamp */ "
			"where "
				"session_id = :session_id /* char[255] */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coSessionId;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("%s: error: code: '%d'; description: '%s';\n", __FUNCTION__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_db_close_session_policy (
	otl_connect &p_coDBConn,
	SSessionInfo &p_soSessInfo,
	SRuleId &p_soRuleId)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		coStream.open (
			1,
			"update "
				"ps.SessionPolicy "
			"set "
				"time_end = :time_end /* timestamp */ "
			"where "
				"session_id = :session_id /* char[255] */ "
				"and rule_id = :rule_id /* unsigned */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soSessInfo.m_coTimeLast
			<< p_soSessInfo.m_coSessionId
			<< p_soRuleId.m_uiRuleId;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		p_coDBConn.rollback ();
		printf ("%s: error: code: '%d'; description: '%s';\n", __FUNCTION__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

/* загружает идентификатор абонента (subscriber_id) из БД */
int pcrf_server_db_load_abon_id (otl_connect &p_coDBConn, SMsgDataForDB &p_soMsgInfo)
{
	int iRetVal = 0;

	try {
		/* выполняем запрос к БД */
		otl_stream coStream;
		coStream.open (
			1,
			"select "
			  "Subscriber_id "
			"from "
			  "ps.Subscription_Data "
			"where "
			  "(end_user_e164 is null or end_user_e164 = :end_user_e164 /* char[64] */) "
			  "and (end_user_imsi is null or end_user_imsi = :end_user_imsi /* char[100] */) "
			  "and (end_user_sip_uri is null or end_user_sip_uri = :end_user_sip_uri /* char[100] */) "
			  "and (end_user_nai is null or end_user_nai = :end_user_nai /* char[100] */) "
			  "and (end_user_private is null or end_user_private = :end_user_private /* char[100] */)",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserE164
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserIMSI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserSIPURI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserNAI
			<< p_soMsgInfo.m_psoSessInfo->m_coEndUserPrivate;
		coStream
			>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId;
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_server_db_load_active_rules (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfoCache,
	std::vector<SDBAbonRule> &p_vectActive)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		/* определяем по какому протоколу работает пир */
		unsigned int uiProtocol;
		coStream.open (
			1,
			"select "
				"protocol_id "
			"from "
				"ps.peer "
			"where "
				"host_name = :host_name /* char[100] */ "
				"and realm = :realm /* char[100] */",
			p_coDBConn);
		coStream
			<< p_soMsgInfoCache.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfoCache.m_psoSessInfo->m_coOriginRealm.v;
		coStream
			>> uiProtocol;
		coStream.close ();
		/* загружаем активные политики сессии */
		SDBAbonRule soRule;
		coStream.open (
			10,
			"select "
				"rule_id,"
				"rule_name,"
				"protocol_id "
			"from "
				"ps.sessionpolicy sp "
			"where "
				"sp.session_id = :session_id /* char[255] */ "
				"and sp.protocol_id = :protocol_id /* unsigned */ "
				"and time_end is null",
			p_coDBConn);
		coStream
			<< p_soMsgInfoCache.m_psoSessInfo->m_coSessionId
			<< uiProtocol;
		while (! coStream.eof ()) {
			coStream
				>> soRule.m_soRuleId.m_uiRuleId
				>> soRule.m_coRuleName
				>> soRule.m_soRuleId.m_uiProtocol;
			p_vectActive.push_back (soRule);
		}
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s: error: code: '%d'; description: '%s';\n", __FUNCTION__, coExcept.code, coExcept.msg);
	}
	return iRetVal;
}

/* загружает список идентификаторов правил абонента из БД */
int load_abon_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		otl_value<otl_datetime> coValidUntil;
		otl_value<otl_datetime> coRuleTimeEnd;
		otl_null coNULL;
		SRuleId soRuleId;
		coStream.open (
			10,
			"select "
				"pc.rule_id,"
				"pc.protocol_id,"
				"sp.valid_until,"
				"case when trunc(sysdate,'dd') + pc.end_time > sysdate then trunc(sysdate,'dd') + pc.end_time else trunc(sysdate,'dd') + pc.end_time + 1 end rule_time_end "
			"from "
				"ps.subscriberpolicy sp "
				"inner join ps.policy p on sp.policy_id = p.id "
				"inner join ps.policy_content pc on p.id = pc.policy_id "
				"inner join ps.protocol pp on pc.protocol_id = pp.id "
				"inner join ps.peer peer on pp.id = peer.protocol_id "
				"left join ps.apn apn on pc.apn_id = apn.id "
				"left join (ps.location l inner join ps.sgsn_node n on n.location = l.id) on pc.location_id = l.id "
				"left join ps.device_type dt on pc.device_type_id = dt.id "
			"where "
				"sp.subscriber_id = :subscriber_id /* char[64] */  "
				"and peer.host_name = :host_name /* char[100] */ "
				"and peer.realm = :realm /* char[100] */ "
				"and nvl(sp.blocked_until,sysdate - 1) < sysdate "
				"and nvl(sp.valid_until,sysdate + 1) > sysdate "
				"and nvl(sp.ignore_flag,0) = 0 "
				"and (pc.ip_can_type is null or pc.ip_can_type = :ip_can_type /* char[64] */ ) "
				"and (apn.access_point_name is null or apn.access_point_name = :apn_name /* char[255] */ ) "
				"and (n.ip_address is null or n.ip_address = :sgsn_node_ip_address /* char[16] */ ) "
				"and (dt.id is null or dt.id = nvl(:device_type_id /* unsigned */ ,dt.id)) "
				"and "
					"((nvl(pc.start_time,0) < nvl(pc.end_time,1) and sysdate between trunc(sysdate,'dd') + nvl(pc.start_time,0) and trunc(sysdate,'dd') + nvl(pc.end_time,1)) "
					"or (nvl(pc.start_time,0) > nvl(pc.end_time,1) and sysdate between trunc(sysdate,'dd') + nvl(pc.start_time,0) and trunc(sysdate,'dd') + nvl(pc.end_time,1) + 1))",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.v
			<< p_soMsgInfo.m_psoSessInfo->m_coIPCANType
			<< p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
			<< p_soMsgInfo.m_psoSessInfo->m_coSGSNAddress
			<< coNULL;
		while (! coStream.eof ()) {
			coStream
				>> soRuleId.m_uiRuleId
				>> soRuleId.m_uiProtocol
				>> coValidUntil
				>> coRuleTimeEnd;
			p_vectRuleList.push_back (soRuleId);
			/* если известна дата действия политик */
			if (! coValidUntil.is_null ()) {
				pcrf_server_db_insert_refqueue (p_coDBConn, coValidUntil.v, p_soMsgInfo.m_psoSessInfo->m_strSubscriberId);
			}
			/* если известна дата действия правила */
			if (! coRuleTimeEnd.is_null ()) {
				pcrf_server_db_insert_refqueue (p_coDBConn, coRuleTimeEnd.v, p_soMsgInfo.m_psoSessInfo->m_strSubscriberId);
			}
		}
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

/* загрузжает список идентификаторов правил по умолчанию */
int load_def_rule_list (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList)
{
	int iRetVal = 0;
	unsigned int uiProto;
	SRuleId soRuleId;

	try {
		otl_stream coStream;
		/* определяем по какому протоколу работает peer */
		coStream.open (
			1,
			"select protocol_id from ps.peer where host_name = :host_name /* char[100] */ and peer.realm = :realm /* char[100] */",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginHost.v
			<< p_soMsgInfo.m_psoSessInfo->m_coOriginRealm.v;
		coStream
			>> uiProto;
		coStream.close ();
		/* загружаем правила по умолчанию */
		switch (uiProto) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select r.id, 1 from ps.rule r where r.default_rule_flag <> 0",
				p_coDBConn);
			while (! coStream.eof ()) {
				coStream
					>> soRuleId.m_uiRuleId
					>> soRuleId.m_uiProtocol;
				p_vectRuleList.push_back (soRuleId);
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select r.id, 2 from ps.SCE_rule r where r.default_rule_flag <> 0",
				p_coDBConn);
			while (! coStream.eof ()) {
				coStream
					>> soRuleId.m_uiRuleId
					>> soRuleId.m_uiProtocol;
				p_vectRuleList.push_back (soRuleId);
			} 
			break;  /* Gx Cisco SCE */
		}
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

/* загружает список потоков правила */
int load_rule_flows (otl_connect &p_coDBConn, SMsgDataForDB &p_soMsgInfo, unsigned int p_uiRuleId, std::vector<std::string> &p_vectRuleFlows)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		std::string strFlowDescr;
		coStream.open (
			25,
			"select "
				"ft.flow_description "
			"from "
				"ps.rule_flows rf "
				"inner join ps.flow_template ft on rf.flow_template_id = ft.id "
			"where rf.rule_id = :rule_id<unsigned>",
			p_coDBConn);
		coStream << p_uiRuleId;
		while (! coStream.eof ()) {
			coStream >> strFlowDescr;
			p_vectRuleFlows.push_back (strFlowDescr);
		}
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

/* загружает описание правила */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	SRuleId &p_soRuleId,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;

	try {
		otl_stream coStream;
		SDBMonitoringInfo soMonitInfo;
		SDBAbonRule soAbonRule;
		switch (p_soRuleId.m_uiProtocol) {
		case 1: /* Gx */
			coStream.open (
				10,
				"select "
					"r.rule_name,"
					"r.dynamic_rule_flag,"
					"r.rule_group_flag,"
					"r.precedence_level,"
					"r.rating_group_id,"
					"r.service_id,"
					"r.metering_method,"
					"r.online_charging,"
					"r.offline_charging,"
					"qt.qos_class_identifier,"
					"qt.max_requested_bandwidth_ul,"
					"qt.max_requested_bandwidth_dl,"
					"qt.guaranteed_bitrate_ul,"
					"qt.guaranteed_bitrate_dl,"
					"mk.key_code,"
					"mk.dosage_total_octets,"
					"mk.dosage_output_octets,"
					"mk.dosage_input_octets,"
					"rs.redirect_address_type,"
					"rs.redirect_server_address "
				"from "
					"ps.rule r "
					"left join ps.qos_template qt on r.qos_template_id = qt.id "
					"left join ps.monitoring_key mk on r.monitoring_key_id = mk.id "
					"left join ps.redirection_server rs on r.redirection_server_id = rs.id "
				"where "
					"r.id = :rule_id<unsigned>",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			coStream
				>> soAbonRule.m_coRuleName
				>> soAbonRule.m_coDynamicRuleFlag
				>> soAbonRule.m_coRuleGroupFlag
				>> soAbonRule.m_coPrecedenceLevel
				>> soAbonRule.m_coRatingGroupId
				>> soAbonRule.m_coServiceId
				>> soAbonRule.m_coMeteringMethod
				>> soAbonRule.m_coOnlineCharging
				>> soAbonRule.m_coOfflineCharging
				>> soAbonRule.m_coQoSClassIdentifier
				>> soAbonRule.m_coMaxRequestedBandwidthUl
				>> soAbonRule.m_coMaxRequestedBandwidthDl
				>> soAbonRule.m_coGuaranteedBitrateUl
				>> soAbonRule.m_coGuaranteedBitrateDl
				>> soMonitInfo.m_coKeyName
				>> soMonitInfo.m_coDosageTotalOctets
				>> soMonitInfo.m_coDosageOutputOctets
				>> soMonitInfo.m_coDosageInputOctets
				>> soAbonRule.m_coRedirectAddressType
				>> soAbonRule.m_coRedirectServerAddress;
			CHECK_FCT (iFnRes = load_rule_flows (p_coDBConn, p_soMsgInfo, p_soRuleId.m_uiRuleId, soAbonRule.m_vectFlowDescr));
			if (0 == iFnRes) {
				soAbonRule.m_soRuleId = p_soRuleId;
				soAbonRule.m_vectMonitInfo.push_back (soMonitInfo);
				p_vectAbonRules.push_back (soAbonRule);
			} else {
				iRetVal = iFnRes;
			}
			break; /* Gx */
		case 2: /* Gx Cisco SCE */
			coStream.open (
				10,
				"select "
					"r.name,"
					"r.PACKAGE,"
					"r.REAL_TIME_MONITOR,"
					"r.VLINK_UPSTREAM,"
					"r.VLINK_DOWNSTREAM "
				"from "
					"ps.SCE_rule r "
				"where "
					"r.id = :rule_id /* unsigned */",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			coStream
				>> soAbonRule.m_coRuleName
				>> soAbonRule.m_coSCE_PackageId
				>> soAbonRule.m_coSCE_RealTimeMonitor
				>> soAbonRule.m_coSCE_UpVirtualLink
				>> soAbonRule.m_coSCE_DownVirtualLink;
			soAbonRule.m_soRuleId = p_soRuleId;
			coStream.close ();
			coStream.open (
				10,
				"select "
					"mk.key_code,"
					"mk.dosage_total_octets,"
					"mk.dosage_output_octets,"
					"mk.dosage_input_octets "
				"from "
					"ps.sce_monitoring_key s "
					"inner join ps.monitoring_key mk on s.monitoring_key_id = mk.id "
				"where "
					"s.sce_rule_id = :rule_id /* unsigned */",
				p_coDBConn);
			coStream
				<< p_soRuleId.m_uiRuleId;
			while (! coStream.eof ()) {
				coStream
					>> soMonitInfo.m_coKeyName
					>> soMonitInfo.m_coDosageTotalOctets
					>> soMonitInfo.m_coDosageOutputOctets
					>> soMonitInfo.m_coDosageInputOctets;
				soAbonRule.m_vectMonitInfo.push_back (soMonitInfo);
			}
			p_vectAbonRules.push_back (soAbonRule);
			break; /* Gx Cisco SCE */
		}
	} catch (otl_exception coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

/* загружает описание правил */
int load_rule_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SRuleId> &p_vectRuleList,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	int iFnRes;
	std::vector<SRuleId>::iterator iter = p_vectRuleList.begin ();

	for (; iter != p_vectRuleList.end (); ++iter) {
		load_rule_info (p_coDBConn, p_soMsgInfo, *iter, p_vectAbonRules);
	}

	return iRetVal;
}

int pcrf_server_db_load_session_info (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.open (
			1,
			"select "
				"subscriber_id,"
				"called_station_id,"
				"ip_can_type,"
				"sgsn_address,"
				"sysdate,"
				"origin_host,"
				"origin_realm "
			"from "
				"ps.sessionList "
			"where "
				"session_id = :session_id<char[255]>",
			p_coDBConn);
		coStream
			<< p_soMsgInfo.m_psoSessInfo->m_coSessionId;
		coStream
			>> p_soMsgInfo.m_psoSessInfo->m_strSubscriberId
			>> p_soMsgInfo.m_psoSessInfo->m_coCalledStationId
			>> p_soMsgInfo.m_psoSessInfo->m_coIPCANType
			>> p_soMsgInfo.m_psoSessInfo->m_coSGSNAddress
			>> p_soMsgInfo.m_psoSessInfo->m_coTimeLast
			>> p_soMsgInfo.m_psoSessInfo->m_coOriginHost
			>> p_soMsgInfo.m_psoSessInfo->m_coOriginRealm;
		if (! p_soMsgInfo.m_psoSessInfo->m_coIPCANType.is_null ()) {
			p_soMsgInfo.m_psoSessInfo->m_iIPCANType = atoi (p_soMsgInfo.m_psoSessInfo->m_coIPCANType.v.c_str ());
		}
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}

int pcrf_server_db_abon_rule (
	otl_connect &p_coDBConn,
	SMsgDataForDB &p_soMsgInfo,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* очищаем список перед выполнением */
	p_vectAbonRules.clear ();

	do {
		/* список идентификаторов правил абонент */
		std::vector<SRuleId> vectRuleList;
		/* если идентификатора подписчика определен */
		if (p_soMsgInfo.m_psoSessInfo->m_strSubscriberId.length ()) {
			/* если абонент определен */
			/* загружаем правила абонента */
			load_abon_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		} else {
			/* если абонент не найден */
			/* загружаем правила по умолчанию */
			load_def_rule_list (p_coDBConn, p_soMsgInfo, vectRuleList);
		}
		/* если список идентификаторов правил не пустой */
		if (vectRuleList.size ()) {
			load_rule_info (p_coDBConn, p_soMsgInfo, vectRuleList, p_vectAbonRules);
		}
	} while (0);

	return iRetVal;
}

int pcrf_server_db_insert_refqueue (otl_connect &p_coDBConn, otl_datetime &p_coDateTime, std::string &p_strSubscriberId)
{
	int iRetVal = 0;

	try {
		otl_stream coStream;
		coStream.set_commit (0);
		coStream.open (
			1,
			"insert into ps.refreshQueue (module, subscriber_id, refresh_date) values ('pcrf', :subscriber_id /* char[64] */, :refresh_date /* timestamp */)",
			p_coDBConn);
		coStream
			<< p_strSubscriberId
			<< p_coDateTime;
		p_coDBConn.commit ();
	} catch (otl_exception &coExcept) {
		iRetVal = coExcept.code;
		printf ("%s:%d: error: code: '%d'; description: '%s';\n", __FILE__, __LINE__, coExcept.code, coExcept.msg);
	}

	return iRetVal;
}
