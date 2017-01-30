#include <freeDiameter/extension.h>

#include "dict_rx/dict_rx.h"

struct dict_object *g_psoDictVend3GPP;
struct dict_object *g_psoDictAppRx;

struct dict_object *g_psoDictCmdAAR;
struct dict_object *g_psoDictCmdSTR;

/* Auth-Application-Id */
struct dict_object *g_psoDictAVPAuthApplicationId;

/* Subscription-Id */
struct dict_object *g_psoDictAVPSubscriptionId;

/* Origin-Host */
struct dict_object *g_psoDictAVPOriginHost;
/* Origin-Realm */
struct dict_object *g_psoDictAVPOriginRealm;
/* Experimental-Result */
struct dict_object *g_psoDictAVPExperimentalResult;
/* Experimental-Result-Code */
struct dict_object *g_psoDictAVPExperimentalResultCode;
/* Vendor-Id */
struct dict_object *g_psoDictAVPVendorId;
/* IP-CAN-Type */
struct dict_object *g_psoDictAVPIPCANType;

int app_rx_dict_cache_init ()
{
  int iRetVal = 0;

  /* load vendor defination */
  {
  	vendor_id_t vend_id = VENDOR_3GPP_ID;
	  CHECK_FCT( fd_dict_search (fd_g_config->cnf_dict, DICT_VENDOR, VENDOR_BY_ID, &vend_id, &g_psoDictVend3GPP, ENOENT) );
  }

  /* загрузка объекта словаря - приложение Rx */
  {
    application_id_t app_id = APP_RX_ID;
	  CHECK_FCT( fd_dict_search (fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID, &app_id, &g_psoDictAppRx, ENOENT) );
  }

  /* загруза объкта словаря - команда AAR */
  {
    command_code_t cmd_code = AAR_CMD_ID;
	  CHECK_FCT( fd_dict_search (fd_g_config->cnf_dict, DICT_COMMAND, CMD_BY_CODE_R, &cmd_code, &g_psoDictCmdAAR, ENOENT) );
  }

  /* загруза объкта словаря - команда STR */
  {
    command_code_t cmd_code = STR_CMD_ID;
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_COMMAND, CMD_BY_CODE_R, &cmd_code, &g_psoDictCmdSTR, ENOENT));
  }

  /* Auth-Application-Id */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Auth-Application-Id" };
    CHECK_FCT( fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPAuthApplicationId, ENOENT) );
  }

  /* Subscription-Id */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Subscription-Id" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPSubscriptionId, ENOENT));
  }

  /* Origin-Host */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Origin-Host" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPOriginHost, ENOENT));
  }

  /* Origin-Realm */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Origin-Realm" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPOriginRealm, ENOENT));
  }

  /* Experimental-Result */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Experimental-Result" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPExperimentalResult, ENOENT));
  }

  /* Experimental-Result-Code */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Experimental-Result-Code" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPExperimentalResultCode, ENOENT));
  }

  /* Vendor-Id */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_DIAM_ID, 0, "Vendor-Id" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPVendorId, ENOENT));
  }

  /* IP-CAN-Type */
  {
    struct dict_avp_request soAVPRequest = { VENDOR_3GPP_ID, 0, "IP-CAN-Type" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPIPCANType, ENOENT));
  }

  return iRetVal;
}
