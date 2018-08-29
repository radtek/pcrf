#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include "utils/stat/stat.h"

#include <vector>
#include <stdio.h>

extern CLog *g_pcoLog;

static SStat *g_psoReqStat;
static SStat *g_psoPeerStat;

static void pcrf_tracer (
	fd_hook_type p_eHookType,
	msg * p_psoMsg,
	peer_hdr * p_psoPeer,
	void * p_pOther,
	fd_hook_permsgdata *p_psoPMD,
	void * p_pRegData)
{
  if ( NULL != p_psoMsg ) {
  } else {
    UTL_LOG_E(*g_pcoLog, "NULL pointer to message structure");
    return;
  }

  msg_hdr *psoMsgHdr;

  CHECK_FCT_DO(fd_msg_hdr(p_psoMsg, &psoMsgHdr), return);

  int iFnRes;
  std::string strRequestType;
  std::string strPeerName;
  std::string strPeerRlm;

  /* формируем Request Type */
  /* тип команды */
  switch (psoMsgHdr->msg_code) {
  case 257: /* Capabilities-Exchange */
    strRequestType = "CE";
    break;
  case 258: /* Re-Auth */
    strRequestType = "RA";
    break;
  case 265: /* AA */
    strRequestType = "AA";
    break;
  case 271: /* Accounting */
    strRequestType = "A";
    break;
  case 272: /* Credit-Control */
    strRequestType = "CC";
    break;
  case 274: /* Abort-Session */
    strRequestType = "AS";
    break;
  case 275: /* Session-Termination */
    strRequestType = "ST";
    break;
  case 280: /* Device-Watchdog */
    strRequestType = "DW";
    break;
  case 282: /* Disconnect-Peer */
    strRequestType = "DP";
    break;
  default:
  {
    char mcCode[256];
    iFnRes = snprintf(mcCode, sizeof(mcCode), "%u", psoMsgHdr->msg_code);
    if (0 < iFnRes) {
      if (sizeof(mcCode) > static_cast<size_t>(iFnRes)) {
      } else {
        mcCode[sizeof(mcCode) - 1] = '\0';
      }
      strRequestType = mcCode;
    }
  }
  break;
  }
  /* тип запроса */
  if (psoMsgHdr->msg_flags & CMD_FLAG_REQUEST) {
    strRequestType += 'R';
  } else {
    strRequestType += 'A';
  }

  if (NULL != p_psoPeer) {
    strPeerName.insert(0, reinterpret_cast<char*>(p_psoPeer->info.pi_diamid), p_psoPeer->info.pi_diamidlen);
    strPeerRlm.insert( 0, reinterpret_cast<char*>( p_psoPeer->info.runtime.pir_realm ), p_psoPeer->info.runtime.pir_realmlen );
  } else {
    strPeerName = "<unknown peer>";
    strPeerRlm = "<unknown realm>";
  }

  /* статистика по пирам */
  stat_measure(g_psoPeerStat, strPeerRlm.c_str(), NULL);

  char *pmcBuf = NULL;
  size_t stLen;

  CHECK_MALLOC_DO(
    fd_msg_dump_treeview( &pmcBuf, &stLen, NULL, p_psoMsg, fd_g_config->cnf_dict, 1, 1 ),
    { UTL_LOG_E( *g_pcoLog, "Error while dumping a message" ); return; } );

  /* добываем необходимые значения из запроса */
  uint32_t    ui32ApplicationId = 0;
  std::string strSessionId;
  std::string strOriginHost;
  std::string strOriginReal;
  std::string strDestinHost;
  std::string strResultCode;
  std::string strDestinReal;
  char mcEnumValue[ 256 ];
  avp_hdr *psoAVPHdr;
  avp *psoAVP = reinterpret_cast<avp*>(p_psoMsg);
  msg_brw_dir eSearchDirection = MSG_BRW_FIRST_CHILD;

  while ( 0 == fd_msg_browse_internal( psoAVP, eSearchDirection, reinterpret_cast<msg_or_avp**>(&psoAVP), NULL ) && NULL != psoAVP ) {
    eSearchDirection = MSG_BRW_NEXT;
    if ( 0 == fd_msg_avp_hdr( psoAVP, &psoAVPHdr ) ) {
    } else {
      continue;
    }
    /* нас интересуют лишь вендор Diameter */
    if ( psoAVPHdr->avp_vendor == 0 ) {
    } else {
      continue;
    }
    switch ( psoAVPHdr->avp_code ) {
      case 258: /* Auth-Application-Id */
        ui32ApplicationId = psoAVPHdr->avp_value->u32;
        break;  /* Auth-Application-Id */
      case 263: /* Session-Id */
        if ( NULL != psoAVPHdr->avp_value ) {
          strSessionId.insert( 0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
        } else {
          LOG_D( "Session-Id: %p", psoAVPHdr->avp_value );
        }
        break;
      case 264: /* Origin-Host */
        if ( NULL != psoAVPHdr->avp_value ) {
          strOriginHost.insert( 0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
        } else {
          LOG_D( "Origin-Host: %p", psoAVPHdr->avp_value );
        }
        break;
      case 268: /* Result-Code */
        if ( NULL != psoAVPHdr->avp_value ) {
          iFnRes = pcrf_extract_avp_enum_val( psoAVPHdr, mcEnumValue, sizeof( mcEnumValue ) );
          if ( 0 == iFnRes ) {
            strResultCode = mcEnumValue;
          }
        } else {
          LOG_D( "Result-Code: %p", psoAVPHdr->avp_value );
        }
        break;
      case 283: /* Destination-Realm */
        if ( NULL != psoAVPHdr->avp_value ) {
          strDestinReal.insert( 0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
        } else {
          LOG_D( "Destination-Realm: %p", psoAVPHdr->avp_value );
        }
        break;
      case 293: /* Destination-Host */
        if ( NULL != psoAVPHdr->avp_value ) {
          strDestinHost.insert( 0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
        } else {
          LOG_D( "Destination-Host: %p", psoAVPHdr->avp_value );
        }
        break;
      case 296: /* Origin-Realm */
        if ( NULL != psoAVPHdr->avp_value ) {
          strOriginReal.insert( 0, (const char*)psoAVPHdr->avp_value->os.data, psoAVPHdr->avp_value->os.len );
        } else {
          LOG_D( "Origin-Realm: %p", psoAVPHdr->avp_value );
        }
        break;
      case 416: /* CC-Request-Type */
        if ( NULL != psoAVPHdr->avp_value ) {
          iFnRes = pcrf_extract_avp_enum_val( psoAVPHdr, mcEnumValue, sizeof( mcEnumValue ) );
          if ( 0 == iFnRes && psoMsgHdr->msg_code == 272 ) {
            strRequestType += '-';
            strRequestType += mcEnumValue[ 0 ];
          }
        } else {
          LOG_D( "CC-Request-Type: %p", psoAVPHdr->avp_value );
        }
        break;
    }
  }

  /* статистика по запросам */
  stat_measure( g_psoReqStat, strRequestType.c_str(), NULL );

  /* если трассировка включена следуем дальше */
  if ( 0 != g_psoConf->m_iTraceReq || ui32ApplicationId == 16777236 ) {
  } else {
    /* если нет необходимости трассировки */
    if ( NULL != pmcBuf ) {
      fd_cleanup_buffer( pmcBuf );
    }
    return;
  }

  /* suppress compiler warning */
  p_pOther = p_pOther; p_psoPMD = p_psoPMD; p_pRegData = p_pRegData;

	otl_value<std::string> coSessionId;
  otl_value<std::string> coRequestType;
	otl_value<std::string> coOriginHost;
	otl_value<std::string> coDestinHost;
  otl_value<std::string> coOTLOriginReal;
  otl_value<std::string> coOTLDestinReal;
  otl_value<std::string> coOTLResultCode;
  otl_value<std::string> coParsedPack;
  otl_value<otl_datetime> coDateTime;
  std::list<SSQLQueueParam*> *plistParameters = new std::list<SSQLQueueParam*>;

  /* копируем Session-Id */
  if ( 0 < strSessionId.length() ) {
    coSessionId = strSessionId;
  }
  /* копируем Request-Type */
  if ( 0 < strRequestType.length() ) {
    coRequestType = strRequestType;
  }
  /* копируем Origin-Host */
  if ( 0 < strOriginHost.length() ) {
    coOriginHost = strOriginHost;
  }
  /* копируем Destination-Host */
  if ( 0 < strDestinHost.length() ) {
    coDestinHost = strDestinHost;
  }
	/* проверяем наличие обязательных атрибутов */
	if (0 != coOriginHost.is_null()) {
		switch (p_eHookType)
		{
		case HOOK_MESSAGE_RECEIVED:
			coOriginHost = strPeerName;
			break;
		case HOOK_MESSAGE_SENT:
			coOriginHost.v.insert (0, (const char*)fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
      coOriginHost.set_non_null();
			break;
		default:
			break;
		}
	}
	if (0 != coDestinHost.is_null()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			coDestinHost.v.insert (0, (const char*)fd_g_config->cnf_diamid, fd_g_config->cnf_diamid_len);
      coDestinHost.set_non_null();
			break;
		case HOOK_MESSAGE_SENT:
			coDestinHost = strPeerName;
			break;
		default:
			break;
		}
	}
	/* проверяем возможность заполнения опциональных атрибутов */
	if (0 == strOriginReal.length ()) {
		switch (p_eHookType)
		{
      case HOOK_MESSAGE_RECEIVED:
        strOriginReal = strPeerRlm;
        break;
      case HOOK_MESSAGE_SENT:
			strOriginReal.insert (0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
		default:
			break;
		}
	}
	if (0 == strDestinReal.length ()) {
		switch (p_eHookType) {
		case HOOK_MESSAGE_RECEIVED:
			strDestinReal.insert (0, (const char*)fd_g_config->cnf_diamrlm, fd_g_config->cnf_diamrlm_len);
			break;
    case HOOK_MESSAGE_SENT:
      strDestinReal = strPeerRlm;
      break;
    default:
			break;
		}
	}

  if ( strOriginReal.length() ) {
    coOTLOriginReal = strOriginReal;
  }
  if ( strDestinReal.length() ) {
    coOTLDestinReal = strDestinReal;
  }
  if ( strResultCode.length() ) {
    coOTLResultCode = strResultCode;
  }
  pcrf_fill_otl_datetime( coDateTime, NULL );
  coParsedPack = pmcBuf;

  pcrf_sql_queue_add_param( plistParameters, coSessionId);
  pcrf_sql_queue_add_param( plistParameters, coDateTime );
  pcrf_sql_queue_add_param( plistParameters, coRequestType );
  pcrf_sql_queue_add_param( plistParameters, coOriginHost );
  pcrf_sql_queue_add_param( plistParameters, coOTLOriginReal );
  pcrf_sql_queue_add_param( plistParameters, coDestinHost);
  pcrf_sql_queue_add_param( plistParameters, coOTLDestinReal);
  pcrf_sql_queue_add_param( plistParameters, coOTLResultCode);
  pcrf_sql_queue_add_param( plistParameters, coParsedPack );

  pcrf_sql_queue_enqueue(
    "insert into ps.requestList"
    "(seq_id,session_id,event_date,request_type,origin_host,origin_realm,destination_host,destination_realm,diameter_result,message)"
    "values"
    "(ps.requestlist_seq.nextval,:session_id/*char[255]*/,:date_time/*timestamp*/,:request_type/*char[10]*/,:origin_host/*char[100]*/,:origin_realm/*char[100]*/,:destination_host/*char[100]*/,:destination_realm/*char[100]*/,:diameter_result/*char[100]*/,:message/*char[32000]*/)",
    plistParameters,
    "insert request",
    ( 0 < strSessionId.length() ) ? ( &strSessionId ) : NULL );

	if (pmcBuf) {
		fd_cleanup_buffer (pmcBuf);
	}
}

fd_hook_hdl *psoHookHandle = NULL;

int pcrf_tracer_init (void)
{
	int iRetVal = 0;

	CHECK_FCT (fd_hook_register (HOOK_MASK (HOOK_MESSAGE_RECEIVED, HOOK_MESSAGE_SENT), pcrf_tracer, NULL, NULL, &psoHookHandle));

  g_psoPeerStat = stat_get_branch("peer stat");
  g_psoReqStat = stat_get_branch("req stat");

	return iRetVal;
}

void pcrf_tracer_fini (void)
{
	if (psoHookHandle) {
		CHECK_FCT_DO (fd_hook_unregister (psoHookHandle), );
	}
}
