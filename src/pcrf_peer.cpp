#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <vector>

extern CLog *g_pcoLog;
std::vector<SPeerInfo> g_vectPeerList;

/* функция загружает список клиентов */
int app_pcrf_load_peer_info(std::vector<SPeerInfo> &p_vectPeerList, otl_connect &p_coDBConn);

/* функция проводит валидации клиента */
int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *));

/* функция формирует список клиентов */
int app_pcrf_load_peer ()
{
	int iRetVal = 0;
	int iFnRes;
	otl_connect *pcoDBConn = NULL;

	do {
		iFnRes = pcrf_db_pool_get((void **)&pcoDBConn, __func__);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		iFnRes = app_pcrf_load_peer_info (g_vectPeerList, *pcoDBConn);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		iFnRes = fd_peer_validate_register (app_pcrf_peer_validate);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
	} while (0);

	if (pcoDBConn) {
		pcrf_db_pool_rel(pcoDBConn, __func__);
		pcoDBConn = NULL;
	}

	return iRetVal;
}

int app_pcrf_load_peer_info(std::vector<SPeerInfo> &p_vectPeerList, otl_connect &p_coDBConn)
{
	int iRetVal = 0;

	otl_nocommit_stream coStream;
	try {
		SPeerInfo soPeerInfo;
		otl_value<std::string> coHostName;
		otl_value<std::string> coRealm;
		otl_value<unsigned> coProto;
		char mcDiamId[256];
		coStream.open (
			10,
			"select "
				"host_name,"
				"realm,"
				"protocol_id "
			"from "
				"ps.peer",
			p_coDBConn);
		while (! coStream.eof ()) {
			coStream
				>> coHostName
				>> coRealm
				>> coProto;
			if (! coHostName.is_null ())
				soPeerInfo.m_coHostName = coHostName;
			if (! coRealm.is_null ())
				soPeerInfo.m_coHostReal = coRealm;
			if (!coProto.is_null())
				soPeerInfo.m_uiPeerProto = coProto.v;
			else
				soPeerInfo.m_uiPeerProto = 0;
			p_vectPeerList.push_back (soPeerInfo);
		}
		coStream.close ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (! iRetVal) {
			iRetVal = -1;
		}
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int app_pcrf_peer_compare(peer_info &p_soLeft, SPeerInfo &p_soRight)
{
	int iRetVal;

	/* сравниваем длины имен */
	iRetVal = p_soLeft.pi_diamidlen - p_soRight.m_coHostName.v.length();
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем содержимое имен */
	iRetVal = memcmp (p_soLeft.pi_diamid, p_soRight.m_coHostName.v.data(), p_soLeft.pi_diamidlen);
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем длины реалмов */
	iRetVal = p_soLeft.runtime.pir_realmlen - p_soRight.m_coHostReal.v.length();
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем соответствие доменов */
	iRetVal = memcmp (p_soLeft.runtime.pir_realm, p_soRight.m_coHostReal.v.data(), p_soLeft.runtime.pir_realmlen);
	if (iRetVal) {
		return iRetVal;
	}

	return iRetVal;
}

int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *))
{
	int iRetVal = 0;

	*p_piAuth = 0;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin();

	for (; iterPeerList != g_vectPeerList.end (); ++ iterPeerList) {
		if (0 == app_pcrf_peer_compare (*p_psoPeerInfo, *iterPeerList)) {
			*p_piAuth = 1;
			p_psoPeerInfo->config.pic_flags.sec = PI_SEC_NONE;
			iterPeerList->m_iIsConnected = 1;
			break;
		}
	}

	return iRetVal;
}

int pcrf_peer_proto (SSessionInfo &p_soSessInfo)
{
	int iRetVal = -1403;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin ();

	while (iterPeerList != g_vectPeerList.end ()) {
		if (iterPeerList->m_coHostName.v == p_soSessInfo.m_coOriginHost.v
			&& iterPeerList->m_coHostReal.v == iterPeerList->m_coHostReal.v) {
			p_soSessInfo.m_uiPeerProto = iterPeerList->m_uiPeerProto;
			iRetVal = 0;
			break;
		}
		++iterPeerList;
	}

	return iRetVal;
}

int pcrf_peer_is_connected (SSessionInfo &p_soSessInfo)
{
	int iRetVal = -1403;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin ();

	while (iterPeerList != g_vectPeerList.end ()) {
		if (iterPeerList->m_coHostName.v == p_soSessInfo.m_coOriginHost.v
			&& iterPeerList->m_coHostReal.v == iterPeerList->m_coHostReal.v) {
			p_soSessInfo.m_uiPeerProto = iterPeerList->m_uiPeerProto;
			iRetVal = iterPeerList->m_iIsConnected;
			break;
		}
		++iterPeerList;
	}

	return iRetVal;
}
