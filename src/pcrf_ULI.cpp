#include "app_pcrf_header.h"

extern CLog *g_pcoLog;

#pragma pack(push, 1)
struct SMCCMNC {
	unsigned m_uiMCC1 : 4;
	unsigned m_uiMCC2 : 4;
	unsigned m_uiMCC3 : 4;
	unsigned m_uiMNC3 : 4;
	unsigned m_uiMNC1 : 4;
	unsigned m_uiMNC2 : 4;
};
struct SLAC {
	unsigned m_uiLAC1 : 8;
	unsigned m_uiLAC2 : 8;
};
struct SCI {
	unsigned m_uiCI1 : 8;
	unsigned m_uiCI2 : 8;
};
struct SSAS {
	unsigned m_uiSAS1 : 8;
	unsigned m_uiSAS2 : 8;
};
struct SRAC {
	unsigned m_uiRAC : 8;
	unsigned m_uiPadding : 8;
};
struct STAC {
	unsigned m_uiTAC1 : 8;
	unsigned m_uiTAC2 : 8;
};
struct SECI {
	unsigned m_uiECI1 : 4;
	unsigned m_uiPadding : 4;
	unsigned m_uiECI2 : 8;
	unsigned m_uiECI3 : 8;
	unsigned m_uiECI4 : 8;
};
struct SCGI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SCI m_soCI;
};
struct SSAI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SSAS m_soSAS;
};
struct SRAI {
	SMCCMNC m_soMCCMNC;
	SLAC m_soLAC;
	SRAC m_soRAC;
};
struct STAI {
	SMCCMNC m_soMCCMNC;
	STAC m_soTAC;
};
struct SECGI {
	SMCCMNC m_soMCCMNC;
	SECI m_soECI;
};
struct STAI_ECGI {
	STAI m_soTAI;
	SECGI m_soECGI;
};
#pragma pack(pop)

enum EUserLocationType {
	eCGI = 0,
	eSAI = 1,
	eRAI = 2,
	eTAI = 128,
	eECGI = 129,
	eTAI_ECGI = 130
};

void format_CGI(SCGI &p_soCGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_SAI(SSAI &p_soSAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_RAI(SRAI &p_soRAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_TAI(STAI &p_soTAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);
void format_ECGI(SECGI &p_soECGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue);

int pcrf_parse_user_location( avp_value *p_psoAVPValue, SUserLocation &p_soUserLocationInfo, bool *p_pbLoaded )
{
	int iRetVal = 0;
	int iFnRes;

	SCGI soCGI;
	SSAI soSAI;
	SRAI soRAI;
	STAI soTAI;
	SECGI soECGI;
	STAI_ECGI soTAI_ECGI;

	SMCCMNC *psoMCCMNC = NULL;
	char mcMCCMNC[8];

	switch (p_psoAVPValue->os.data[0]) {
	case eCGI:
		if (p_psoAVPValue->os.len >= sizeof(soCGI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SCGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soCGI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soCGI.m_soMCCMNC);
		break;
	case eSAI:
		if (p_psoAVPValue->os.len >= sizeof(soSAI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SSAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soSAI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soSAI.m_soMCCMNC);
		break;
	case eRAI:
		if (p_psoAVPValue->os.len >= sizeof(soRAI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SRAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soRAI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soRAI.m_soMCCMNC);
		break;
	case eTAI:
		if (p_psoAVPValue->os.len >= sizeof(soTAI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of STAI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soTAI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soTAI.m_soMCCMNC);
		break;
	case eECGI:
		if (p_psoAVPValue->os.len >= sizeof(soECGI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of SECGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soECGI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soECGI.m_soMCCMNC);
		break;
	case eTAI_ECGI:
		if (p_psoAVPValue->os.len >= sizeof(soTAI_ECGI)) {
    } else {
			UTL_LOG_E(*g_pcoLog, "value length less than size of soTAI_ECGI struct");
			iRetVal = -1;
			break;
		}
		memcpy(&soTAI_ECGI, &(p_psoAVPValue->os.data[1]), p_psoAVPValue->os.len - 1);
		psoMCCMNC = &(soTAI_ECGI.m_soTAI.m_soMCCMNC);
		break;
	}

	if (0 == iRetVal) {
  } else {
		return iRetVal;
  }

  if (NULL != psoMCCMNC) {
  } else {
		UTL_LOG_E(*g_pcoLog, "unexpected error: NULL pointer to MCCMNC");
		return -2;
	}

	/* формируем MCCMNC */
	iFnRes = snprintf(
		mcMCCMNC, sizeof(mcMCCMNC),
		"%u%u%u-%u%u",
		psoMCCMNC->m_uiMCC1, psoMCCMNC->m_uiMCC2, psoMCCMNC->m_uiMCC3,
		psoMCCMNC->m_uiMNC1, psoMCCMNC->m_uiMNC2);
	if (0 < iFnRes) {
    if (sizeof(mcMCCMNC) > static_cast<size_t>(iFnRes)) {
    } else {
      mcMCCMNC[sizeof(mcMCCMNC) - 1] = '\0';
    }
  } else {
		iRetVal = -1;
		UTL_LOG_E(*g_pcoLog, "snprintf error code: '%d'", errno);
		mcMCCMNC[0] = '\0';
	}

	/* что-то полезное уже имеем, ставим метку, что данные получены */
  if ( NULL != p_pbLoaded ) {
    *p_pbLoaded = true;
  }

	/* формируем опциональные данные */
	switch (p_psoAVPValue->os.data[0]) {
	case eCGI:
		format_CGI(soCGI, mcMCCMNC, p_soUserLocationInfo.m_coCGI);
		break;
	case eSAI:
		break;
	case eRAI:
		format_RAI(soRAI, mcMCCMNC, p_soUserLocationInfo.m_coRAI);
		break;
	case eTAI:
		format_TAI(soTAI, mcMCCMNC, p_soUserLocationInfo.m_coTAI);
		break;
	case eECGI:
		format_ECGI(soECGI, mcMCCMNC, p_soUserLocationInfo.m_coECGI);
		break;
	case eTAI_ECGI:
		format_TAI(soTAI_ECGI.m_soTAI, mcMCCMNC, p_soUserLocationInfo.m_coTAI);
		format_ECGI(soTAI_ECGI.m_soECGI, mcMCCMNC, p_soUserLocationInfo.m_coECGI);
		break;
	default:
		iFnRes = 0;
		break;
	}

	return iRetVal;
}

void format_CGI(SCGI &p_soCGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];
	std::string strSector;

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soCGI.m_soLAC.m_uiLAC1 << 8) + (p_soCGI.m_soLAC.m_uiLAC2),
		(p_soCGI.m_soCI.m_uiCI1 << 8 ) + (p_soCGI.m_soCI.m_uiCI2));
	if (0 < iFnRes) {
    if (sizeof(mcValue) > static_cast<size_t>(iFnRes)) {
      /* выбираем сектор - последняя цифра CI */
      strSector = mcValue[iFnRes - 1];
    } else {
      mcValue[sizeof(mcValue) - 1] = '\0';
    }
  } else {
		return;
  }

	p_coValue.v += '-';
	p_coValue.v += mcValue;
	/* формируем сектор */
	if (0 < strSector.length()) {
		p_coValue.v[p_coValue.v.length() - 1] = '-';
		p_coValue.v += strSector;
	}
}

void format_SAI(SSAI &p_soSAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soSAI.m_soLAC.m_uiLAC1 << 8) + (p_soSAI.m_soLAC.m_uiLAC2),
		(p_soSAI.m_soSAS.m_uiSAS1 << 8) + (p_soSAI.m_soSAS.m_uiSAS2));
	if (0 < iFnRes) {
    if (sizeof(mcValue) > static_cast<size_t>(iFnRes)) {
    } else {
      mcValue[sizeof(mcValue) - 1] = '\0';
    }
  } else {
		return;
  }
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_RAI(SRAI &p_soRAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soRAI.m_soLAC.m_uiLAC1 << 8) + (p_soRAI.m_soLAC.m_uiLAC2),
		p_soRAI.m_soRAC.m_uiRAC);
	if (0 < iFnRes) {
    if (sizeof(mcValue) > static_cast<size_t>(iFnRes)) {
    } else {
      mcValue[sizeof(mcValue) - 1] = '\0';
    }
  } else {
		return;
  }
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_TAI(STAI &p_soTAI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u",
		(p_soTAI.m_soTAC.m_uiTAC1 << 8) + (p_soTAI.m_soTAC.m_uiTAC2));
	if (0 < iFnRes) {
    if (sizeof(mcValue) > static_cast<size_t>(iFnRes)) {
    } else {
      mcValue[sizeof(mcValue) - 1] = '\0';
    }
  } else {
		return;
  }
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

void format_ECGI(SECGI &p_soECGI, const char *p_pszMCCMNC, otl_value<std::string> &p_coValue)
{
	int iFnRes;
	char mcValue[128];

	p_coValue = p_pszMCCMNC;

	iFnRes = snprintf(
		mcValue, sizeof(mcValue),
		"%u-%u",
		(p_soECGI.m_soECI.m_uiECI1 << 16) + (p_soECGI.m_soECI.m_uiECI2 << 8) + (p_soECGI.m_soECI.m_uiECI3),
		p_soECGI.m_soECI.m_uiECI4);
	if (0 < iFnRes) {
    if (sizeof(mcValue) > static_cast<size_t>(iFnRes)) {
    } else {
      mcValue[sizeof(mcValue) - 1] = '\0';
    }
  } else {
		return;
  }
	p_coValue.v += '-';
	p_coValue.v += mcValue;
}

int pcrf_parse_RAI(avp_value &p_soAVPValue, otl_value<std::string> &p_coValue)
{
	int iRetVal = 0;
	int iFnRes;
	char mcMCCMNC[128];
	SRAI soRAI;

	/* проверяем размер данных */
	if (p_soAVPValue.os.len >= 5 + sizeof(soRAI.m_soLAC) + sizeof(soRAI.m_soRAC)) {
  } else {
		return EINVAL;
  }

	iFnRes = snprintf(
		mcMCCMNC, sizeof(mcMCCMNC),
		"%c%c%c-%c%c",
		p_soAVPValue.os.data[0], p_soAVPValue.os.data[1], p_soAVPValue.os.data[2],
		p_soAVPValue.os.data[3], p_soAVPValue.os.data[4]);
	if (0 < iFnRes) {
    if (sizeof(mcMCCMNC) > static_cast<size_t>(iFnRes)) {
    } else {
      mcMCCMNC[sizeof(mcMCCMNC) - 1] = '\0';
    }
  } else {
		return -1;
  }

	memcpy(((char*)(&soRAI)) + sizeof(soRAI.m_soMCCMNC), &(p_soAVPValue.os.data[5]), sizeof(soRAI.m_soLAC) + sizeof(soRAI.m_soRAC));

	format_RAI(soRAI, mcMCCMNC, p_coValue);

	return iRetVal;
}
