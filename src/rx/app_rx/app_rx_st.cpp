#include "app_rx.h"
#include "app_rx_data_types.h"

#include "dict_rx/dict_rx.h"
#include "../app_pcrf/app_pcrf_header.h"

static disp_hdl * app_rx_str_cb_hdl = NULL;
static int app_rx_str_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *);

/* функция выполняет выборку данных из запроса */
static int app_rx_extract_str(msg *p_pMsg, SSTR &soSTR);
/* функция деинсталлирует назначенный QoS */
static int app_rx_uninstall_QoS(SSessionInfo &soIPCANSessInfo, otl_connect &p_coDBConn);
/* закрытие сессии в БД */
static int pcrf_terminate_rx_session(otl_connect *p_pcoDBConn, SSTR &soSTR);

int app_rx_register_str_cb()
{
  struct disp_when data;

  memset(&data, 0, sizeof(data));
  data.app = g_psoDictAppRx;
  data.command = g_psoDictCmdAAR;

  /* Now specific handler for AAR */
  CHECK_FCT(fd_disp_register(app_rx_str_cb, DISP_HOW_CC, &data, NULL, &app_rx_str_cb_hdl));

  return 0;
}

int app_rx_str_cb ( struct msg **p_ppMsg, struct avp *p_avp, struct session *p_session, void *p_opaque, enum disp_action *p_dispAction )
{
  /* suppress compiler warnings */
  p_avp = p_avp; p_session = p_session; p_opaque = p_opaque; p_dispAction = p_dispAction;

  msg *pMsg = *p_ppMsg;
  SSTR soSTR;
  msg *psoAns;
  avp *psoAVP;
  avp_value soAVPVal;
  int iResultCode = 2001; /*  DIAMETER_SUCCESS */
  otl_connect *pcoDBConn = NULL;
  SSessionInfo soIPCANSessInfo;

  CHECK_FCT_DO(app_rx_extract_str(pMsg, soSTR), /* continue */);

  /* запрашиваем подключение к БД */
  CHECK_FCT_DO(pcrf_db_pool_get(reinterpret_cast<void**>(&pcoDBConn), __FUNCTION__, NULL, 1), goto answer);

  /* Create answer header */
  CHECK_FCT_DO(fd_msg_new_answer_from_req(fd_g_config->cnf_dict, p_ppMsg, 0), goto cleanup_and_exit);

  /* обработка запроса */
  CHECK_FCT_DO(pcrf_terminate_rx_session(pcoDBConn, soSTR), );

  /* посылаем запрос на инсталляцию нового правила */
  CHECK_FCT_DO(app_rx_uninstall_QoS(soIPCANSessInfo, *pcoDBConn), iResultCode = 5063; goto answer);

  psoAns = *p_ppMsg;

  /* Set Auth-Application-Id */
  do {
    CHECK_FCT_DO(fd_msg_avp_new(g_psoDictAVPAuthApplicationId, 0, &psoAVP), break);
    soAVPVal.u32 = APP_RX_ID;
    CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPVal), break);
    CHECK_FCT_DO(fd_msg_avp_add(psoAns, MSG_BRW_LAST_CHILD, psoAVP), break);
  } while (0);

answer:
  /* Set the Origin-Host, Origin-Realm, Result-Code AVPs */
  switch (iResultCode) {
  case ER_DIAMETER_SUCCESS:  /*  DIAMETER_SUCCESS */
    CHECK_FCT_DO(fd_msg_rescode_set(psoAns, const_cast<char*>("DIAMETER_SUCCESS"), NULL, NULL, 1), /*continue*/);
    break;    /*  DIAMETER_SUCCESS */
  case 5063:  /* REQUESTED_SERVICE_NOT_AUTHORIZED */
    CHECK_FCT_DO(fd_msg_rescode_set(psoAns, const_cast<char*>("REQUESTED_SERVICE_NOT_AUTHORIZED"), NULL, NULL, 1), /*continue*/);
    break;    /* REQUESTED_SERVICE_NOT_AUTHORIZED */
  case 5065:  /*IP-CAN_SESSION_NOT_AVAILABLE*/
    CHECK_FCT_DO(fd_msg_rescode_set(psoAns, const_cast<char*>("IP-CAN_SESSION_NOT_AVAILABLE"), NULL, NULL, 1), /*continue*/);
    break;    /*IP-CAN_SESSION_NOT_AVAILABLE*/
  }

  /* если ответ сформирован отправляем его */
  if (psoAns) {
    CHECK_FCT_DO(fd_msg_send(p_ppMsg, NULL, NULL), /*continue*/);
  }

cleanup_and_exit:

  return 0;
}

static int app_rx_extract_str(msg *p_psoMsg, SSTR &p_soSTR)
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
  CHECK_FCT(fd_msg_browse_internal(p_psoMsg, MSG_BRW_FIRST_CHILD, (void **)&psoAVP2, NULL));
  do {
    psoAVP = psoAVP2;
    /* получаем заголовок AVP */
    CHECK_FCT(fd_msg_avp_hdr(psoAVP, &psoAVPHdr));
    tVenId = AVP_FLAG_VENDOR & psoAVPHdr->avp_flags ? psoAVPHdr->avp_vendor : 0;
    switch (tVenId) {
    case 0: /* Diameter */
      switch (psoAVPHdr->avp_code) {
      case 25: /* Class */
        {
          std::string strClass;
          strClass.insert(0, reinterpret_cast<char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_vectClass.push_back(strClass);
        }
        break; /* Class */
      case 238: /* Origin-State-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soSTR.m_coOriginStateId = psoAVPHdr->avp_value->u32;
        break; /* Origin-State-Id */
      case 258: /* Auth-Application-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        p_soSTR.m_coAuthApplicationId = psoAVPHdr->avp_value->u32;
        break; /* Auth-Application-Id */
      case 263: /* Session-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soSTR.m_coSessionId.v.insert(0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_coSessionId.set_non_null();
        }
        break; /* Session-Id */
      case 264: /* Origin-Host */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soSTR.m_coOriginHost.v.insert(0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_coOriginHost.set_non_null();
        }
        break; /* Origin-Host */
      case 282: /* Route-Record */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          std::string strRouteRecord;
          strRouteRecord.insert(0, reinterpret_cast<const char*> (psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_vectRouteRecord.push_back(strRouteRecord);
        }
        break; /* Route-Record */
      case 283: /* Destination-Realm */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soSTR.m_coDestRealm.v.insert(0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_coDestRealm.set_non_null();
          }
        break; /* Destination-Realm */
      case 284: /* Proxy-Info */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        {
          SProxyInfo soPI;
          app_rx_extract_pi(psoAVP, soPI);
          p_soSTR.m_vectProxyInfo.push_back(soPI);
        }
        break; /* Proxy-Info */
      case 293: /* Destination-Host */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soSTR.m_coDestHost.v.insert(0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_coDestHost.set_non_null();
        }
        break; /* Destination-Host */
      case 295: /* Termination-Cause-Id */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_get_enum_val(tVenId, psoAVPHdr->avp_code, psoAVPHdr->avp_value->i32, p_soSTR.m_coTermCause);
        break;
      case 296: /* Origin-Realm */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        if (NULL != psoAVPHdr->avp_value->os.data) {
          p_soSTR.m_coOriginRealm.v.insert(0, reinterpret_cast<const char*>(psoAVPHdr->avp_value->os.data), psoAVPHdr->avp_value->os.len);
          p_soSTR.m_coOriginRealm.set_non_null();
        }
        break; /* Origin-Realm */
      case 621: /* OC-Supported-Features */
        LOG_D("AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        app_rx_extract_ocsf(psoAVP, p_soSTR.m_soOCSuppFeat);
        break; /* OC-Supported-Features */
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* Diameter */
    case 10415: /* 3GPP */
      switch (psoAVPHdr->avp_code) {
      default:
        LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
        break;
      }
      break; /* 3GPP */
    default:
      LOG_D("unoperated AVP code: %u; Vendor Id: %u", psoAVPHdr->avp_code, tVenId);
      break;
    }
  } while (0 == fd_msg_browse_internal((void *)psoAVP, MSG_BRW_NEXT, (void **)&psoAVP2, NULL) && NULL != psoAVP2);

  return iRetVal;
}

int app_rx_uninstall_QoS(SSessionInfo &soIPCANSessInfo, otl_connect &p_coDBConn)
{
  /* TODO */
  return 0;
}

static int pcrf_terminate_rx_session(otl_connect *p_pcoDBConn, SSTR &soSTR)
{
  /* TODO */
  return 0;
}
