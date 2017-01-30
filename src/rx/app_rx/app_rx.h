#ifndef _APP_RX_H_
#define _APP_RX_H_

#ifdef __cplusplus
extern "C" {
#endif


#include <freeDiameter/extension.h>

  /* функция загрузки объектов словаря */
  int app_rx_dict_cache_init();

  extern struct dict_object *g_psoDictVend3GPP; /* объект словаря - вендор 3GPP */
  extern struct dict_object *g_psoDictAppRx;    /* объект словаря - приложение Rx */
  extern struct dict_object *g_psoDictCmdAAR;   /* объект словаря - команда AAR */
  extern struct dict_object *g_psoDictCmdSTR;   /* объект словаря - команда STR */

  extern struct dict_object *g_psoDictAVPAuthApplicationId;       /* Auth-Application-Id */
  extern struct dict_object *g_psoDictAVPSubscriptionId;          /* Subscription-Id */
  extern struct dict_object *g_psoDictAVPOriginHost;              /* Origin-Host */
  extern struct dict_object *g_psoDictAVPOriginRealm;             /* Origin-Realm */
  extern struct dict_object *g_psoDictAVPExperimentalResult;      /* Experimental-Result */
  extern struct dict_object *g_psoDictAVPExperimentalResultCode;  /* Experimental-Result-Code */
  extern struct dict_object *g_psoDictAVPVendorId;                /* Vendor-Id */
  extern struct dict_object *g_psoDictAVPIPCANType;               /* IP-CAN-Type */

  /* регистрация обработчика AAR */
  /* регистрация обработчика AAR */
  int app_rx_register_aar_cb();
  /* регистрация обработчика STR */
  int app_rx_register_str_cb();

#ifdef __cplusplus
}
#endif

#endif /* _APP_RX_H_ */
