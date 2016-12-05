#include "app_rx.h"

int app_rx_entry (char *conffile)
{
  /* suppress compiler warning */
  conffile = conffile;

  TRACE_DEBUG (FULL, "Initializing rx application");

  /* кеширование объектов словаря */
  CHECK_FCT (app_rx_dict_cache_init());

  CHECK_FCT (app_rx_register_aar_cb());

  /* Advertise the support for the Gx application in the peer */
  CHECK_FCT (fd_disp_app_support (g_psoDictAppRx, g_psoDictVend3GPP, 1, 0));

	return 0;
}

EXTENSION_ENTRY ("app_rx", app_rx_entry);
