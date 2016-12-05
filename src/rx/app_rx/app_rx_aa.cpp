#include "app_rx.h"
#include "app_rx_data_types.h"

#include "utils/dbpool/dbpool.h"

static disp_hdl * app_rx_aar_cb_hdl = NULL;
static int app_rx_aar_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *);

/* выборка данных из AAR */
static int app_rx_extract_aar (msg_or_avp *p_psoMsg, SAAR &p_soAAR);
/* выборка данных из Media-Component-Description */
static int app_rx_extract_mcd (avp *p_psoAVP, std::vector<SMCD> &p_vectMCD);
static int app_rx_extract_msc (avp *p_psoAVP, std::vector<SMSC> &p_vectMSC);
/* выборка из Subscription-Id */
static int app_rx_extract_si (avp *p_psoAVP, SSubscriptionId &p_soSI);
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

extern "C"
int app_rx_register_aar_cb ()
{
  struct disp_when data;

  memset (&data, 0, sizeof(data));
	data.app = g_psoDictAppRx;
	data.command = g_psoDictCmdAAR;

  /* Now specific handler for CCR */
	CHECK_FCT (fd_disp_register (app_rx_aar_cb, DISP_HOW_CC, &data, NULL, &app_rx_aar_cb_hdl));

  return 0;
}

int app_rx_aar_cb ( struct msg **pp_msg, struct avp *p_avp, struct session *p_session, void *p_opaque, enum disp_action *p_dispAction)
{
  msg *pMsg = *pp_msg;
  SAAR soAAR;

  CHECK_FCT_DO (app_rx_extract_aar (pMsg, soAAR), /* continue */);

  return 0;
}

int app_rx_extract_aar (msg_or_avp *p_psoMsg, SAAR &p_soAAR)
{
  int iRetVal = 0;

  /* проверка параметров */
	if (NULL == p_psoMsg) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoMsg, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        p_soAAR.m_coFramedIPAddress.v.m_uAddr.m_uiAddr = psoAVPHdr->avp_value->u32;
        break; /* Framed-IP-Address */
      case 30: /* Called-Station-Id */
        p_soAAR.m_coCalledStationId.v.insert (0, psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coCalledStationId.set_non_null ();
        break; /* Called-Station-Id */
      case 97: /* Framed-Ipv6-Prefix */
        p_soAAR.m_coFramedIpv6Prefix.v.insert (0, psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coFramedIpv6Prefix.set_non_null ();
        break; /* Framed-Ipv6-Prefix */
      case 238: /* Origin-State-Id */
        p_soAAR.m_coOriginStateId = psoAVPHdr->avp_value->u32;
        break; /* Origin-State-Id */
      case 258: /* Auth-Application-Id */
        p_soAAR.m_coAuthApplicationId = psoAVPHdr->avp_value->u32;
        break; /* Auth-Application-Id */
      case 263: /* Session-Id */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coSessionId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coSessionId.set_non_null ();
        }
        break; /* Session-Id */
      case 264: /* Origin-Host */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginHost.set_non_null ();
        }
        break; /* Origin-Host */
      case 277: /* Auth-Session-State */
        p_soAAR.m_coAuthSessionState = psoAVPHdr->avp_value->i32;
        break; /* Auth-Session-State */
      case 282: /* Route-Record */
        {
          std::string strRouteRecord;
          strRouteRecord.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_vectRouteRecord.push_back (strRouteRecord);
        }
        break; /* Route-Record */
      case 283: /* Destination-Realm */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationRealm.set_non_null ();
        }
        break; /* Destination-Realm */
      case 284: /* Proxy-Info */
        {
          SProxyInfo soPI;
          app_rx_extract_pi (psoAVP, soPI);
          p_soAAR.m_vectProxyInfo.push_back (soPI);
        }
        break; /* Proxy-Info */
      case 293: /* Destination-Host */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coDestinationHost.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coDestinationHost.set_non_null ();
        }
        break; /* Destination-Host */
      case 296: /* Origin-Realm */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coOriginRealm.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coOriginRealm.set_non_null ();
        }
        break; /* Origin-Realm */
      case 621: /* OC-Supported-Features */
        app_rx_extract_ocsf (psoAVP, p_soAAR.m_coOCSupportedFeatures);
        break; /* OC-Supported-Features */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 443: /* Subscription-Id */
        {
          SSubscriptionId soSI;
          app_rx_extract_si (psoAVP, soSI);
          p_soAAR.m_vectSubscriptionId.push_back (soSI);
        }
        break; /* Subscription-Id */
      case 458: /* Reservation-Priority */
        p_soAAR.m_coReservationPriority = psoAVPHdr->avp_value->i32;
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coAFApplicationIdentifier.set_non_null ();
        }
        break; /* AF-Application-Identifier */
      case 513: /* Specific-Action */
        p_soAAR.m_vectSpecificAction.push_back (psoAVPHdr->avp_value->i32);
        break; /* Specific-Action */
      case 517: /* Media-Component-Description */
        app_rx_extract_mcd (psoAVP, p_soAAR.m_vectMediaComponentDescription);
        break; /* Media-Component-Description */
      case 523: /* SIP-Forking-Indication */
        p_soAAR.m_coSIPForkingIndication = psoAVPHdr->avp_value->i32;
        break; /* SIP-Forking-Indication */
      case 525: /* Service-URN */
        p_soAAR.m_coServiceURN.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coServiceURN.set_non_null ();
        break; /* Service-URN */
      case 527: /* Service-Info-Status */
        p_soAAR.m_coServiceInfoStatus = psoAVPHdr->avp_value->i32;
        break; /* Service-Info-Status */
      case 528: /* MPS-Identifier */
        p_soAAR.m_coMPSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        break; /* MPS-Identifier */
      case 530: /* Sponsored-Connectivity-Data */
        app_rx_extract_scd ();
        break; /* Sponsored-Connectivity-Data */
      case 533: /* Rx-Request-Type */
        p_soAAR.m_coRxRequestType = psoAVPHdr->avp_value->i32;
        break; /* Rx-Request-Type */
      case 536: /* Required-Access-Info */
        p_soAAR.m_vectRequiredAccessInfo.push_back (psoAVPHdr->avp_value->i32);
        break; /* Required-Access-Info */
      case 538: /* GCS-Identifier */
        p_soAAR.m_coGCSIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soAAR.m_coGCSIdentifier.set_non_null ();
        break; /* GCS-Identifier */
      case 629: /* Supported-Features */
        {
          SSF soSF;
          app_rx_extract_sf (psoAVP, soSF);
          p_soAAR.m_vectSupportedFeatures.push_back (soSF);
        }
        break; /* Supported-Features */
      case 537: /* IP-Domain-Id */
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soAAR.m_coIPDomainId.v.insert (0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soAAR.m_coIPDomainId.set_non_null ();
        }
        break; /* IP-Domain-Id */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

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

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Reservation-Priority */
      case 504: /* AF-Application-Identifier */
        soMCD.m_coAFApplicationIdentifier.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        soMCD.m_coAFApplicationIdentifier.set_non_null ();
        break; /* AF-Application-Identifier */
      case 511: /* Flow-Status */
        soMCD.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        break; /* Flow-Status */
      case 515: /* Max-Requested-Bandwidth-DL */
        soMCD.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        soMCD.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-UL */
      case 518: /* Media-Component-Number */
        soMCD.m_coMediaComponentNumber = psoAVPHdr->avp_value->u32;
        break; /* Media-Component-Number */
      case 519: /* Media-Sub-Component */
        app_rx_extract_msc (psoAVP, soMCD.m_vectMediaSubComponent);
        break; /* Media-Sub-Component */
      case 520: /* Media-Type */
        soMCD.m_coMediaType = psoAVPHdr->avp_value->i32;
        app_rx_get_enum_val (tVenId, psoAVPHdr->avp_code, psoAVPHdr->avp_value->i32, soMCD.m_coMediaTypeEnun);
        break; /* Media-Type */
      case 521: /* RR-Bandwidth */
        soMCD.m_coRRBandwidth = psoAVPHdr->avp_value->u32;
        break; /* RR-Bandwidth */
      case 522: /* RS-Bandwidth */
        soMCD.m_coRSBandwidth = psoAVPHdr->avp_value->u32;
        break; /* RS-Bandwidth */
      case 524: /* Codec-Data */
        {
          std::string strCodecData;
          strCodecData.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          soMCD.m_vectCodecData.push_back (strCodecData);
        }
        break; /* Codec-Data */
      case 534: /* Min-Requested-Bandwidth-DL */
        soMCD.m_coMinRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Min-Requested-Bandwidth-DL */
      case 535: /* Min-Requested-Bandwidth-UL */
        soMCD.m_coMinRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Min-Requested-Bandwidth-UL */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_vectMCD.push_back (soMCD);

  return iRetVal;
}

int app_rx_extract_msc (avp *p_psoAVP, std::vector<SMSC> &p_vectMSC)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Flow-Description */
      case 509: /* Flow-Number */
        soMSC.m_coFlowNumber = psoAVPHdr->avp_value->u32;
        break; /* Flow-Number */
      case 511: /* Flow-Status */
        soMSC.m_coFlowStatus = psoAVPHdr->avp_value->i32;
        break; /* Flow-Status */
      case 512: /* Flow-Usage */
        soMSC.m_coFlowUsage = psoAVPHdr->avp_value->i32;
        break; /* Flow-Usage */
      case 515: /* Max-Requested-Bandwidth-DL */
        soMSC.m_coMaxRequestedBandwidthDL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-DL */
      case 516: /* Max-Requested-Bandwidth-UL */
        soMSC.m_coMaxRequestedBandwidthUL = psoAVPHdr->avp_value->u32;
        break; /* Max-Requested-Bandwidth-UL */
      case 529: /* AF-Signalling-Protocol */
        soMSC.m_coAFSignallingProtocol = psoAVPHdr->avp_value->i32;
        break; /* AF-Signalling-Protocol */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_vectMSC.push_back (soMSC);

  return iRetVal;
}

static int app_rx_extract_si (avp *p_psoAVP, SSubscriptionId &p_soSI)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
      case 444: /* Subscription-Id-Data */
        p_soSI.m_coSubscriptionIdData.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_soSI.m_coSubscriptionIdData.set_non_null ();
        break; /* Subscription-Id-Data */
      case 450: /* Subscription-Id-Type */
        p_soSI.m_coSubscriptionIdType = psoAVPHdr->avp_value->i32;
        break; /* Subscription-Id-Type */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_vectMSC.push_back (soMSC);

  return iRetVal;
}

int app_rx_extract_ocsf (avp *p_psoAVP, otl_value<SOCSF> &p_coOCSF)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* OC-Feature-Vector */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coOCSF.set_non_null ();

  return iRetVal;
}

static int app_rx_extract_sf (avp *p_psoAVP, SSF &p_soSF)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Vendor-Id */
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      case 629: /* Feature-List-ID */
        p_soSF.m_coFeatureListID = psoAVPHdr->avp_value->u32;
        break; /* Feature-List-ID */
      case 630: /* Feature-List */
        p_soSF.m_coFeatureList = psoAVPHdr->avp_value->u32;
        break; /* Feature-List */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_vectMSC.push_back (soMSC);

  return iRetVal;
}

static int app_rx_extract_scd (avp *p_psoAVP, otl_value<SSCD> &p_coSCD)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Granted-Service-Unit */
      case 446: /* Used-Service-Unit */
        app_rx_extract_usu (psoAVP, p_coSCD.v.m_coUsedServiceUnit);
        break; /* Used-Service-Unit */
      }
      break; /* Diameter */
    /* 3GPP */
    case 10415:
      switch (psoAVPHdr->avp_code) {
      case 531: /* Sponsor-Identity */
        p_coSCD.v.m_coSponsorIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coSponsorIdentity.set_non_null ();
        break; /* Sponsor-Identity */
      case 532: /* Application-Service-Provider-Identity */
        p_coSCD.v.m_coApplicationServiceProviderIdentity.v.insert (0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
        p_coSCD.v.m_coApplicationServiceProviderIdentity.set_non_null ();
        break; /* Application-Service-Provider-Identity */
      }
      break; /* 3GPP */
    }
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coSCD.set_non_null ();

  return iRetVal;
}

int app_rx_extract_gsu (avp *p_psoAVP, otl_value<SGSU> &p_coGSU)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        app_rx_extract_ccm (psoAVP, p_coGSU.v.m_soServiceUnit.m_coCCMoney);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        p_coGSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        p_coGSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        p_coGSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        p_coGSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
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
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coGSU.set_non_null ();

  return iRetVal;
}

int app_rx_extract_usu (avp *p_psoAVP, otl_value<SUSU> &p_coUSU)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* CC-Input-Octets */
      case 413: /* CC-Money */
        app_rx_extract_ccm (psoAVP, p_coUSU.v.m_soServiceUnit.m_coCCMoney);
        break; /* CC-Money */
      case 414: /* CC-Output-Octets */
        p_coUSU.v.m_soServiceUnit.m_coCCOutputOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Output-Octets */
      case 417: /* CC-Service-Specific-Units */
        p_coUSU.v.m_soServiceUnit.m_coCCServiceSpecificUnits = psoAVPHdr->avp_value->u64;
        break; /* CC-Service-Specific-Units */
      case 420: /* CC-Time */
        p_coUSU.v.m_soServiceUnit.m_coCCTime = psoAVPHdr->avp_value->u32;
        break; /* CC-Time */
      case 421: /* CC-Total-Octets */
        p_coUSU.v.m_soServiceUnit.m_coCCTotalOctets = psoAVPHdr->avp_value->u64;
        break; /* CC-Total-Octets */
      case 451: /* Tariff-Time-Change */
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
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coUSU.set_non_null ();

  return iRetVal;
}

int app_rx_extract_ccm (avp *p_psoAVP, otl_value<SCCMoney> &p_coCCMoney)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Currency-Code */
      case 445: /* Unit-Value */
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
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coCCMoney.set_non_null ();

  return iRetVal;
}

int app_rx_extract_uv (avp *p_psoAVP, otl_value<SUnitValue> &p_coUV)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Exponent */
      case 447: /* Value-Digits */
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
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coUV.set_non_null ();

  return iRetVal;
}

int app_rx_extract_pi (avp *p_psoAVP, SProxyInfo &p_soPI)
{
  int iRetVal = 0;
  int iInd = 0;
  SMSC soMSC;

  /* проверка параметров */
	if (NULL == p_psoAVP) {
		return EINVAL;
	}

  struct avp *psoAVP;
	struct avp_hdr *psoAVPHdr;
	vendor_id_t tVenId;

	/* ищем первую AVP */
  CHECK_FCT (fd_msg_browse_internal (p_psoAVP, MSG_BRW_FIRST_CHILD, (void **)&psoAVP, NULL));
  do {
		/* получаем заголовок AVP */
		if (NULL == psoAVP) {
			break;
    }
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
        break; /* Proxy-State */
      case 280: /* Proxy-Host */
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
  } while (CHECK_FCT (fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP, NULL)));

  p_coUV.set_non_null ();

  return iRetVal;
}
