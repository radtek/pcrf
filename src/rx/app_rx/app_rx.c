#include "app_rx.h"

int app_rx_entry (char *conffile)
{
//	struct disp_when data;

  /* suppress compiler warning */
  conffile = conffile;

	TRACE_DEBUG (FULL, "Initializing rx application");

  /* кеширование объектов словаря */
  CHECK_FCT (app_rx_dict_cache_init());

//	memset (&data, 0, sizeof(data));
//	data.app = g_psoDictApp;
//	data.command = g_psoDictCCR;

	/* Now specific handler for CCR */
//	CHECK_FCT (fd_disp_register (app_pcrf_ccr_cb, DISP_HOW_CC, &data, NULL, &app_pcrf_hdl_ccr));

	/* Advertise the support for the Gx application in the peer */
	CHECK_FCT (fd_disp_app_support (g_psoDictAppRx, g_psoDictVend3GPP, 1, 0));

	return 0;
}

EXTENSION_ENTRY ("app_rx", app_rx_entry);
