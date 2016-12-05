#include "app_rx.h"
#include "app_rx_data_types.h"

static int app_rx_rar_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *);

int app_rx_raa_cb ( struct msg **, struct avp *, struct session *, void *, enum disp_action *)
{
  return 0;
}
