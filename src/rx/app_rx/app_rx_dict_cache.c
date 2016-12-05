#include <freeDiameter/extension.h>

#define VENDOR_3GPP_ID  10415
#define APP_RX_ID       16777236

#define AAR_CMD_ID      265

struct dict_object *g_psoDictVend3GPP = NULL;
struct dict_object *g_psoDictAppRx = NULL;

struct dict_object *g_psoDictCmdAAR = NULL;

int app_rx_dict_cache_init ()
{
  int iRetVal = 0;

  /* load vendor defination */
  {
  	vendor_id_t vend_id = VENDOR_3GPP_ID;
	  CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_VENDOR, VENDOR_BY_ID, &vend_id, &g_psoDictVend3GPP, ENOENT));
  }

  /* загрузка объекта словаря - приложение Rx */
  {
    application_id_t app_id = APP_RX_ID;
	  CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID, &app_id, &g_psoDictAppRx, ENOENT));
  }

  /* загруза объкта словаря - команда AAR */
  {
    command_code_t cmd_code = AAR_CMD_ID;
	  CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_COMMAND, CMD_BY_CODE_R, &cmd_code, &g_psoDictCmdAAR, ENOENT));
  }

  return iRetVal;
}
