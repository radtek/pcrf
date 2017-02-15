#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/stat/stat.h"

#include <vector>
#include <stdio.h>

extern CLog *g_pcoLog;

static SStat *g_psoReqStat;
static SStat *g_psoPeerStat;

static void pcrf_tracer (
	fd_hook_type p_eHookType,
	msg * p_psoMsg,
	peer_hdr * p_psoPeer,
	void * p_pOther,
	fd_hook_permsgdata *p_psoPMD,
	void * p_pRegData)
{
  if (NULL == p_psoMsg) {
    UTL_LOG_E(*g_pcoLog, "NULL pointer to message structure");
    return;
  }

  msg_hdr *psoMsgHdr;

  CHECK_FCT_DO(fd_msg_hdr(p_psoMsg, &psoMsgHdr), return);

  int iFnRes;
  std::string strRequestType;
  std::string strPeerName;

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
    if (0 < iFnRes) {
      if (sizeof(mcCode) > static_cast<size_t>(iFnRes)) {
      } else {
        mcCode[sizeof(mcCode) - 1] = '\0';
      }
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

  /* статистика по запросам */
  stat_measure(g_psoReqStat, strRequestType.c_str(), NULL);

  if (NULL != p_psoPeer) {
    strPeerName.insert(0, reinterpret_cast<char*>(p_psoPeer->info.pi_diamid), p_psoPeer->info.pi_diamidlen);
  } else {
    strPeerName = "<unknown peer>";
  }

  /* статистика по пирам */
  stat_measure(g_psoPeerStat, strPeerName.c_str(), NULL);

  /* если нет необходимости трассировки */
  if (0 == g_psoConf->m_iTraceReq) {
    return;
  }

	CTimeMeasurer coTM;
	char *pmcBuf = NULL;
	size_t stLen;
	otl_nocommit_stream coStream;
	char mcEnumValue[256];
	otl_connect *pcoDBConn = NULL;

  /* suppress compiler warning */
  p_pOther = p_pOther; p_psoPMD = p_psoPMD; p_pRegData = p_pRegData;

  if (p_psoMsg) {
		CHECK_MALLOC_DO (
			fd_msg_dump_treeview (&pmcBuf, &stLen, NULL, p_psoMsg, fd_g_config->cnf_dict, 1, 1),
			{ UTL_LOG_E (*g_pcoLog, "Error while dumping a message"); return; });
	}

	std::string strSessionId;
	std::string strCCReqType;
	std::string strOriginHost;
	std::string strOriginReal;
	std::string strDestinHost;
	std::string strDestinReal;
	std::string strResultCode;
	/* добываем необходимые значения из запроса */
	msg_or_avp *psoMsgOrAVP;
	avp_hdr *psoAVPHdr;
	avp *psoAVP;
	iFnRes = fd_msg_browse_internal (p_psoMsg, MSG_BRW_FIRST_CHILD, &psoMsgOrAVP, NULL);
	if (iFnRes)
		goto free_and_exit;
	do {
		psoAVP = (avp*)psoMsgOrAVP;
		/* получаем заголовок AVP */
		if (NULL == psoMsgOrAVP)
			break;
		if (fd_msg_avp_hdr ((avp*)psoMsgOrAVP, &psoAVPHdr)) {
			continue;
		}
		/* нас интересуют лишь вендор Diameter */
		if (psoAVPHdr->avp_vendor != 0 && psoAVPHdr->avp_vendor != (vendor_id_t)-1) {
			continue;
		}
		switch (psoAVPHdr->avp_code) {
		case 263: /* Session-Id */
			strSessionId.insert (0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 264: /* Origin-Host */
			strOriginHost.insert (0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 268: /* Result-Code */
		{
			iFnRes = pcrf_extract_avp_enum_val (psoAVPHdr, mcEnumValue, sizeof (mcEnumValue));
			if (0 == iFnRes)
				strResultCode = mcEnumValue;
		}
		break;
		case 283: /* Destination-Realm */
			strDestinReal.insert (0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 293: /* Destination-Host */
			strDestinHost.insert (0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 296: /* Origin-Realm */
			strOriginReal.insert (0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
			break;
		case 416: /* CC-Request-Type */
		{
			iFnRes = pcrf_extract_avp_enum_val (psoAVPHdr, mcEnumValue, sizeof (mcEnumValue));
			if (0 == iFnRes)
				strCCReqType = mcEnumValue;
		}
		break;
		}
	} while (0 == fd_msg_browse_internal (psoAVP, MSG_BRW_NEXT, &psoMsgOrAVP, NULL));
	/* опционально для CC определяем тип ——-запроса */
	if (psoMsgHdr->msg_code == 272 && strCCReqType.length ()) {
		strRequestType += '-';
		strRequestType += strCCReqType[0];
	}
	/* проверяем наличие обязательных атрибутов */
	if (0 == strOriginHost.length ()) {
		switch (p_eHookType)
		{
		case HOOK_MESSAGE_RECEIVED:
			strOriginHost = strPeerName;
			break;
		case HOOK_MESSAGE_LOCAL:
		case HOOK_MESSAGE_SENT:
			strOriginHost.insert (0, (const char*)fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
			break;
		default:
			break;
		}
	}
	if (0 == strDestinHost.length ()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			strDestinHost.insert (0, (const char*)fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
			break;
		case HOOK_MESSAGE_SENT:
			strDestinHost = strPeerName;
			break;
		default:
			break;
		}
	}
	/* проверяем возможность заполнения опциональных атрибутов */
	if (0 == strOriginReal.length ()) {
		switch (p_eHookType)
		{
		case HOOK_MESSAGE_SENT:
			strOriginReal.insert (0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
		default:
			break;
		}
	}
	if (0 == strDestinReal.length ()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			strDestinReal.insert (0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
		default:
			break;
		}
	}

	/* пытаемся сохранить данные в БД */
#ifdef DEBUG
  iFnRes = pcrf_db_pool_get(&pcoDBConn, __FUNCTION__, 1);
#else
  iFnRes = pcrf_db_pool_get(&pcoDBConn, __FUNCTION__, 0);
#endif
  if (0 != iFnRes || NULL == pcoDBConn) {
    goto free_and_exit;
  }
	try {
		otl_null coNull;
		otl_value<std::string> coOTLOriginReal;
		otl_value<std::string> coOTLDestinReal;
		otl_value<std::string> coOTLResultCode;
		if (strOriginReal.length ())
			coOTLOriginReal = strOriginReal;
		if (strDestinReal.length ())
			coOTLDestinReal = strDestinReal;
		if (strResultCode.length ())
			coOTLResultCode = strResultCode;
		coStream.open (1,
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
		pcoDBConn->commit ();
		coStream.close ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E (*g_pcoLog, "code: '%d'; description: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		if (coStream.good ())
			coStream.close ();
	}

free_and_exit:
	if (pcoDBConn) {
		pcrf_db_pool_rel (pcoDBConn, __FUNCTION__);
	}

	if (pmcBuf) {
		fd_cleanup_buffer (pmcBuf);
	}
}

fd_hook_hdl *psoHookHandle = NULL;

int pcrf_tracer_init (void)
{
	int iRetVal = 0;

	CHECK_FCT (fd_hook_register (HOOK_MASK (HOOK_MESSAGE_RECEIVED, HOOK_MESSAGE_SENT), pcrf_tracer, NULL, NULL, &psoHookHandle));

  g_psoPeerStat = stat_get_branch("peer stat");
  g_psoReqStat = stat_get_branch("req stat");

	return iRetVal;
}

void pcrf_tracer_fini (void)
{
	if (psoHookHandle) {
		CHECK_FCT_DO (fd_hook_unregister (psoHookHandle), );
	}
}
