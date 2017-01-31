#include "app_rx_data_types.h"

#include <errno.h>

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

void app_rx_ip_addr_to_string(uint8_t *p_puiIPAddress, size_t p_stLen, otl_value<std::string> &p_coIPAddress)
{
  if (p_stLen != sizeof(unsigned int)) {
    LOG_D("invalid size of ip-address: '%u' != '%u'", p_stLen, sizeof(unsigned int));
    return;
  }

  int iFnRes;
  char mcAddr[16];
  SFramedIPAddress soAddr;

  soAddr.m_uAddr.m_uiAddr = *reinterpret_cast<unsigned int*>(p_puiIPAddress);

  iFnRes = snprintf(
    mcAddr, sizeof(mcAddr),
    "%u.%u.%u.%u",
    soAddr.m_uAddr.m_soAddr.b1, soAddr.m_uAddr.m_soAddr.b2, soAddr.m_uAddr.m_soAddr.b3, soAddr.m_uAddr.m_soAddr.b4);
  if (0 < iFnRes) {
    if (sizeof(mcAddr) > iFnRes) {
      p_coIPAddress = mcAddr;
    } else {
      LOG_D("buffer is too small to store a ip-address");
    }
  } else {
    LOG_D("snrpintf error: %s", strerror(errno));
  }
}
