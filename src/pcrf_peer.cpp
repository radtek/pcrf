#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <vector>

std::vector<peer_info> g_vectPeerList;

/* функция загружает список клиентов */
int app_pcrf_load_peer_info (std::vector<peer_info> &p_vectPeerList, otl_connect &p_coDBConn);

/* функция проводит валидации клиента */
int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *));

/* функция формирует список клиентов */
int app_pcrf_load_peer ()
{
	int iRetVal = 0;
	int iFnRes;
	otl_connect *pcoDBConn;

	do {
		iFnRes = pcrf_db_pool_get ((void **) &pcoDBConn);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		iFnRes = app_pcrf_load_peer_info (g_vectPeerList, *pcoDBConn);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
		if (pcoDBConn) {
			pcrf_db_pool_rel (pcoDBConn);
			pcoDBConn = NULL;
		}
		iFnRes = fd_peer_validate_register (app_pcrf_peer_validate);
		if (iFnRes) {
			iRetVal = iFnRes;
			break;
		}
	} while (0);

	if (pcoDBConn) {
		pcrf_db_pool_rel (pcoDBConn);
		pcoDBConn = NULL;
	}

	return iRetVal;
}

int app_pcrf_load_peer_info (std::vector<peer_info> &p_vectPeerList, otl_connect &p_coDBConn)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		peer_info soPeerInfo;
		otl_value<std::string> coHostName;
		otl_value<std::string> coRealm;
		otl_value<std::string> coIPAddress;
		otl_value<unsigned> coPort;
		char mcDiamId[256];
		coStream.open (
			10,
			"select "
				"host_name,"
				"realm,"
				"ip_address,"
				"port "
			"from "
				"ps.peer",
			p_coDBConn);
		while (! coStream.eof ()) {
			coStream
				>> coHostName
				>> coRealm
				>> coIPAddress
				>> coPort;
			memset (&soPeerInfo, 0, sizeof (soPeerInfo));
			if (! coHostName.is_null ()) {
				soPeerInfo.pi_diamid = strdup (coHostName.v.c_str ());
				soPeerInfo.pi_diamidlen = coHostName.v.length ();
			}
			if (! coRealm.is_null ()) {
				soPeerInfo.runtime.pir_realm = strdup (coRealm.v.c_str ());
				soPeerInfo.runtime.pir_realmlen = coRealm.v.length ();
			}
			p_vectPeerList.push_back (soPeerInfo);
		}
		coStream.close ();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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

int app_pcrf_peer_compare (peer_info &p_soLeft, peer_info &p_soRight)
{
	int iRetVal;

	/* сравниваем длины имен */
	iRetVal = p_soLeft.pi_diamidlen - p_soRight.pi_diamidlen;
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем содержимое имен */
	iRetVal = memcmp (p_soLeft.pi_diamid, p_soRight.pi_diamid, p_soLeft.pi_diamidlen);
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем длины реалмов */
	iRetVal = p_soLeft.runtime.pir_realmlen - p_soRight.runtime.pir_realmlen;
	if (iRetVal) {
		return iRetVal;
	}
	/* сравниваем соответствие доменов */
	iRetVal = memcmp (p_soLeft.runtime.pir_realm, p_soRight.runtime.pir_realm, p_soLeft.runtime.pir_realmlen);
	if (iRetVal) {
		return iRetVal;
	}

	return iRetVal;
}

int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *))
{
	int iRetVal = 0;

	*p_piAuth = 0;

	std::vector<peer_info>::iterator iterPeerList = g_vectPeerList.begin ();

	for (; iterPeerList != g_vectPeerList.end (); ++ iterPeerList) {
		if (0 == app_pcrf_peer_compare (*p_psoPeerInfo, *iterPeerList)) {
			*p_piAuth = 1;
			p_psoPeerInfo->config.pic_flags.sec = PI_SEC_NONE;
			break;
		}
	}

	return iRetVal;
}
