#include "app_rx_data_types.h"

void app_rx_get_enum_val (vendor_id_t p_tVendId, avp_code_t p_tAVPCode, int32_t p_iVal, otl_value<std::string> &p_coValue)
{
  dict_avp_request soAVPReq = { p_tVendId, p_tAVPCode, NULL};
  dict_object *psoDictAVP;
  dict_object *psoDictType;
  dict_object *psoDictEnumValue;
  dict_enumval_data soAVPEnumData;
  dict_enumval_request soEnumValue;

  CHECK_FCT_DO (fd_dict_search (fd_g_config->cnf_dict, DICT_AVP, AVP_BY_CODE_AND_VENDOR, &soAVPReq, &psoDictAVP, ENOENT), return);
  CHECK_FCT_DO (fd_dict_search (fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, psoDictAVP, &psoDictType, ENOENT), return);
  soEnumValue.type_obj = psoDictType;
  soEnumValue.type_name = NULL;
  soEnumValue.search.enum_name = NULL;
  soEnumValue.search.enum_value.i32 = p_iVal;
  CHECK_FCT_DO (fd_dict_search (fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &soEnumValue, &psoDictEnumValue, ENOENT), return);
  CHECK_FCT_DO (fd_dict_getval (psoDictEnumValue, &soAVPEnumData), return);
  if (NULL != soAVPEnumData.enum_name && soAVPEnumData.enum_name[0] != '\0') {
    p_coValue.v = soAVPEnumData.enum_name;
    p_coValue.set_non_null ();
  } else {
    int iStrLen = 0;
    char *pszStr = NULL;
    iStrLen = snprintf (pszStr, 0, "%d", p_iVal);
    if (0 < iStrLen) {
      pszStr = new char[iStrLen + 1];
      if (iStrLen == snprintf (pszStr, iStrLen + 1, "%d", p_iVal)) {
        p_coValue.v = pszStr;
        p_coValue.set_non_null ();
      }
      delete [] pszStr;
    }
  }
}
