#include <freeDiameter/extension.h>

/* функция загрузки объектов словаря */
int app_rx_dict_cache_init ();

extern struct dict_object *g_psoDictVend3GPP; /* объект словаря - вендор 3GPP */
extern struct dict_object *g_psoDictAppRx;    /* объект словаря - приложение Rx */
extern struct dict_object *g_psoDictCmdAAR;   /* объект словаря - команда AAR */

/* регистрация обработчика AAR */
int app_rx_register_aar_cb ();
