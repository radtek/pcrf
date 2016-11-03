#include <freeDiameter/extension.h>

#define VENDOR_3GPP_ID  10415
#define APP_RX_ID       16777236

struct dict_object *g_psoDictVend3GPP = NULL;
struct dict_object *g_psoDictAppRx = NULL;

int app_rx_dict_cache_init ()
{
  int iRetVal = 0;

  /* load vendor defination */
  {
  	vendor_id_t tVendId = VENDOR_3GPP_ID;
	  CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_VENDOR, VENDOR_BY_ID, &tVendId, &g_psoDictVend3GPP, ENOENT));
  }

  /* загрузка объекта словаря - приложение Rx */
  {
    application_id_t tAppId = APP_RX_ID;
	  CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_APPLICATION, APPLICATION_BY_ID, &tAppId, &g_psoDictAppRx, ENOENT));
  }

  return iRetVal;
}
