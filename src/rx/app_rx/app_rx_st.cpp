#include "app_rx.h"
#include "app_rx_data_types.h"

#include "dict_rx/dict_rx.h"
#include "../app_pcrf/app_pcrf_header.h"

static disp_hdl * app_rx_str_cb_hdl = NULL;
static int app_rx_str_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *);

/* функция выполняет выборку данных из запроса */
int app_rx_extract_str(msg *p_pMsg, SSTR &soSTR);
/* функция деинсталлирует назначенный QoS */
int app_rx_uninstall_QoS(SSessionInfo &soIPCANSessInfo, otl_connect &p_coDBConn);

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
  /* поиск активной Интернет-сессии */
  CHECK_FCT_DO(pcrf_server_find_IPCAN_session_byframedip(*pcoDBConn, soSTR.m_coFramedIPAddress, soIPCANSessInfo, NULL), iResultCode = 5065; goto answer);

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

int app_rx_extract_str(msg *p_pMsg, SSTR &soSTR)
{
  return 0;
}

int app_rx_uninstall_QoS(SSessionInfo &soIPCANSessInfo, otl_connect &p_coDBConn)
{
  return 0;
}
