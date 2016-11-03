#include <freeDiameter/extension.h>

/* функция загрузки объектов словаря */
int app_rx_dict_cache_init ();

/* объект словаря - вендор 3GPP */
extern struct dict_object *g_psoDictVend3GPP;
extern struct dict_object *g_psoDictAppRx;
