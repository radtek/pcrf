#include <freeDiameter/extension.h>

#include "dict_rx/dict_rx.h"

struct dict_object *g_psoDictVend3GPP;
struct dict_object *g_psoDictAppRx;

struct dict_object *g_psoDictCmdAAR;

/* Auth-Application-Id */
struct dict_object *g_psoDictAVPAuthApplicationId;

/* g_psoDictAVPSubscriptionId */
struct dict_object *g_psoDictAVPSubscriptionId;

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

  /* Auth-Application-Id */
  {
    struct dict_avp_request soAVPRequest = { 0, 0, "Auth-Application-Id" };
    CHECK_FCT( fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPAuthApplicationId, ENOENT) );
  }

  /* Subscription-Id */
  {
    struct dict_avp_request soAVPRequest = { 0, 0, "Subscription-Id" };
    CHECK_FCT(fd_dict_search(fd_g_config->cnf_dict, DICT_AVP, AVP_BY_NAME_AND_VENDOR, &soAVPRequest, &g_psoDictAVPSubscriptionId, ENOENT));
  }

  return iRetVal;
}
