#include "app_rx.h"
#include "app_rx_data_types.h"
#include "dict_rx/dict_rx.h"

#include "utils/dbpool/dbpool.h"
#include "app_pcrf/app_pcrf_header.h"

#include "utils/log/log.h"

extern CLog *g_pcoLog;

static disp_hdl * app_rx_aar_cb_hdl = NULL;
static int app_rx_aar_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *);

/* выборка данных из AAR */
static int app_rx_extract_aar (msg_or_avp *p_psoMsg, SAAR &p_soAAR);
/* выборка данных из Media-Component-Description */
static int app_rx_extract_mcd (avp *p_psoAVP, std::vector<SMCD> &p_vectMCD);
/* выборка данных из Media-Sub-Component */
static int app_rx_extract_msc (avp *p_psoAVP, std::vector<SMSC> &p_vectMSC);
/* выборка из Subscription-Id */
static int app_rx_extract_si (avp *p_psoAVP, SSId &p_soSI);
/* выборка данных из Supported-Features */
static int app_rx_extract_sf (avp *p_psoAVP, SSF &p_soSF);
/* выборка данных из Sponsored-Connectivity-Data */
static int app_rx_extract_scd (avp *p_psoAVP, otl_value<SSCD> &p_coSCD);
/* выборка данных из Granted-Service-Unit */
static int app_rx_extract_gsu (avp *p_psoAVP, otl_value<SGSU> &p_coGSU);
/* выборка данных из Used-Service-Unit */
static int app_rx_extract_usu (avp *p_psoAVP, otl_value<SUSU> &p_coUSU);
/* выборка данных из CC-Money*/
static int app_rx_extract_ccm (avp *p_psoAVP, otl_value<SCCMoney> &p_coCCMoney);
/* выборка данных из Unit-Value */
static int app_rx_extract_uv (avp *p_psoAVP, otl_value<SUnitValue> &p_coUV);

/* сохраняем сессию в БД */
static int app_rx_store_req(SAAR &p_soAAR, SMsgDataForDB &p_soIPCANSess, otl_connect *p_pcoDBConn);

int app_rx_register_aar_cb ()
{
  struct disp_when data;

  memset (&data, 0, sizeof(data));
	data.app = g_psoDictAppRx;
	data.command = g_psoDictCmdAAR;

  /* Now specific handler for AAR */
	CHECK_FCT (fd_disp_register (app_rx_aar_cb, DISP_HOW_CC, &data, NULL, &app_rx_aar_cb_hdl));

  return 0;
}

int pcrf_server_find_IPCAN_session_byframedip(otl_connect &p_coDBConn, otl_value<std::string> &p_coIPAddr, SSessionInfo &p_soIPCANSessInfo, SStat *p_psoStat)
{
  if (0 == p_coIPAddr.is_null()) {
    LOG_D("Framed-IP-Address: '%s'; Session-Id: '%s'; Origin-Host: '%s'; Origin-Realm: '%s'",
      p_coIPAddr.v.c_str(),
      p_soIPCANSessInfo.m_coSessionId.is_null() ? "<null>" : p_soIPCANSessInfo.m_coSessionId.v.c_str(),
      p_soIPCANSessInfo.m_coOriginHost.is_null() ? "<null>" : p_soIPCANSessInfo.m_coOriginHost.v.c_str(),
      p_soIPCANSessInfo.m_coOriginRealm.is_null() ? "<null>" : p_soIPCANSessInfo.m_coOriginRealm.v.c_str());
    return (pcrf_server_find_ugw_session_byframedip(p_coDBConn, p_coIPAddr.v, p_soIPCANSessInfo, p_psoStat));
  } else {
    LOG_D("Framed-IP-Address is empty");
    return EINVAL;
  }
}

int app_rx_install_QoS(SMsgDataForDB &p_soIPCANSessInfo, std::vector<SMCD> &p_vectMediaComponent, otl_connect &p_pcoDBConn)
{
  if (0 != p_vectMediaComponent.size()) {
  } else {
    LOG_D("empty Media-Component vector");
    return EINVAL;
  }

  int iRetVal;
  SDBAbonRule soAbonRule;
  std::vector<SDBAbonRule> vectNewRule;
  SRARResult *psoRARRes = NULL;

  for (std::vector<SMCD>::iterator iterMCD = p_vectMediaComponent.begin(); iterMCD != p_vectMediaComponent.end(); ++iterMCD) {
    /* формируем новое правило для ims-сессии */
    soAbonRule.m_bIsRelevant = true;
    soAbonRule.m_coRuleName = "IMS Session";
    soAbonRule.m_coDynamicRuleFlag = 1;
    soAbonRule.m_coQoSClassIdentifier = 1;
    soAbonRule.m_soARP.m_coPriorityLevel = 9;
    soAbonRule.m_soARP.m_coPreemptionCapability = 1;    /* PRE-EMPTION_CAPABILITY_DISABLED */
    soAbonRule.m_soARP.m_coPreemptionVulnerability = 0; /* PRE-EMPTION_VULNERABILITY_ENABLED */
    /* Flow */
    for (std::vector<SMSC>::iterator iterFlow = iterMCD->m_vectMediaSubComponent.begin(); iterFlow != iterMCD->m_vectMediaSubComponent.end(); ++iterFlow) {
      if (0 == iterFlow->m_mcoFlowDescription[0].is_null()) {
        soAbonRule.m_vectFlowDescr.push_back(iterFlow->m_mcoFlowDescription[0].v);
      }
      if (0 == iterFlow->m_mcoFlowDescription[1].is_null()) {
        soAbonRule.m_vectFlowDescr.push_back(iterFlow->m_mcoFlowDescription[1].v);
      }
    }

    vectNewRule.push_back(soAbonRule);
  }
  /* создаем объект структуры для контроля выполнения запроса */
  psoRARRes = new SRARResult;
  if (!psoRARRes->m_bInit) {
    goto clean_and_exit;
  }
  /* посылаем запрос */
  CHECK_FCT_DO((iRetVal = pcrf_client_RAR(&p_pcoDBConn, p_soIPCANSessInfo, NULL, vectNewRule, psoRARRes, 500000)), goto clean_and_exit);
  /* ждем результат */
  CHECK_FCT_DO((iRetVal = pthread_mutex_lock(&psoRARRes->m_mutexWait)), goto clean_and_exit);

  /* если все операции завершились успешно */
  if (0 == iRetVal) {
    /* анализируем результат выполнения */
    iRetVal = ((psoRARRes->m_iResultCode == ER_DIAMETER_SUCCESS) ? 0 : iRetVal);
  }

  clean_and_exit:
  if (psoRARRes) {
    delete psoRARRes;
    psoRARRes = NULL;
  }

  return 0;
}

int app_rx_aar_cb ( struct msg **p_ppMsg, struct avp *p_avp, struct session *p_session, void *p_opaque, enum disp_action *p_dispAction )
{
  /* suppress compiler warnings */
  p_avp = p_avp; p_session = p_session; p_opaque = p_opaque; p_dispAction = p_dispAction;

  msg *pMsg = *p_ppMsg;
  SAAR soAAR;
  msg *psoAns;
  avp *psoAVP;
  avp_value soAVPVal;
  int iResultCode = 2001; /*  DIAMETER_SUCCESS */
  otl_connect *pcoDBConn = NULL;
  SMsgDataForDB soIPCANSess;

  CHECK_FCT(pcrf_server_DBstruct_init(&soIPCANSess));
  avp_value soAVPValue;

  CHECK_FCT_DO( app_rx_extract_aar (pMsg, soAAR), /* continue */ );

  /* Create answer header */
  CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, p_ppMsg, 0), goto cleanup_and_exit);

  /* запрашиваем подключение к БД */
  CHECK_FCT_DO(pcrf_db_pool_get(reinterpret_cast<void**>(&pcoDBConn), __FUNCTION__, NULL, 1), goto answer);

  /* обработка запроса */
  /* поиск активной Интернет-сессии */
  CHECK_FCT_DO(pcrf_server_find_IPCAN_session_byframedip(*pcoDBConn, soAAR.m_coFramedIPAddress, *soIPCANSess.m_psoSessInfo, NULL), iResultCode = 5065; goto answer);
  /* загружаем дополнительные сведения о сессии */
  CHECK_FCT_DO(pcrf_server_db_load_session_info(*pcoDBConn, soIPCANSess, soIPCANSess.m_psoSessInfo->m_coSessionId.v, NULL), iResultCode = 5065; goto answer);
  CHECK_FCT_DO(pcrf_peer_dialect(*soIPCANSess.m_psoSessInfo), iResultCode = 5065; goto answer);
  LOG_D("session info: '%s', '%s', '%s', '%s', '%d', '%d'",
    soIPCANSess.m_psoSessInfo->m_coSessionId.is_null() ? "<null>" : soIPCANSess.m_psoSessInfo->m_coSessionId.v.c_str(),
    soIPCANSess.m_psoSessInfo->m_coOriginHost.is_null() ? "<null>" : soIPCANSess.m_psoSessInfo->m_coOriginHost.v.c_str(),
    soIPCANSess.m_psoSessInfo->m_coOriginRealm.is_null() ? "<null>" : soIPCANSess.m_psoSessInfo->m_coOriginRealm.v.c_str(),
    soIPCANSess.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType.is_null() ? "<null>" : soIPCANSess.m_psoReqInfo->m_soUserLocationInfo.m_coIPCANType.v.c_str(),
    soIPCANSess.m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType,
    soIPCANSess.m_psoSessInfo->m_uiPeerDialect);

  /* сохраняем информацию о сессии в БД */
  CHECK_FCT_DO(app_rx_store_req(soAAR, soIPCANSess, pcoDBConn), /* continue */ );

  /* в случае необходимости инсталлируем правило */
  if (soAAR.m_vectMediaComponentDescription.size()
      && soAAR.m_vectMediaComponentDescription.at(0).m_vectMediaSubComponent.size()
      && 0 == soAAR.m_vectMediaComponentDescription.at(0).m_vectMediaSubComponent.at(0).m_coFlowUsage.is_null()
      && soAAR.m_vectMediaComponentDescription.at(0).m_vectMediaSubComponent.at(0).m_coFlowUsage.v != 2) {
    /* посылаем запрос на инсталляцию нового правила */
    CHECK_FCT_DO(app_rx_install_QoS(soIPCANSess, soAAR.m_vectMediaComponentDescription, *pcoDBConn), iResultCode = 5063; goto answer);
  }

answer:

  psoAns = *p_ppMsg;

  /* Set Auth-Application-Id */
  do {
    CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPAuthApplicationId, 0, &psoAVP), break);
    soAVPVal.u32 = APP_RX_ID;
    CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPVal), break);
    CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), break);
  } while (0);

  /* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
  switch (iResultCode) {
  case ER_DIAMETER_SUCCESS:  /*  DIAMETER_SUCCESS */
    CHECK_FCT_DO(fd_msg_rescode_set(psoAns, const_cast<char*>("DIAMETER_SUCCESS"), NULL, NULL, 1), /*continue*/);
    /* IP-CAN-Type */
    {
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPIPCANType, 0, &psoAVP), goto cleanup_and_exit);
      soAVPValue.i32 = soIPCANSess.m_psoReqInfo->m_soUserLocationInfo.m_iIPCANType;
      CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto cleanup_and_exit);
      CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), goto cleanup_and_exit);
    }
    break;    /*  DIAMETER_SUCCESS */
  default:
    /* set the Origin-Host */
    {
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPOriginHost, 0, &psoAVP), goto cleanup_and_exit);
      soAVPValue.os.data = reinterpret_cast<uint8_t*>(fd_g_config->cnf_diamid);
      soAVPValue.os.len = fd_g_config->cnf_diamid_len;
      CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto cleanup_and_exit);
      CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), goto cleanup_and_exit);
    }
    /* set the Origin-Realm */
    {
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPOriginRealm, 0, &psoAVP), goto cleanup_and_exit);
      soAVPValue.os.data = reinterpret_cast<uint8_t*>(fd_g_config->cnf_diamrlm);
      soAVPValue.os.len = fd_g_config->cnf_diamrlm_len;
      CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto cleanup_and_exit);
      CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), goto cleanup_and_exit);
    }
    /* Experimental-Result && Experimental-Result-Code */
    {
      avp *psoAVPChild;

      /* Experimental-Result */
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPExperimentalResult, 0, &psoAVP), goto cleanup_and_exit);
      /* Vendor-Id */
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPVendorId, 0, &psoAVPChild), goto cleanup_and_exit);
      soAVPValue.i32 = 10415;
      CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPValue), goto cleanup_and_exit);
      CHECK_FCT_DO(fd_msg_avp_add(psoAVP, MSG_BRW_LAST_CHILD, psoAVPChild), goto cleanup_and_exit);
      /* Experimental-Result-Code */
      CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPExperimentalResultCode, 0, &psoAVPChild), goto cleanup_and_exit);
      soAVPValue.i32 = iResultCode;
      CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVPChild, &soAVPValue), goto cleanup_and_exit);
      CHECK_FCT_DO(fd_msg_avp_add(psoAVP, MSG_BRW_LAST_CHILD, psoAVPChild), goto cleanup_and_exit);
      /**/
      CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), goto cleanup_and_exit);
    }
    break;
  }

  /* если ответ сформирован отправляем его */
  if (psoAns) {
    CHECK_FCT_DO( fd_msg_send(p_ppMsg, NULL, NULL), /*continue*/ );
  }

  cleanup_and_exit:
  pcrf_server_DBStruct_cleanup(&soIPCANSess);
  if (pcoDBConn) {
    pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
    pcoDBConn = NULL;
  }

  return 0;
}

static int app_rx_extract_aar (msg_or_avp *p_psoMsg, SAAR &p_soAAR)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoMsg) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoMsg, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 8: /* Framed-IP-Address */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId );
        app_rx_ip_addr_to_string(psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len, p_soAAR.m_coFramedIPAddress);
        break; /* Framed-IP-Address */
      case 30: /* Called-Station-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coCalledStationId.v.insert (0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coCalledStationId.set_non_null ();
        break; /* Called-Station-Id */
      case 97: /* Framed-Ipv6-Prefix */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coFramedIpv6Prefix.v.insert (0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coFramedIpv6Prefix.set_non_null ();
        break; /* Framed-Ipv6-Prefix */
      case 238: /* Origin-State-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coOriginStateId = psoAVPHdr->avp_value->u32;
        break; /* Origin-State-Id */
      case 258: /* Auth-Application-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coAuthApplicationId = psoAVPHdr->avp_value->u32;
        break; /* Auth-Application-Id */
      case 263: /* Session-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coSessionId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coSessionId.set_non_null ();
        }
        break; /* Session-Id */
      case 264: /* Origin-Host */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginHost.set_non_null ();
        }
        break; /* Origin-Host */
      case 277: /* Auth-Session-State */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coAuthSessionState = psoAVPHdr->avp_value->i32;
        break; /* Auth-Session-State */
      case 282: /* Route-Record */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          std::string strRouteRecord;
          strRouteRecord.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_vectRouteRecord.push_back (strRouteRecord);
        }
        break; /* Route-Record */
      case 283: /* Destination-Realm */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationRealm.set_non_null ();
        }
        break; /* Destination-Realm */
      case 284: /* Proxy-Info */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          SProxyInfo soPI;
          app_rx_extract_pi (psoAVP, soPI);
          p_soAAR.m_vectProxyInfo.push_back (soPI);
        }
        break; /* Proxy-Info */
      case 293: /* Destination-Host */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationHost.set_non_null ();
        }
        break; /* Destination-Host */
      case 296: /* Origin-Realm */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginRealm.set_non_null ();
        }
        break; /* Origin-Realm */
      case 443: /* Subscription-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          SSId soSI;
          if (0 == app_rx_extract_si(psoAVP, soSI)) {
            LOG_D("Subscription-Id: Ok");
            p_soAAR.m_vectSubscriptionId.push_back(soSI);
          } else {
            LOG_D("Subscription-Id: incorrect!!!");
          }
        }
        break; /* Subscription-Id */
      case 621: /* OC-Supported-Features */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_ocsf (psoAVP, p_soAAR.m_coOCSupportedFeatures);
        break; /* OC-Supported-Features */
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 458: /* Reservation-Priority */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coReservationPriority = psoAVPHdr->avp_value->i32;
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coAFApplicationIdentifier.set_non_null ();
        }
        break; /* AF-Application-Identifier */
      case 513: /* Specific-Action */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_vectSpecificAction.push_back (psoAVPHdr->avp_value->i32);
        break; /* Specific-Action */
      case 517: /* Media-Component-Description */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_mcd (psoAVP, p_soAAR.m_vectMediaComponentDescription);
        break; /* Media-Component-Description */
      case 523: /* SIP-Forking-Indication */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coSIPForkingIndication = psoAVPHdr->avp_value->i32;
        break; /* SIP-Forking-Indication */
      case 525: /* Service-URN */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coServiceURN.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coServiceURN.set_non_null ();
        break; /* Service-URN */
      case 527: /* Service-Info-Status */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coServiceInfoStatus = psoAVPHdr->avp_value->i32;
        break; /* Service-Info-Status */
      case 528: /* MPS-Identifier */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coMPSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        break; /* MPS-Identifier */
      case 530: /* Sponsored-Connectivity-Data */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_scd (psoAVP, p_soAAR.m_coSponsoredConnectivityData);
        break; /* Sponsored-Connectivity-Data */
      case 533: /* Rx-Request-Type */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coRxRequestType = psoAVPHdr->avp_value->i32;
        break; /* Rx-Request-Type */
      case 536: /* Required-Access-Info */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_vectRequiredAccessInfo.push_back (psoAVPHdr->avp_value->i32);
        break; /* Required-Access-Info */
      case 538: /* GCS-Identifier */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soAAR.m_coGCSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coGCSIdentifier.set_non_null ();
        break; /* GCS-Identifier */
      case 629: /* Supported-Features */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          SSF soSF;
          app_rx_extract_sf (psoAVP, soSF);
          p_soAAR.m_vectSupportedFeatures.push_back (soSF);
        }
        break; /* Supported-Features */
      case 537: /* IP-Domain-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coIPDomainId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coIPDomainId.set_non_null ();
        }
        break; /* IP-Domain-Id */
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* 3GPP */
    default:
      LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      break;
    }
  } while (0 == fd_msg_browse_internal ((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  return iRetVal;
}

int app_rx_extract_mcd (avp *p_psoAVP, std::vector<SMCD> &p_vectMCD)
{
  int iRetVal = 0;
  SMCD soMCD;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 458: /* Reservation-Priority */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coReservationPriority = psoAVPHdr->avp_value->i32;
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        soMCD.m_coAFApplicationIdentifier.set_non_null ();
        break; /* AF-Application-Identifier */
      case 511: /* Flow-Status */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        break; /* Flow-Status */
      case 515: /* Max-Requested-Bandwidth-DL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-UL */
      case 518: /* Media-Component-Number */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMediaComponentNumber = psoAVPHdr->avp_value->u32;
        break; /* Media-Component-Number */
      case 519: /* Media-Sub-Component */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_msc (psoAVP, soMCD.m_vectMediaSubComponent);
        break; /* Media-Sub-Component */
      case 520: /* Media-Type */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMediaType = psoAVPHdr->avp_value->i32;
        app_rx_get_enum_val (tVenId, psoAVPHdr->avp_code, psoAVPHdr->avp_value->i32, soMCD.m_coMediaTypeEnum);
        break; /* Media-Type */
      case 521: /* RR-Bandwidth */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coRRBandwidth = psoAVPHdr->avp_value->u32;
        break; /* RR-Bandwidth */
      case 522: /* RS-Bandwidth */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coRSBandwidth = psoAVPHdr->avp_value->u32;
        break; /* RS-Bandwidth */
      case 524: /* Codec-Data */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          std::string strCodecData;
          strCodecData.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          soMCD.m_vectCodecData.push_back (strCodecData);
        }
        break; /* Codec-Data */
      case 534: /* Min-Requested-Bandwidth-DL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMinRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Min-Requested-Bandwidth-DL */
      case 535: /* Min-Requested-Bandwidth-UL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMCD.m_coMinRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Min-Requested-Bandwidth-UL */
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_vectMCD.push_back (soMCD);

  return iRetVal;
}

int app_rx_extract_msc (avp *p_psoAVP, std::vector<SMSC> &p_vectMSC)
{
  int iRetVal = 0;
  size_t iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 507: /* Flow-Description */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (iInd < sizeof(soMSC.m_mcoFlowDescription)/sizeof(*soMSC.m_mcoFlowDescription)) {
          soMSC.m_mcoFlowDescription[iInd].v.insert (0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          soMSC.m_mcoFlowDescription[iInd].set_non_null ();
          ++iInd;
        }
        break; /* Flow-Description */
      case 509: /* Flow-Number */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coFlowNumber = psoAVPHdr->avp_value->u32;
        break; /* Flow-Number */
      case 511: /* Flow-Status */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        break; /* Flow-Status */
      case 512: /* Flow-Usage */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coFlowUsage = psoAVPHdr->avp_value->i32;
        break; /* Flow-Usage */
      case 515: /* Max-Requested-Bandwidth-DL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-UL */
      case 529: /* AF-Signalling-Protocol */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        soMSC.m_coAFSignallingProtocol = psoAVPHdr->avp_value->i32;
        break; /* AF-Signalling-Protocol */
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && psoAVP2 != NULL);

  p_vectMSC.push_back (soMSC);

  return iRetVal;
}

static int app_rx_extract_si (avp *p_psoAVP, SSId &p_soSI)
{
  int iRetVal = -1;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;
  bool bType = false, bData = false;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (msg_or_avp **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 444: /* Subscription-Id-Data */
        bData = true;
        p_soSI.m_coSubscriptionIdData.v.insert(0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soSI.m_coSubscriptionIdData.set_non_null();
        LOG_D( "Subscription-Id-Data: %s", p_soSI.m_coSubscriptionIdData.v.c_str() );
        break; /* Subscription-Id-Data */
      case 450: /* Subscription-Id-Type */
        bType = true;
        p_soSI.m_coSubscriptionIdType = psoAVPHdr->avp_value->i32;
        LOG_D( "Subscription-Id-Type: %u", p_soSI.m_coSubscriptionIdType.v );
        break; /* Subscription-Id-Type */
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      }
      break; /* 3GPP */
    default:
      LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
    }
  } while (0 == fd_msg_browse_internal(psoAVP, MSG_BRW_NEXT, (msg_or_avp **)&psoAVP2, NULL) && NULL != psoAVP2);

  if (bType && bData) {
    iRetVal = 0;
  } else {
    LOG_D( "incomplete data set: data: %s; type: %s", bData ? "true" : "false", bType ? "true" : "false" );
  }

  return iRetVal;
}

int app_rx_extract_ocsf (avp *p_psoAVP, otl_value<SOCSF> &p_coOCSF)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 622: /* OC-Feature-Vector */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coOCSF.v.m_coOCFeatureVector = psoAVPHdr->avp_value->u64;
        p_coOCSF.v.m_coOCFeatureVector.set_non_null ();
        break; /* OC-Feature-Vector */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coOCSF.set_non_null ();

  return iRetVal;
}

static int app_rx_extract_sf (avp *p_psoAVP, SSF &p_soSF)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 266: /* Vendor-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soSF.m_coVendorId = psoAVPHdr->avp_value->u32;
        break; /* Vendor-Id */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 629: /* Feature-List-ID */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soSF.m_coFeatureListID = psoAVPHdr->avp_value->u32;
        break; /* Feature-List-ID */
      case 630: /* Feature-List */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soSF.m_coFeatureList = psoAVPHdr->avp_value->u32;
        break; /* Feature-List */
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  return iRetVal;
}

static int app_rx_extract_scd (avp *p_psoAVP, otl_value<SSCD> &p_coSCD)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 431: /* Granted-Service-Unit */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_gsu (psoAVP, p_coSCD.v.m_coGrantedServiceUnit);
        break; /* Granted-Service-Unit */
      case 446: /* Used-Service-Unit */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_usu (psoAVP, p_coSCD.v.m_coUsedServiceUnit);
        break; /* Used-Service-Unit */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      case 531: /* Sponsor-Identity */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coSCD.v.m_coSponsorIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coSponsorIdentity.set_non_null ();
        break; /* Sponsor-Identity */
      case 532: /* Application-Service-Provider-Identity */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coSCD.v.m_coApplicationServiceProviderIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coApplicationServiceProviderIdentity.set_non_null ();
        break; /* Application-Service-Provider-Identity */
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coSCD.set_non_null ();

  return iRetVal;
}

int app_rx_extract_gsu (avp *p_psoAVP, otl_value<SGSU> &p_coGSU)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 412: /* CC-Input-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_soServiceUnit.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_ccm (psoAVP, p_coGSU.v.m_soServiceUnit.m_coCCMoney);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coGSU.v.m_coTariffTimeChange = psoAVPHdr->avp_value->u32;
        break; /* Tariff-Time-Change */
      }
      break; /* Diameter */
      /* 3GPP */
      case 10415:
        switch (psoAVPHdr->avp_code) {
        break; /* 3GPP */
      }
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coGSU.set_non_null ();

  return iRetVal;
}

int app_rx_extract_usu (avp *p_psoAVP, otl_value<SUSU> &p_coUSU)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 412: /* CC-Input-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_soServiceUnit.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_ccm (psoAVP, p_coUSU.v.m_soServiceUnit.m_coCCMoney);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUSU.v.m_coTariffChangeUsage = psoAVPHdr->avp_value->u32;
        break; /* Tariff-Time-Change */
      }
      break; /* Diameter */
      /* 3GPP */
      case 10415:
        switch (psoAVPHdr->avp_code) {
        break; /* 3GPP */
      }
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coUSU.set_non_null ();

  return iRetVal;
}

int app_rx_extract_ccm (avp *p_psoAVP, otl_value<SCCMoney> &p_coCCMoney)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 425: /* Currency-Code */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coCCMoney.v.m_coCurrencyCode = psoAVPHdr->avp_value->u32;
        break; /* Currency-Code */
      case 445: /* Unit-Value */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_uv (psoAVP, p_coCCMoney.v.m_coUnitValue);
        break; /* Unit-Value */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coCCMoney.set_non_null ();

  return iRetVal;
}

int app_rx_extract_uv (avp *p_psoAVP, otl_value<SUnitValue> &p_coUV)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 429: /* Exponent */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUV.v.m_coExponent = psoAVPHdr->avp_value->i32;
        break; /* Exponent */
      case 447: /* Value-Digits */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_coUV.v.m_coValueDigits = psoAVPHdr->avp_value->i64;
        break; /* Value-Digits */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  p_coUV.set_non_null ();

  return iRetVal;
}

int app_rx_extract_pi (avp *p_psoAVP, SProxyInfo &p_soPI)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP, *psoAVP2;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
		/* получаем заголовок AVP */
    CHECK_FCT (fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 33: /* Proxy-State */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soPI.m_coProxyState.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soPI.m_coProxyState.set_non_null ();
        break; /* Proxy-State */
      case 280: /* Proxy-Host */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soPI.m_coProxyHost.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soPI.m_coProxyHost.set_non_null ();
        break; /* Proxy-Host */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  return iRetVal;
}

static int app_rx_store_req(SAAR &p_soAAR, SMsgDataForDB &p_soIPCANSess, otl_connect *p_pcoDBConn)
{
  if (NULL == p_pcoDBConn) {
    return EINVAL;
  }

  int iRetVal = 0;
  try {
    otl_nocommit_stream coStream;

    coStream.open(
      1,
      "insert into ps.sessionList_RX (session_id, framed_ip_address, ip_can_session_id) values (:session_id/*char[255]*/,:ip_addr/*char[16]*/,:ip_can_session/*char[255]*/)",
      *p_pcoDBConn);
    coStream
      << p_soAAR.m_coSessionId
      << p_soAAR.m_coFramedIPAddress
      << p_soIPCANSess.m_psoSessInfo->m_coSessionId;
  } catch (otl_exception &coExcept) {
    UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
    iRetVal = coExcept.code;
  }

  return iRetVal;
}
