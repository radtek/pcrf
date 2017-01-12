#include "app_pcrf.h"
#include "app_pcrf_header.h"

#include <string.h>

extern CLog *g_pcoLog;

int pcrf_extract_avp_enum_val (struct avp_hdr *p_psoAVPHdr, char *p_pszBuf, int p_iBufSize)
{
	int iRetVal = 0;
	int iFnRes;

	/* запрашиваем в словаре идентификатор */
	struct dict_object *psoDictObj;
	struct dict_avp_request_ex soAVPIdent;
	memset (&soAVPIdent, 0, sizeof (soAVPIdent));
	soAVPIdent.avp_vendor.vendor_id = p_psoAVPHdr->avp_vendor;
	soAVPIdent.avp_data.avp_code = p_psoAVPHdr->avp_code;
	if (NULL == soAVPIdent.avp_vendor.vendor
			&& 0 == soAVPIdent.avp_vendor.vendor_id
			&& NULL == soAVPIdent.avp_vendor.vendor_name) {
		CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_AVP, AVP_BY_CODE, &(soAVPIdent.avp_data.avp_code), &psoDictObj, ENOENT));
	} else {
		CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_AVP, AVP_BY_STRUCT, &soAVPIdent, &psoDictObj, ENOENT));
	}

	/* запрашиваем в словаре информацию об AVP */
	struct dict_avp_data soAVPInfo;
	CHECK_POSIX (fd_dict_getval (psoDictObj, &soAVPInfo));

	/* структура дл¤ формировани¤ запроса к словарю и выборки из него значени¤ */
	struct dict_enumval_request soEnumReq;
	memset (&soEnumReq, 0, sizeof (soEnumReq));
	/* копируем запрашаваемое значение */
	switch (soAVPInfo.avp_basetype) {
	case AVP_TYPE_INTEGER32:
		soEnumReq.search.enum_value.i32 = p_psoAVPHdr->avp_value->i32;
		break;
	case AVP_TYPE_INTEGER64:
		soEnumReq.search.enum_value.i64 = p_psoAVPHdr->avp_value->i64;
		break;
	case AVP_TYPE_UNSIGNED32:
		soEnumReq.search.enum_value.u32 = p_psoAVPHdr->avp_value->u32;
		break;
	case AVP_TYPE_UNSIGNED64:
		soEnumReq.search.enum_value.u64 = p_psoAVPHdr->avp_value->u64;
		break;
	case AVP_TYPE_FLOAT32:
		soEnumReq.search.enum_value.f32 = p_psoAVPHdr->avp_value->f32;
		break;
	case AVP_TYPE_FLOAT64:
		soEnumReq.search.enum_value.f64 = p_psoAVPHdr->avp_value->f64;
		break;
	default:
		return -3333;
	}

	struct dict_object *psoDictTypeObj;
	struct dict_object *psoEnumDictObj;

	CHECK_FCT (fd_dict_search (fd_g_config->cnf_dict, DICT_TYPE, TYPE_OF_AVP, psoDictObj, &psoDictTypeObj, ENOENT));
	soEnumReq.type_obj = psoDictTypeObj;
	if (0 == fd_dict_search (fd_g_config->cnf_dict, DICT_ENUMVAL, ENUMVAL_BY_STRUCT, &soEnumReq, &psoEnumDictObj, ENOTSUP)) {
		if (0 == fd_dict_getval (psoEnumDictObj, &(soEnumReq.search))) {
			strncpy (p_pszBuf, soEnumReq.search.enum_name, p_iBufSize);
			p_pszBuf[p_iBufSize - 1] = 0;
		}
	} else {
		switch (soAVPInfo.avp_basetype) {
		case AVP_TYPE_INTEGER32:
			iFnRes = snprintf (p_pszBuf, p_iBufSize , "%#08x", (int) p_psoAVPHdr->avp_value->i32);
      if (0 < iFnRes) {
      } else {
        iRetVal = -1;
      }
			break;
		case AVP_TYPE_UNSIGNED32:
			iFnRes = snprintf (p_pszBuf, p_iBufSize , "%#08x", (unsigned int) p_psoAVPHdr->avp_value->u32);
      if (0 < iFnRes) {
      } else {
        iRetVal = -1;
      }
			break;
		case AVP_TYPE_INTEGER64:
			iFnRes = snprintf (p_pszBuf, p_iBufSize - 1, "%#016Lx", static_cast<unsigned long long>(p_psoAVPHdr->avp_value->i64));
			if (iFnRes < 0) {
				iRetVal = errno;
			}
			break;
		case AVP_TYPE_UNSIGNED64:
			iFnRes = snprintf (p_pszBuf, p_iBufSize - 1, "%#016Lx", static_cast<unsigned long long>(p_psoAVPHdr->avp_value->u64));
			if (iFnRes < 0) {
				iRetVal = errno;
			}
			break;
		case AVP_TYPE_FLOAT32:
			iFnRes = snprintf (p_pszBuf, p_iBufSize , "%f", p_psoAVPHdr->avp_value->f32);
      if (0 < iFnRes) {
      } else {
        iRetVal = -1;
      }
			break;
		case AVP_TYPE_FLOAT64:
			iFnRes = snprintf (p_pszBuf, p_iBufSize , "%f", p_psoAVPHdr->avp_value->f64);
      if (0 < iFnRes) {
      } else {
        iRetVal = -1;
      }
			break;
		default:
			iRetVal = -1;
			break;
		}
    if (0 == iRetVal) {
      if (p_iBufSize > iFnRes) {
      } else {
        p_pszBuf[p_iBufSize - 1] = '\0';
      }
    }
	}

	return iRetVal;
}
