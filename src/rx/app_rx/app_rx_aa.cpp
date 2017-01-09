#include "app_rx.h"
#include "app_rx_data_types.h"
#include "dict_rx/dict_rx.h"

#include "utils/dbpool/dbpool.h"

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
/* выборка данных из OC-Supported-Features */
static int app_rx_extract_ocsf (avp *p_psoAVP, otl_value<SOCSF> &p_coOCSF);
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
/* выбрка данных из Proxy-Info */
static int app_rx_extract_pi (avp *p_psoAVP, SProxyInfo &p_soPI);

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

int app_rx_aar_cb ( struct msg **pp_msg, struct avp *p_avp, struct session *p_session, void *p_opaque, enum disp_action *p_dispAction )
{
  /* suppress compiler warnings */
  p_avp = p_avp; p_session = p_session; p_opaque = p_opaque; p_dispAction = p_dispAction;

  msg *pMsg = *pp_msg;
  SAAR soAAR;
  msg *psoAns;
  avp *psoAVP;
  avp_value soAVPVal;

  /* ищем END_USER_SIP_URI */
  fd_msg_search_avp(pMsg, g_psoDictAVPSubscriptionId, &psoAVP);
  if (NULL != psoAVP) {
    LOG_D("Subscription-Id:");
    SSId soSI;
    if (0 == app_rx_extract_si(psoAVP, soSI)) {
      LOG_D("Subscription-Id:");
      soAAR.m_vectSubscriptionId.push_back(soSI);
    }
  } else {
    LOG_D("Subscription-Id not found");
  }

  CHECK_FCT_DO( app_rx_extract_aar (pMsg, soAAR), /* continue */ );

#ifdef DEBUG
  std::string strSIPURI, strFramedIPAddress;
  for (std::vector<SSId>::iterator iter = soAAR.m_vectSubscriptionId.begin(); iter != soAAR.m_vectSubscriptionId.end(); ++iter) {
    if (! iter->m_coSubscriptionIdType.is_null() && iter->m_coSubscriptionIdType.v == END_USER_SIP_URI) {
      if (! iter->m_coSubscriptionIdData.is_null()) {
        strSIPURI = iter->m_coSubscriptionIdData.v;
        break;
      }
    }
  }
  if (!soAAR.m_coFramedIPAddress.is_null()) {
    char mcFramedIPAddress[16];
    int iStrLen;
    iStrLen = snprintf (mcFramedIPAddress, sizeof(mcFramedIPAddress), "%u.%u.%u.%u",
      soAAR.m_coFramedIPAddress.v.m_uAddr.m_soAddr.b1, soAAR.m_coFramedIPAddress.v.m_uAddr.m_soAddr.b2, soAAR.m_coFramedIPAddress.v.m_uAddr.m_soAddr.b3, soAAR.m_coFramedIPAddress.v.m_uAddr.m_soAddr.b4);
    if (iStrLen > 0) {
      if (static_cast<size_t>(iStrLen) >= sizeof(mcFramedIPAddress)) {
        iStrLen = sizeof(mcFramedIPAddress) - 1;
      }
      mcFramedIPAddress[iStrLen] = '\0';
      strFramedIPAddress = mcFramedIPAddress;
    }
  }
  LOG_D( "END-USER-IMSI: %s; Framed-IP-Address: %s;", strSIPURI.c_str(), strFramedIPAddress.c_str() );
#endif

  /* Create answer header */
  CHECK_FCT_DO( fd_msg_new_answer_from_req(fd_g_config->cnf_dict, pp_msg, 0), goto cleanup_and_exit );

  psoAns = *pp_msg;

  /* Set Auth-Application-Id */
  do {
    CHECK_FCT_DO( fd_msg_avp_new (g_psoDictAVPAuthApplicationId, 0, &psoAVP), break );
    soAVPVal.u32 = APP_RX_ID;
    CHECK_FCT_DO( fd_msg_avp_setvalue(psoAVP, &soAVPVal), break );
    CHECK_FCT_DO( fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), break );
  } while (0);

  /* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
  CHECK_FCT_DO( fd_msg_rescode_set(psoAns, const_cast<char*>("DIAMETER_SUCCESS"), NULL, NULL, 1), /*continue*/ );

  /* если ответ сформирован отправляем его */
  if (psoAns) {
    CHECK_FCT_DO( fd_msg_send(pp_msg, NULL, NULL), /*continue*/ );
  }

  cleanup_and_exit:

  return 0;
}

int app_rx_extract_aar (msg_or_avp *p_psoMsg, SAAR &p_soAAR)
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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 8: /* Framed-IP-Address */
        memcpy(&p_soAAR.m_coFramedIPAddress.v.m_uAddr, psoAVPHdr->avp_value->os.data, sizeof(p_soAAR.m_coFramedIPAddress.v.m_uAddr) >= psoAVPHdr->avp_value->os.len ? psoAVPHdr->avp_value->os.len : sizeof(p_soAAR.m_coFramedIPAddress.v.m_uAddr));
        p_soAAR.m_coFramedIPAddress.set_non_null();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId );
        break; /* Framed-IP-Address */
      case 30: /* Called-Station-Id */
        p_soAAR.m_coCalledStationId.v.insert (0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coCalledStationId.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Called-Station-Id */
      case 97: /* Framed-Ipv6-Prefix */
        p_soAAR.m_coFramedIpv6Prefix.v.insert (0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coFramedIpv6Prefix.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Framed-Ipv6-Prefix */
      case 238: /* Origin-State-Id */
        p_soAAR.m_coOriginStateId = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Origin-State-Id */
      case 258: /* Auth-Application-Id */
        p_soAAR.m_coAuthApplicationId = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Auth-Application-Id */
      case 263: /* Session-Id */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coSessionId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coSessionId.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Session-Id */
      case 264: /* Origin-Host */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginHost.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Origin-Host */
      case 277: /* Auth-Session-State */
        p_soAAR.m_coAuthSessionState = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Auth-Session-State */
      case 282: /* Route-Record */
        {
          std::string strRouteRecord;
          strRouteRecord.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_vectRouteRecord.push_back (strRouteRecord);
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Route-Record */
      case 283: /* Destination-Realm */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationRealm.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Destination-Realm */
      case 284: /* Proxy-Info */
        {
          SProxyInfo soPI;
          app_rx_extract_pi (psoAVP, soPI);
          p_soAAR.m_vectProxyInfo.push_back (soPI);
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Proxy-Info */
      case 293: /* Destination-Host */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationHost.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Destination-Host */
      case 296: /* Origin-Realm */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginRealm.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Origin-Realm */
      case 443: /* Subscription-Id */
        {
          SSId soSI;
          if (0 == app_rx_extract_si(psoAVP, soSI)) {
            LOG_D("Subscription-Id:");
            p_soAAR.m_vectSubscriptionId.push_back(soSI);
          } else {
            LOG_D("Subscription-Id incorrect!!!");
          }
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Subscription-Id */
      case 621: /* OC-Supported-Features */
        app_rx_extract_ocsf (psoAVP, p_soAAR.m_coOCSupportedFeatures);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* OC-Supported-Features */
      default:
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 458: /* Reservation-Priority */
        p_soAAR.m_coReservationPriority = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coAFApplicationIdentifier.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* AF-Application-Identifier */
      case 513: /* Specific-Action */
        p_soAAR.m_vectSpecificAction.push_back (psoAVPHdr->avp_value->i32);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Specific-Action */
      case 517: /* Media-Component-Description */
        app_rx_extract_mcd (psoAVP, p_soAAR.m_vectMediaComponentDescription);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Media-Component-Description */
      case 523: /* SIP-Forking-Indication */
        p_soAAR.m_coSIPForkingIndication = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* SIP-Forking-Indication */
      case 525: /* Service-URN */
        p_soAAR.m_coServiceURN.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coServiceURN.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Service-URN */
      case 527: /* Service-Info-Status */
        p_soAAR.m_coServiceInfoStatus = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Service-Info-Status */
      case 528: /* MPS-Identifier */
        p_soAAR.m_coMPSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* MPS-Identifier */
      case 530: /* Sponsored-Connectivity-Data */
        app_rx_extract_scd (psoAVP, p_soAAR.m_coSponsoredConnectivityData);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Sponsored-Connectivity-Data */
      case 533: /* Rx-Request-Type */
        p_soAAR.m_coRxRequestType = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Rx-Request-Type */
      case 536: /* Required-Access-Info */
        p_soAAR.m_vectRequiredAccessInfo.push_back (psoAVPHdr->avp_value->i32);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Required-Access-Info */
      case 538: /* GCS-Identifier */
        p_soAAR.m_coGCSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coGCSIdentifier.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* GCS-Identifier */
      case 629: /* Supported-Features */
        {
          SSF soSF;
          app_rx_extract_sf (psoAVP, soSF);
          p_soAAR.m_vectSupportedFeatures.push_back (soSF);
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Supported-Features */
      case 537: /* IP-Domain-Id */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coIPDomainId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coIPDomainId.set_non_null ();
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* IP-Domain-Id */
      default:
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* 3GPP */
    default:
      LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 458: /* Reservation-Priority */
        soMCD.m_coReservationPriority = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        soMCD.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        soMCD.m_coAFApplicationIdentifier.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* AF-Application-Identifier */
      case 511: /* Flow-Status */
        soMCD.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Flow-Status */
      case 515: /* Max-Requested-Bandwidth-DL */
        soMCD.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        soMCD.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Max-Requested-Bandwidth-UL */
      case 518: /* Media-Component-Number */
        soMCD.m_coMediaComponentNumber = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Media-Component-Number */
      case 519: /* Media-Sub-Component */
        app_rx_extract_msc (psoAVP, soMCD.m_vectMediaSubComponent);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Media-Sub-Component */
      case 520: /* Media-Type */
        soMCD.m_coMediaType = psoAVPHdr->avp_value->i32;
        app_rx_get_enum_val (tVenId, psoAVPHdr->avp_code, psoAVPHdr->avp_value->i32, soMCD.m_coMediaTypeEnum);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Media-Type */
      case 521: /* RR-Bandwidth */
        soMCD.m_coRRBandwidth = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* RR-Bandwidth */
      case 522: /* RS-Bandwidth */
        soMCD.m_coRSBandwidth = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* RS-Bandwidth */
      case 524: /* Codec-Data */
        {
          std::string strCodecData;
          strCodecData.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          soMCD.m_vectCodecData.push_back (strCodecData);
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Codec-Data */
      case 534: /* Min-Requested-Bandwidth-DL */
        soMCD.m_coMinRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Min-Requested-Bandwidth-DL */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      case 535: /* Min-Requested-Bandwidth-UL */
        soMCD.m_coMinRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 507: /* Flow-Description */
        if (iInd < sizeof(soMSC.m_mcoFlowDescription)/sizeof(*soMSC.m_mcoFlowDescription)) {
          soMSC.m_mcoFlowDescription[iInd].v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          soMSC.m_mcoFlowDescription[iInd].set_non_null ();
          ++iInd;
        }
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Flow-Description */
      case 509: /* Flow-Number */
        soMSC.m_coFlowNumber = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Flow-Number */
      case 511: /* Flow-Status */
        soMSC.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Flow-Status */
      case 512: /* Flow-Usage */
        soMSC.m_coFlowUsage = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Flow-Usage */
      case 515: /* Max-Requested-Bandwidth-DL */
        soMSC.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        soMSC.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Max-Requested-Bandwidth-UL */
      case 529: /* AF-Signalling-Protocol */
        soMSC.m_coAFSignallingProtocol = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* AF-Signalling-Protocol */
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && psoAVP2 != NULL);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
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
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal(psoAVP, MSG_BRW_NEXT, (msg_or_avp **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 622: /* OC-Feature-Vector */
        p_coOCSF.v.m_coOCFeatureVector = psoAVPHdr->avp_value->u64;
        p_coOCSF.v.m_coOCFeatureVector.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* OC-Feature-Vector */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 266: /* Vendor-Id */
        p_soSF.m_coVendorId = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Vendor-Id */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 629: /* Feature-List-ID */
        p_soSF.m_coFeatureListID = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Feature-List-ID */
      case 630: /* Feature-List */
        p_soSF.m_coFeatureList = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Feature-List */
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 431: /* Granted-Service-Unit */
        app_rx_extract_gsu (psoAVP, p_coSCD.v.m_coGrantedServiceUnit);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Granted-Service-Unit */
      case 446: /* Used-Service-Unit */
        app_rx_extract_usu (psoAVP, p_coSCD.v.m_coUsedServiceUnit);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Used-Service-Unit */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      case 531: /* Sponsor-Identity */
        p_coSCD.v.m_coSponsorIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coSponsorIdentity.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Sponsor-Identity */
      case 532: /* Application-Service-Provider-Identity */
        p_coSCD.v.m_coApplicationServiceProviderIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coApplicationServiceProviderIdentity.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Application-Service-Provider-Identity */
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 412: /* CC-Input-Octets */
        p_coGSU.v.m_soServiceUnit.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        app_rx_extract_ccm (psoAVP, p_coGSU.v.m_soServiceUnit.m_coCCMoney);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        p_coGSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        p_coGSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        p_coGSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        p_coGSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
        p_coGSU.v.m_coTariffTimeChange = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Tariff-Time-Change */
      }
      break; /* Diameter */
      /* 3GPP */
      case 10415:
        switch (psoAVPHdr->avp_code) {
        break; /* 3GPP */
      }
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 412: /* CC-Input-Octets */
        p_coUSU.v.m_soServiceUnit.m_coCCInputOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        app_rx_extract_ccm (psoAVP, p_coUSU.v.m_soServiceUnit.m_coCCMoney);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        p_coUSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        p_coUSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        p_coUSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        p_coUSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
        p_coUSU.v.m_coTariffChangeUsage = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Tariff-Time-Change */
      }
      break; /* Diameter */
      /* 3GPP */
      case 10415:
        switch (psoAVPHdr->avp_code) {
        break; /* 3GPP */
      }
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 425: /* Currency-Code */
        p_coCCMoney.v.m_coCurrencyCode = psoAVPHdr->avp_value->u32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Currency-Code */
      case 445: /* Unit-Value */
        app_rx_extract_uv (psoAVP, p_coCCMoney.v.m_coUnitValue);
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Unit-Value */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 429: /* Exponent */
        p_coUV.v.m_coExponent = psoAVPHdr->avp_value->i32;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Exponent */
      case 447: /* Value-Digits */
        p_coUV.v.m_coValueDigits = psoAVPHdr->avp_value->i64;
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Value-Digits */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

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
    if (NULL != psoAVPHdr->avp_value) {
    } else {
      continue;
    }
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    /* Diameter */
    case 0:
      switch (psoAVPHdr->avp_code) {
      case 33: /* Proxy-State */
        p_soPI.m_coProxyState.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soPI.m_coProxyState.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Proxy-State */
      case 280: /* Proxy-Host */
        p_soPI.m_coProxyHost.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soPI.m_coProxyHost.set_non_null ();
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break; /* Proxy-Host */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  return iRetVal;
}
