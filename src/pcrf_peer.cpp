#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <vector>

extern CLog *g_pcoLog;
std::vector<SPeerInfo> g_vectPeerList;

/* функция загружает список клиентов */
int app_pcrf_load_peer_info( std::vector<SPeerInfo> &p_vectPeerList, otl_connect *p_pcoDBConn );

/* функция проводит валидации клиента */
int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *));

/* функция формирует список клиентов */
int app_pcrf_load_peer()
{
  int iRetVal = 0;
  otl_connect *pcoDBConn = NULL;

  do {
    if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, 10 * USEC_PER_SEC ) && NULL != pcoDBConn ) {
    } else {
      break;
    }
    iRetVal = app_pcrf_load_peer_info( g_vectPeerList, pcoDBConn );
    if ( iRetVal ) {
      break;
    }
    iRetVal = fd_peer_validate_register( app_pcrf_peer_validate );
    if ( iRetVal ) {
      break;
    }
  } while ( 0 );

  if ( pcoDBConn ) {
    pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
    pcoDBConn = NULL;
  }

  return iRetVal;
}

int app_pcrf_load_peer_info( std::vector<SPeerInfo> &p_vectPeerList, otl_connect *p_pcoDBConn )
{
  if ( NULL != p_pcoDBConn ) {
  } else {
    return EINVAL;
  }

  int iRetVal = 0;
  int iRepeat = 1;

  otl_nocommit_stream coStream;

  sql_repeat:

  try {
    SPeerInfo soPeerInfo;
    otl_value<std::string> coHostName;
    otl_value<std::string> coRealm;
    otl_value<unsigned> coDialect;

    coStream.open(
      10,
      "select "
      "host_name,"
      "realm,"
      "protocol_id "
      "from "
      "ps.peer",
      *p_pcoDBConn );
    while ( ! coStream.eof() ) {
      soPeerInfo.m_coHostName = "";
      soPeerInfo.m_coHostReal = "";
      soPeerInfo.m_uiPeerDialect = 0;
      coStream
        >> coHostName
        >> coRealm
        >> coDialect;
      if ( ! coHostName.is_null() ) {
        soPeerInfo.m_coHostName = coHostName;
      }
      if ( ! coRealm.is_null() ) {
        soPeerInfo.m_coHostReal = coRealm;
      }
      soPeerInfo.m_uiPeerDialect = ( ( 0 == coDialect.is_null() ) ? coDialect.v : GX_UNDEF );
      p_vectPeerList.push_back( soPeerInfo );
    }
  } catch ( otl_exception &coExcept ) {
    UTL_LOG_E( *g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( p_pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
    if ( 0 != coExcept.code ) {
      iRetVal = coExcept.code;
    } else {
      iRetVal = -1;
    }
  }

  return iRetVal;
}

int app_pcrf_peer_compare (peer_info &p_soLeft, SPeerInfo &p_soRight)
{
	int iRetVal;

	/* сравниваем длины имен */
	iRetVal = p_soLeft.pi_diamidlen - p_soRight.m_coHostName.v.length();
	if (0 == iRetVal) {
  } else {
		return iRetVal;
	}
	/* сравниваем содержимое имен */
	iRetVal = memcmp (p_soLeft.pi_diamid, p_soRight.m_coHostName.v.data(), p_soLeft.pi_diamidlen);
	if (0 == iRetVal) {
  } else {
		return iRetVal;
	}
	/* сравниваем длины реалмов */
	iRetVal = p_soLeft.runtime.pir_realmlen - p_soRight.m_coHostReal.v.length();
	if (0 == iRetVal) {
  } else {
		return iRetVal;
	}
	/* сравниваем соответствие доменов */
	iRetVal = memcmp (p_soLeft.runtime.pir_realm, p_soRight.m_coHostReal.v.data(), p_soLeft.runtime.pir_realmlen);
	if (0 == iRetVal) {
  } else {
		return iRetVal;
	}

	return iRetVal;
}

int app_pcrf_peer_validate (peer_info *p_psoPeerInfo, int *p_piAuth, int (**cb2)(struct peer_info *))
{
	int iRetVal = 0;

  /* suppress compiler warning */
  cb2 = cb2;

  if (p_piAuth) {
  	*p_piAuth = 0;
  }

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin();

	for (; iterPeerList != g_vectPeerList.end (); ++ iterPeerList) {
		if (0 == app_pcrf_peer_compare (*p_psoPeerInfo, *iterPeerList)) {
      if (p_piAuth) {
			  *p_piAuth = 1;
      }
			p_psoPeerInfo->config.pic_flags.sec = PI_SEC_NONE;
			iterPeerList->m_iIsConnected = 1;
      UTL_LOG_D(*g_pcoLog, "peer validated: host name: '%s'; realm: '%s'; dialect: '%u'",
        iterPeerList->m_coHostName.v.c_str(),
        iterPeerList->m_coHostReal.v.c_str(),
        iterPeerList->m_uiPeerDialect);
			break;
		}
	}

	return iRetVal;
}

int pcrf_peer_dialect (SSessionInfo &p_soSessInfo)
{
	int iRetVal = 1403;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin ();

	for (; iterPeerList != g_vectPeerList.end (); ++iterPeerList) {
		if (iterPeerList->m_coHostName.v == p_soSessInfo.m_coOriginHost.v && iterPeerList->m_coHostReal.v == p_soSessInfo.m_coOriginRealm.v) {
			p_soSessInfo.m_uiPeerDialect = iterPeerList->m_uiPeerDialect;
			iRetVal = 0;
			break;
		}
	}

	return iRetVal;
}

int pcrf_peer_is_connected (SSessionInfo &p_soSessInfo)
{
	int iRetVal = 1403;

	std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin ();

	for (; iterPeerList != g_vectPeerList.end (); ++iterPeerList) {
		if (iterPeerList->m_coHostName.v == p_soSessInfo.m_coOriginHost.v && iterPeerList->m_coHostReal.v == p_soSessInfo.m_coOriginRealm.v) {
			iRetVal = iterPeerList->m_iIsConnected;
			break;
		}
	}

	return iRetVal;
}

int pcrf_peer_is_dialect_used (unsigned int p_uiPeerDialect)
{
  int iRetVal = 0;

  std::vector<SPeerInfo>::iterator iterPeerList = g_vectPeerList.begin ();

  for (; iterPeerList != g_vectPeerList.end (); ++iterPeerList) {
    if (iterPeerList->m_uiPeerDialect == p_uiPeerDialect && iterPeerList->m_iIsConnected) {
      iRetVal = 1;
      break;
    }
  }

  return iRetVal;
}
