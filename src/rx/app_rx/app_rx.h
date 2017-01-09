#ifndef _APP_RX_H_
#define _APP_RX_H_


#include <freeDiameter/extension.h>

/* функция загрузки объектов словаря */
int app_rx_dict_cache_init ();

extern struct dict_object *g_psoDictVend3GPP; /* объект словаря - вендор 3GPP */
extern struct dict_object *g_psoDictAppRx;    /* объект словаря - приложение Rx */
extern struct dict_object *g_psoDictCmdAAR;   /* объект словаря - команда AAR */

extern struct dict_object *g_psoDictAVPAuthApplicationId; /* Auth-Application-Id */
extern struct dict_object *g_psoDictAVPSubscriptionId;    /* Subscription-Id */

/* регистрация обработчика AAR */
#ifdef __cplusplus
extern "C"
#endif
int app_rx_register_aar_cb ();

#endif /* _APP_RX_H_ */
