#include <pthread.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <poll.h>
#include <string>
#include <vector>

#include "freeDiameter/extension.h"
#include "pcrf_ipc.h"
#include "utils/pspacket/pspacket.h"
#include "pcrf_session_cache.h"
#include "pcrf_tracer.h"
#include "utils/log/log.h"

struct SIPCReqOperator {
  pthread_mutex_t m_mutexIPCSetData;
  pthread_mutex_t m_mutexIPCOperData;
  pthread_t       m_thrdThread;
  uint8_t        *m_pmui8Data;
  ssize_t         m_stDataLen;
  sockaddr_in     m_soAddr;
  int             m_iSock;
  int             m_iIsInitialized;
  volatile int    m_iLetsWork;
  void SetTask( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr );
  SIPCReqOperator();
  ~SIPCReqOperator();
};

struct SNode {
  DiamId_t  m_diamid;
  size_t		m_diamid_len;
  DiamId_t	m_diamrlm;
  size_t		m_diamrlm_len;
  std::string m_addr;
  uint16_t m_port;
};

#pragma pack(push,1)
struct SPayloadHdr {
  uint16_t m_uiVendId;
  uint16_t m_uiAVPId;
  uint16_t m_uiPadding;
  uint16_t m_uiPayloadLen;
};
#pragma pack(pop)

extern CLog *g_pcoLog;
static int g_iNodeCount;
static int g_iReqOperCount;

/* список нод */
static std::vector<SNode> g_vectNodeList;
/* поток для обработки входящих команд */
static pthread_t    g_thrdSessionCacheReceiver;
static std::string  g_strLocalIPAddress = "0.0.0.0";
static uint16_t     g_uiLocalPort = 7777;

/* обработчики запросов */
static SIPCReqOperator *g_pmsoIPCOper;

static void * pcrf_ipc_receiver( void * );
#define POLL_TIMEOUT 100

static int g_send_sock = -1;

static volatile int g_iIPCWord;

static void * pcrf_ipc_supply_processor( void *p_pvParam );
static int pcrf_ipc_process_request( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr );
static void pcrf_ipc_select_request_operator( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr );

/* загрузка списка нод */
static int pcrf_ipc_init_node();
static void pcrf_ipc_fini_node();

int pcrf_ipc_init()
{
  g_iIPCWord = 1;
  /* загрузка списка нод */
  CHECK_FCT( pcrf_ipc_init_node() );
  /* инициализация пула обработчиков команд */
  g_iReqOperCount = g_iNodeCount * 4 + 1;
  g_pmsoIPCOper = new SIPCReqOperator[ g_iReqOperCount ];
  for ( int i = 0; i < g_iReqOperCount; ++i ) {
    if ( 3 == g_pmsoIPCOper[ i ].m_iIsInitialized ) {
    } else {
      return EINVAL;
    }
  }
  /* создаем поток для обработки входящих команд */
  CHECK_FCT( pthread_create( &g_thrdSessionCacheReceiver, NULL, pcrf_ipc_receiver, NULL ) );
}

void pcrf_ipc_fini()
{
  g_iIPCWord = 0;
  if ( 0 != g_thrdSessionCacheReceiver ) {
    CHECK_FCT_DO( pthread_join( g_thrdSessionCacheReceiver, NULL ), /* continue */ );
  }
  /* освобождаем ресурсы нод */
  pcrf_ipc_fini_node();
  /**/
  if ( NULL != g_pmsoIPCOper) {
    delete[ ]g_pmsoIPCOper;
    g_pmsoIPCOper = NULL;
  }
}

static void pcrf_ipc_select_request_operator( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr )
{
  static int iOperatorIndex = 0;

  if ( g_iReqOperCount > iOperatorIndex ) {
  } else {
    iOperatorIndex %= g_iReqOperCount;
  }

  g_pmsoIPCOper[ iOperatorIndex ].SetTask( p_pmui8Buf, p_stMsgLen, p_iSock, p_psoAddr );
  ++iOperatorIndex;
}

static void * pcrf_ipc_supply_processor( void *p_pvParam )
{
  SIPCReqOperator *psoIPCOper = reinterpret_cast< SIPCReqOperator* >( p_pvParam );

  while ( 0 != psoIPCOper->m_iLetsWork ) {
    if ( 0 == pthread_mutex_lock( &psoIPCOper->m_mutexIPCOperData ) ) {
      if ( 0 != psoIPCOper->m_stDataLen ) {
        pcrf_ipc_process_request( psoIPCOper->m_pmui8Data, psoIPCOper->m_stDataLen, psoIPCOper->m_iSock, &psoIPCOper->m_soAddr );
        psoIPCOper->m_stDataLen = 0;
      }
      pthread_mutex_unlock( &psoIPCOper->m_mutexIPCSetData );
    } else {
      usleep( 10 );
    }
  }

  pthread_exit( NULL );
}

static void pcrf_ipc_ps_reply(
  const int p_iSock,
  const sockaddr_in *p_psoAddr,
  const uint16_t p_uiReqType,
  const uint32_t p_uiReqNum,
  const int p_iResCode,
  const char *p_pszMessage = NULL,
  const uint16_t p_ui16MsgSize = 0 )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  CPSPacket coPSPack;
  uint8_t muiBuf[ 1024 ];
  int iReqLen;
  int iResultCode = p_iResCode;
  int iFnRes;

  CHECK_FCT_DO( coPSPack.Init( reinterpret_cast<SPSRequest *>( muiBuf ), sizeof( muiBuf ), p_uiReqNum, p_uiReqType ), return );
  CHECK_FCT_DO( 0 >= ( iReqLen = coPSPack.AddAttr( reinterpret_cast< SPSRequest * >( muiBuf ), sizeof( muiBuf ), PS_RESULT, &iResultCode, sizeof( iResultCode ) ) ), return );
  if ( NULL != p_pszMessage && 0 != p_ui16MsgSize ) {
    CHECK_FCT_DO( 0 >= ( iReqLen = coPSPack.AddAttr( reinterpret_cast< SPSRequest * >( muiBuf ), sizeof( muiBuf ), PS_DESCR, p_pszMessage, p_ui16MsgSize ) ), return );
  }
  if ( 0 < iReqLen ) {
    CHECK_FCT_DO( 0 >= ( iFnRes = sendto( p_iSock, muiBuf, iReqLen, 0, reinterpret_cast< sockaddr* >( const_cast< sockaddr_in* >( p_psoAddr ) ), sizeof( *p_psoAddr ) ) ), return );
    LOG_N( "%s: operation result: %d", __FUNCTION__, iFnRes );
  }

  LOG_D( "leave: %s", __FUNCTION__ );
}

static int pcrf_ipc_process_request( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr )
{
  LOG_D( "enter: %s", __FUNCTION__ );

  int iRetVal = 0;
  int iFnRes;
  CPSPacket coPSPack;
  uint32_t uiPackNum;
  uint16_t uiReqType;
  uint16_t uiPackLen;
  std::multimap<uint16_t, SPSReqAttrParsed> mmap;
  std::multimap<uint16_t, SPSReqAttrParsed>::iterator iter;
  std::string strSessionId;
  std::string strAdmCmd;
  uint32_t ui32ApplicationId = 0;
  uint8_t *pmucBuf = NULL;
  size_t stBufSize = 0;
  SPayloadHdr *psoPayload;
  SSessionCache *psoCache = new SSessionCache;
  std::string *pstrParentSessionId = NULL;
  std::string *pstrRuleName = NULL;
  std::list<std::string> *plistMonitKey = NULL;

  /* парсинг запроса */
  CHECK_FCT_DO( coPSPack.Parse( reinterpret_cast<const SPSRequest*>( p_pmui8Buf ), static_cast<size_t>( p_stMsgLen ), uiPackNum, uiReqType, uiPackLen, mmap ), goto clean_and_exit );

  LOG_D( "ps req type: %#06x", uiReqType );

  /* обходим все атрибуты запроса */
  for ( iter = mmap.begin(); iter != mmap.end(); ++iter ) {
    LOG_D( "ps attr id: %#06x; ps data len: %u", iter->first, iter->second.m_usDataLen );
    /* проверяем размр буфера */
    if ( stBufSize < iter->second.m_usDataLen ) {
      /* выделяем дополнительную память с запасом на будущее */
      stBufSize = iter->second.m_usDataLen * 2;
      if ( NULL != pmucBuf ) {
        free( pmucBuf );
      }
      pmucBuf = reinterpret_cast<uint8_t*>( malloc( stBufSize ) );
    }
    switch ( iter->first ) {
      case PS_ADMCMD: /* команда администратора */
        strAdmCmd.insert( 0, reinterpret_cast<char*>( iter->second.m_pvData ), static_cast<size_t>( iter->second.m_usDataLen ) );
        break;
      case PS_SUBSCR: /* Subscriber-Id */
        psoCache->m_coSubscriberId.v.insert( 0, reinterpret_cast<char*>( iter->second.m_pvData ), static_cast<size_t>( iter->second.m_usDataLen ) );
        psoCache->m_coSubscriberId.set_non_null();
        break;        /* Subscriber-Id */
      case 1066:  /* Monitoring-Key */
        if ( NULL == plistMonitKey ) {
          plistMonitKey = new std::list<std::string>;
        }
        {
          std::string strMonitKey;

          strMonitKey.insert( 0, reinterpret_cast<char*>( iter->second.m_pvData ), static_cast<size_t>( iter->second.m_usDataLen ) );
          plistMonitKey->push_back( strMonitKey );
        }
        break;    /* Monitoring-Key */
      case PCRF_ATTR_AVP:
        psoPayload = reinterpret_cast<SPayloadHdr*>( pmucBuf );
        memcpy( psoPayload, iter->second.m_pvData, iter->second.m_usDataLen );
        LOG_D( "vendor id: %u; avp id: %u; attr len: %u", psoPayload->m_uiVendId, psoPayload->m_uiAVPId, psoPayload->m_uiPayloadLen );
        switch ( psoPayload->m_uiVendId ) {
          case 0:     /* Dimeter */
            switch ( psoPayload->m_uiAVPId ) {
              case 8:   /* Framed-IP-Address */
                psoCache->m_coFramedIPAddr.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coFramedIPAddr.set_non_null();
                break;  /* Framed-IP-Address */
              case 30:  /* Called-Station-Id */
                psoCache->m_coCalledStationId.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coCalledStationId.set_non_null();
                break;  /* Called-Station-Id */
              case 263: /* Session-Id */
                strSessionId.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                break;  /* Session-Id */
              case 258: /* Auth-Application-Id */
                if ( sizeof( ui32ApplicationId ) == psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) ) {
                  ui32ApplicationId = *reinterpret_cast<int32_t*>( reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ) );
                } else {
                  /* invalid data size */
                }
                break;  /* Auth-Application-Id */
              case 264: /* Origin-Host */
                psoCache->m_coOriginHost.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coOriginHost.set_non_null();
                break;  /* Origin-Host */
              case 296: /* Origin-Realm */
                psoCache->m_coOriginRealm.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coOriginRealm.set_non_null();
                break;  /* Origin-Realm */
              default:
                LOG_N( "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_uiVendId, psoPayload->m_uiAVPId );
            }
            break;    /* Diameter */
          case 10415: /* 3GPP */
            switch ( psoPayload->m_uiAVPId ) {
              case 18:     /* SGSN-MCC-MNC */
                psoCache->m_coSGSNMCCMNC.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coSGSNMCCMNC.set_non_null();
                break;    /* SGSN-IP-Address */
              case 6:     /* SGSN-IP-Address */
                psoCache->m_coSGSNIPAddr.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coSGSNIPAddr.set_non_null();
                break;    /* SGSN-IP-Address */
              case 1027:  /* IP-CAN-Type */
                if ( sizeof( psoCache->m_iIPCANType ) == psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) ) {
                  psoCache->m_iIPCANType = *reinterpret_cast<int32_t*>( reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ) );
                } else {
                  /* invalid data size */
                }
                break;    /* IP-CAN-Type */
              case 1032:  /* RAT-Type */
                if ( sizeof( psoCache->m_iRATType ) == psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) ) {
                  psoCache->m_iRATType = *reinterpret_cast<int32_t*>( reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ) );
                } else {
                  /* invalid data size */
                }
                break;    /* RAT-Type */
              case 1066:  /* Monitoring-Key */
                if ( NULL == plistMonitKey ) {
                  plistMonitKey = new std::list<std::string>;
                }
                {
                  std::string strMonitKey;

                  strMonitKey.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                  plistMonitKey->push_back( strMonitKey );
                }
                break;    /* Monitoring-Key */
              default:
                LOG_N( "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_uiVendId, psoPayload->m_uiAVPId );
            }
            break;    /* 3GPP */
          case 65535: /* Tenet */
            switch ( psoPayload->m_uiAVPId ) {
              case PS_SUBSCR: /* Subscriber-Id */
                psoCache->m_coSubscriberId.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coSubscriberId.set_non_null();
                break;        /* Subscriber-Id */
              case PCRF_ATTR_CGI: /* CGI */
                psoCache->m_coCGI.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coCGI.set_non_null();
                break;            /* CGI */
              case PCRF_ATTR_ECGI:  /* ECGI */
                psoCache->m_coECGI.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coECGI.set_non_null();
                break;              /* ECGI */
              case PCRF_ATTR_TAI:   /* TAI */
                psoCache->m_coTAI.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coTAI.set_non_null();
                break;              /* TAI */
              case PCRF_ATTR_IMEI:  /* IMEI-SV */
                psoCache->m_coIMEISV.v.insert( 0, reinterpret_cast<const char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coIMEISV.set_non_null();
                break;              /* IMEI-SV */
              case PCRF_ATTR_IMSI:  /* End-User-IMSI */
                psoCache->m_coEndUserIMSI.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coEndUserIMSI.set_non_null();
                break;              /* End-User-IMSI */
              case PCRF_ATTR_E164:  /* End-User-E164 */
                psoCache->m_coEndUserE164.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coEndUserE164.set_non_null();
                break;              /* End-User-E164 */
              case PCRF_ATTR_PSES:  /* Parent-Session-Id */
                pstrParentSessionId = new std::string;
                pstrParentSessionId->insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                break;              /* Parent-Session-Id */
              case PCRF_ATTR_IPCANTYPE: /* IP-CAN-Type */
                psoCache->m_coIPCANType.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coIPCANType.set_non_null();
                break;                  /* IP-CAN-Type */
              case PCRF_ATTR_RATTYPE: /* RAT-Type */
                psoCache->m_coRATType.v.insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                psoCache->m_coRATType.set_non_null();
                break;                  /* RAT-Type */
              case PCRF_ATTR_RULNM: /* Rule-Name */
                pstrRuleName = new std::string;
                pstrRuleName->insert( 0, reinterpret_cast<char*>( psoPayload ) + sizeof( *psoPayload ), psoPayload->m_uiPayloadLen - sizeof( *psoPayload ) );
                break;                  /* Rule-Name */
              default:
                LOG_N( "unsupported avp: vendor: '%u'; avp: '%u'", psoPayload->m_uiVendId, psoPayload->m_uiAVPId );
            }
            break;    /* Tenet */
          default:
            LOG_N( "unsupported vendor: '%u'", psoPayload->m_uiVendId );
        }
        break;
    }
  }

  /* выполняем команду */
  switch ( uiReqType ) {
    case PCRF_CMD_INSERT_SESSION:
      pcrf_session_cache_insert_local( strSessionId, psoCache, pstrParentSessionId, pcrf_peer_dialect_ret( psoCache->m_coOriginHost.v, psoCache->m_coOriginRealm.v ) );
      /* объект использован по назначению, больше он нам не понадобится */
      psoCache = NULL;
      break;
    case PCRF_CMD_REMOVE_SESSION:
      if ( 0 == pcrf_session_cache_remove_local( strSessionId ) ) {
      } else {
        usleep( 100 );
        pcrf_session_cache_remove_local( strSessionId );
      }
      pcrf_session_rule_cache_remove_sess_local( strSessionId );
      break;
    case PCRF_CMD_INSERT_SESSRUL:
      if ( NULL != pstrRuleName ) {
        pcrf_session_rule_cache_insert_local( strSessionId, *pstrRuleName );
      }
      break;
    case PCRF_CMD_REMOVE_SESSRUL:
      if ( NULL != pstrRuleName ) {
        pcrf_session_rule_cache_remove_rule_local( strSessionId, *pstrRuleName );
      }
      break;
    case PCRF_CMD_SESS_USAGE:
      iFnRes = pcrf_send_umi_rar( psoCache->m_coSubscriberId, plistMonitKey );
      pcrf_ipc_ps_reply( p_iSock, p_psoAddr, PCRF_CMD_SESS_USAGE, uiPackNum, iFnRes );
      break;
    case PCRF_GET_INFO_FROM_CACHE:
    {
      std::string strResult;

      pcrf_session_cache_get_info_from_cache( strSessionId, strResult );
      pcrf_ipc_ps_reply( p_iSock, p_psoAddr, PCRF_CMD_SESS_USAGE, uiPackNum, 0, strResult.c_str(), strResult.length() );
    }
    break;
    case ADMIN_REQ:
      if ( 0 == strAdmCmd.compare( "tracer.set" ) ) {
        if ( 0 == psoCache->m_coEndUserIMSI.is_null() ) {
          pcrf_tracer_set_condition( m_eIMSI, psoCache->m_coEndUserIMSI.v.c_str() );
        }
        if ( 0 == psoCache->m_coCalledStationId.is_null() ) {
          pcrf_tracer_set_condition( m_eAPN, psoCache->m_coCalledStationId.v.c_str() );
        }
        if ( 0 == psoCache->m_coEndUserE164.is_null() ) {
          pcrf_tracer_set_condition( m_eE164, psoCache->m_coEndUserE164.v.c_str() );
        }
        if ( 0 != ui32ApplicationId ) {
          pcrf_tracer_set_condition( m_eApplicationId, &ui32ApplicationId );
        }
        if ( 0 != strSessionId.length() ) {
          pcrf_tracer_set_session_id( strSessionId.c_str() );
        }
      } else if ( 0 == strAdmCmd.compare( "tracer.reset" ) ) {
        if ( 0 == psoCache->m_coEndUserIMSI.is_null() ) {
          pcrf_tracer_reset_condition( m_eIMSI, psoCache->m_coEndUserIMSI.v.c_str() );
        }
        if ( 0 == psoCache->m_coCalledStationId.is_null() ) {
          pcrf_tracer_reset_condition( m_eAPN, psoCache->m_coCalledStationId.v.c_str() );
        }
        if ( 0 == psoCache->m_coEndUserE164.is_null() ) {
          pcrf_tracer_reset_condition( m_eIMSI, psoCache->m_coEndUserE164.v.c_str() );
        }
        if ( 0 != ui32ApplicationId ) {
          pcrf_tracer_reset_condition( m_eApplicationId, &ui32ApplicationId );
        }
        if ( 0 != strSessionId.length() ) {
          pcrf_tracer_reset_session_id( strSessionId.c_str() );
        }
      } else {
        LOG_N( "unsupported admin command: %s", strAdmCmd.c_str() );
      }
      break;
    default:
      LOG_N( "unsupported command: '%#x'", uiReqType );
  }

  clean_and_exit:
  if ( NULL != pmucBuf ) {
    free( pmucBuf );
  }
  if ( NULL != pstrParentSessionId ) {
    delete pstrParentSessionId;
  }
  if ( NULL != pstrRuleName ) {
    delete pstrRuleName;
  }
  /* если объект не понадобился уничтожаем его */
  if ( NULL != psoCache ) {
    delete psoCache;
  }
  if ( NULL != plistMonitKey ) {
    delete plistMonitKey;
  }

  LOG_D( "leave: %s", __FUNCTION__ );

  return iRetVal;
}

static void * pcrf_ipc_receiver( void * )
{
  int iRecvSock;
  uint8_t *pmucBuf = NULL;
  sockaddr_in soAddr;
  socklen_t stAddrLen;
  pollfd soPollFD;
  const size_t stBufSize = 0x10000;
  ssize_t stRecv;

  /* создаем сокет для получения запросов */
  iRecvSock = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( -1 != iRecvSock ) {
  } else {
    goto clean_and_exit;
  }

  /* привязываемся к локальному адресу */
  soAddr.sin_family = AF_INET;
  if ( 0 != inet_aton( g_strLocalIPAddress.c_str(), &soAddr.sin_addr ) ) {
  } else {
    UTL_LOG_E( *g_pcoLog, "inet_aton can not convert local address: '%s'", g_strLocalIPAddress.c_str() );
    goto clean_and_exit;
  }
  soAddr.sin_port = htons( g_uiLocalPort );
  CHECK_FCT_DO( bind( iRecvSock, reinterpret_cast<sockaddr*>( &soAddr ), sizeof( soAddr ) ), goto clean_and_exit );

  pmucBuf = reinterpret_cast<uint8_t*> ( malloc( stBufSize ) );
  while ( 0 != g_iIPCWord ) {
    soPollFD.fd = iRecvSock;
    soPollFD.events = POLLIN;
    soPollFD.revents = 0;
    if ( 1 == poll( &soPollFD, 1, POLL_TIMEOUT ) ) {
      if ( soPollFD.revents | POLLIN ) {
        stAddrLen = sizeof( soAddr );
        stRecv = recvfrom( iRecvSock, pmucBuf, stBufSize, 0, reinterpret_cast< sockaddr* >( &soAddr ), &stAddrLen );
        if ( sizeof( soAddr ) >= stAddrLen ) {
          /* в этом случае мы имеем корректно сформированный адрес пира */
          /* использование inet_ntoa в этом случае не опасно, т.к. обрабатываем по одному пакету за цикл */
        }
        if ( 0 < stRecv ) {
          pcrf_ipc_select_request_operator( pmucBuf, stRecv, iRecvSock, &soAddr );
        }
      }
    }
  }

  clean_and_exit:
  if ( -1 != iRecvSock ) {
    shutdown( iRecvSock, SHUT_RDWR );
    close( iRecvSock );
  }
  if ( NULL != pmucBuf ) {
    free( pmucBuf );
  }

  pthread_exit( NULL );
}

static int pcrf_ipc_init_node()
{
  g_send_sock = socket( AF_INET, SOCK_DGRAM, 0 );
  if ( -1 != g_send_sock ) {
  } else {
    return errno;
  }

  otl_connect *pcoDBConn = NULL;
  if ( 0 == pcrf_db_pool_get( &pcoDBConn, __FUNCTION__, USEC_PER_SEC ) && NULL != pcoDBConn ) {
  } else {
    return -1;
  }

  int iRepeat = 1;

  sql_repeat:

  try {
    SNode soNode;
    otl_value<std::string>  coHost;
    otl_value<std::string>  coReal;
    otl_value<std::string>  coIPAddress;
    otl_value<uint32_t>     coPort;

    otl_nocommit_stream coStream;
    coStream.open(
      10,
      "select host_name, realm, ip_address, port from ps.node",
      *pcoDBConn );
    while ( ! coStream.eof() ) {
      coStream
        >> coHost
        >> coReal
        >> coIPAddress
        >> coPort;
      UTL_LOG_D( *g_pcoLog, "node information loaded: host: '%s'; realm: '%s'; ip-address: '%s'; port: '%u'", coHost.v.c_str(), coReal.v.c_str(), coIPAddress.v.c_str(), coPort.v );
      if ( ! coHost.is_null() && coHost.v.length() == fd_g_config->cnf_diamid_len && 0 == coHost.v.compare( 0, fd_g_config->cnf_diamid_len, reinterpret_cast<const char*>( fd_g_config->cnf_diamid ) )
        && ! coReal.is_null() && coReal.v.length() == fd_g_config->cnf_diamrlm_len && 0 == coReal.v.compare( 0, fd_g_config->cnf_diamrlm_len, reinterpret_cast<const char*>( fd_g_config->cnf_diamrlm ) ) ) {
        if ( ! coIPAddress.is_null() ) {
          g_strLocalIPAddress = coIPAddress.v;
        }
        if ( ! coPort.is_null() ) {
          g_uiLocalPort = static_cast<uint16_t>( coPort.v );
        }
        UTL_LOG_D( *g_pcoLog, "home node detected: set local ip-address to '%s'; set local port to '%u'", g_strLocalIPAddress.c_str(), g_uiLocalPort );
        continue;
      }
      if ( ! coHost.is_null() ) {
        soNode.m_diamid = strdup( coHost.v.c_str() );
        soNode.m_diamid_len = coHost.v.length();
      } else {
        soNode.m_diamid = NULL;
        soNode.m_diamid_len = 0;
      }
      if ( ! coReal.is_null() ) {
        soNode.m_diamrlm = strdup( coReal.v.c_str() );
        soNode.m_diamrlm_len = coReal.v.length();
      } else {
        soNode.m_diamrlm = NULL;
        soNode.m_diamrlm_len = 0;
      }
      if ( ! coIPAddress.is_null() ) {
        soNode.m_addr = coIPAddress.v;
      } else {
        soNode.m_addr = "";
      }
      if ( ! coPort.is_null() ) {
        soNode.m_port = coPort.v;
      } else {
        soNode.m_port = 0;
      }
      g_vectNodeList.push_back( soNode );
    }
    coStream.close();
    g_iNodeCount = g_vectNodeList.size();
  } catch ( otl_exception &coExept ) {
    UTL_LOG_F( *g_pcoLog, "can not load node list: error: '%d'; description: '%s'", coExept.code, coExept.msg );
    if ( 0 != iRepeat && 1 == pcrf_db_pool_restore( pcoDBConn ) ) {
      --iRepeat;
      goto sql_repeat;
    }
  }

  if ( NULL != pcoDBConn ) {
    pcrf_db_pool_rel( pcoDBConn, __FUNCTION__ );
  }

  return 0;
}

static void pcrf_ipc_fini_node()
{
  if ( -1 != g_send_sock ) {
    shutdown( g_send_sock, SHUT_RDWR );
    close( g_send_sock );
    g_send_sock = -1;
  }

  std::vector<SNode>::iterator iter = g_vectNodeList.begin();
  for ( ; iter != g_vectNodeList.end(); ++iter ) {
    if ( NULL != iter->m_diamid ) {
      free( iter->m_diamid );
      iter->m_diamid = NULL;
    }
    if ( NULL != iter->m_diamrlm ) {
      free( iter->m_diamrlm );
      iter->m_diamrlm = NULL;
    }
  }
}

static int pcrf_session_cache_fill_payload( SPayloadHdr *p_psoPayload, size_t p_stMaxSize, uint16_t p_uiVendId, uint16_t p_uiAVPId, const void *p_pvData, uint16_t p_uiDataLen )
{
  if ( p_uiDataLen + sizeof( *p_psoPayload ) <= p_stMaxSize ) {
    /* все в порядке */
  } else {
    /* размера буфера не достаточно */
    LOG_E( "%s: data length: %d; buffer size; %u; vendor id: %u; avp id: %u", __FUNCTION__, p_uiDataLen, p_stMaxSize, p_uiVendId, p_uiAVPId );
    return EINVAL;
  }
  p_psoPayload->m_uiVendId = p_uiVendId;
  p_psoPayload->m_uiAVPId = p_uiAVPId;
  p_psoPayload->m_uiPadding = 0;
  p_psoPayload->m_uiPayloadLen = p_uiDataLen + sizeof( *p_psoPayload );
  memcpy( reinterpret_cast<char*>( p_psoPayload ) + sizeof( *p_psoPayload ), p_pvData, p_uiDataLen );

  return 0;
}

static int pcrf_session_cache_fill_pspack(
  char *p_pmcBuf,
  const size_t p_stBufSize,
  const std::string &p_strSessionId,
  const SSessionCache *p_psoSessionInfo,
  const uint16_t p_uiReqType,
  const std::string *p_pstrOptionalParam )
{
  int iRetVal = 0;
  CPSPacket ps_pack;
  static uint32_t uiReqNum = 0;
  SPayloadHdr *pso_payload_hdr;
  char mc_attr[ 256 ];
  uint16_t uiVendId;
  uint16_t uiAVPId;

  CHECK_FCT( ps_pack.Init( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, uiReqNum, p_uiReqType ) );

  /* связываем заголовок с буфером */
  pso_payload_hdr = reinterpret_cast<SPayloadHdr*>( mc_attr );

  /* добавляем SessionId */
  CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), 0, 263, p_strSessionId.data(), p_strSessionId.length() ) );
  if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
  } else {
    return -1;
  }

  if ( static_cast<uint16_t>( PCRF_CMD_INSERT_SESSION ) == p_uiReqType ) {
    if ( NULL != p_psoSessionInfo ) {
    } else {
      return -1;
    }
    const otl_value<std::string> *pco_field;
    /* добавляем Subscriber-Id */
    pco_field = &p_psoSessionInfo->m_coSubscriberId;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PS_SUBSCR;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем Framed-IP-Address */
    pco_field = &p_psoSessionInfo->m_coFramedIPAddr;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 0;
      uiAVPId = 8;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем Called-Station-Id */
    pco_field = &p_psoSessionInfo->m_coCalledStationId;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 0;
      uiAVPId = 30;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем IP-CAN-Type */
    uiVendId = 10415;
    uiAVPId = 1027;
    CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, &p_psoSessionInfo->m_iIPCANType, sizeof( p_psoSessionInfo->m_iIPCANType ) ) );
    if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
    } else {
      return -1;
    }
    /* добавляем IP-CAN-Type (в текстовом виде) */
    pco_field = &p_psoSessionInfo->m_coIPCANType;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IPCANTYPE;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем SGSN-MCC-MNC */
    pco_field = &p_psoSessionInfo->m_coSGSNMCCMNC;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 10415;
      uiAVPId = 18;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем SGSN-IP-Address */
    pco_field = &p_psoSessionInfo->m_coSGSNIPAddr;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 10415;
      uiAVPId = 6;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем RAT-Type */
    uiVendId = 10415;
    uiAVPId = 1032;
    CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, &p_psoSessionInfo->m_iRATType, sizeof( p_psoSessionInfo->m_iRATType ) ) );
    if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
    } else {
      return -1;
    }
    /* добавляем RAT-Type (в текстовом виде)*/
    pco_field = &p_psoSessionInfo->m_coRATType;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_RATTYPE;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем Origin-Host */
    pco_field = &p_psoSessionInfo->m_coOriginHost;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 0;
      uiAVPId = 264;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем Origin-Realm */
    pco_field = &p_psoSessionInfo->m_coOriginRealm;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 0;
      uiAVPId = 296;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем CGI */
    pco_field = &p_psoSessionInfo->m_coCGI;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_CGI;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем ECGI */
    pco_field = &p_psoSessionInfo->m_coECGI;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_ECGI;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем TAI */
    pco_field = &p_psoSessionInfo->m_coTAI;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_TAI;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем IMEI-SV */
    pco_field = &p_psoSessionInfo->m_coIMEISV;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IMEI;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем End-User-IMSI */
    pco_field = &p_psoSessionInfo->m_coEndUserIMSI;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_IMSI;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем End-User-E164 */
    pco_field = &p_psoSessionInfo->m_coEndUserE164;
    if ( 0 == pco_field->is_null() ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_E164;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, pco_field->v.data(), pco_field->v.length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
    /* добавляем Parent-Session-Id (по необходимости) */
    if ( NULL != p_pstrOptionalParam ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_PSES;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, p_pstrOptionalParam->data(), p_pstrOptionalParam->length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    }
  } else if ( static_cast<uint16_t>( PCRF_CMD_REMOVE_SESSION ) == p_uiReqType ) {
    /* для удаления сессии достаточно лишь SessionId*/
  } else if ( static_cast<uint16_t>( PCRF_CMD_INSERT_SESSRUL ) == p_uiReqType
    || static_cast<uint16_t>( PCRF_CMD_REMOVE_SESSRUL ) == p_uiReqType ) {
    /* для выполнения этой операции необходим атрибут Rule-Name */
    if ( NULL != p_pstrOptionalParam ) {
      uiVendId = 65535;
      uiAVPId = PCRF_ATTR_RULNM;
      CHECK_FCT( pcrf_session_cache_fill_payload( pso_payload_hdr, sizeof( mc_attr ), uiVendId, uiAVPId, p_pstrOptionalParam->data(), p_pstrOptionalParam->length() ) );
      if ( 0 < ( iRetVal = ps_pack.AddAttr( reinterpret_cast<SPSRequest*>( p_pmcBuf ), p_stBufSize, PCRF_ATTR_AVP, pso_payload_hdr, pso_payload_hdr->m_uiPayloadLen ) ) ) {
      } else {
        return -1;
      }
    } else {
      return -1;
    }
  } else {
    UTL_LOG_E( *g_pcoLog, "unsupported command type: '%#x", static_cast<uint32_t>( p_uiReqType ) );
    return -1;
  }

  return iRetVal;
}

void pcrf_ipc_cmd2remote( const std::string &p_strSessionId, const SSessionCache *p_psoSessionInfo, const uint16_t p_uiCmdType, const std::string *p_pstrOptionalParam )
{
  char mc_buf[ 4096 ];
  int iPayloadLen;
  std::vector<SNode>::iterator iter;

  iPayloadLen = pcrf_session_cache_fill_pspack( mc_buf, sizeof( mc_buf ), p_strSessionId, p_psoSessionInfo, p_uiCmdType, p_pstrOptionalParam );
  if ( 0 < iPayloadLen ) {
  } else {
    return;
  }

  /* собираем пакет данных для запроса */
  ssize_t sent;
  sockaddr_in soAddr;
  /* обходим все ноды */
  for ( iter = g_vectNodeList.begin(); iter != g_vectNodeList.end(); ++iter ) {
    soAddr.sin_family = AF_INET;
    if ( 0 == inet_aton( iter->m_addr.c_str(), &soAddr.sin_addr ) ) {
      UTL_LOG_E( *g_pcoLog, "inet_aton can not convert address: '%s'", iter->m_addr.c_str() );
      continue;
    }
    soAddr.sin_port = htons( iter->m_port );
    sent = sendto( g_send_sock, mc_buf, static_cast<ssize_t>( iPayloadLen ), 0, reinterpret_cast<sockaddr*>( &soAddr ), sizeof( soAddr ) );
    if ( iPayloadLen == sent ) {
    } else {
      int i_errno = errno;
      UTL_LOG_E( *g_pcoLog, "sendto: error accurred: '%s'; error code: '%d'", strerror( i_errno ), i_errno );
    }
  }
}

void SIPCReqOperator::SetTask( const uint8_t *p_pmui8Buf, const ssize_t p_stMsgLen, const int p_iSock, const sockaddr_in *p_psoAddr )
{
  if ( 3 == m_iIsInitialized ) {
  } else {
    return;
  }

  if ( 0 == pthread_mutex_lock( &m_mutexIPCSetData ) && 0 != m_iLetsWork ) {
    memcpy( m_pmui8Data, p_pmui8Buf, p_stMsgLen );
    m_stDataLen = p_stMsgLen;
    m_iSock = p_iSock;
    memcpy( &m_soAddr, p_psoAddr, sizeof( m_soAddr ) );

    pthread_mutex_unlock( &m_mutexIPCOperData );
  }
}

SIPCReqOperator::SIPCReqOperator() : m_pmui8Data( new uint8_t[ 0x10000 ] ), m_stDataLen( 0 ), m_iSock( 0 ), m_iIsInitialized( 0 ), m_iLetsWork( 1 )
{
  if ( 0 == pthread_mutex_init( &m_mutexIPCSetData, NULL ) ) {
    m_iIsInitialized = 1;
  }
  if ( 0 == pthread_mutex_init( &m_mutexIPCOperData, NULL ) && 0 == pthread_mutex_lock( &m_mutexIPCOperData ) ) {
    m_iIsInitialized = 2;
  }
  if ( 0 == pthread_create( &m_thrdThread, NULL, pcrf_ipc_supply_processor, this ) ) {
    m_iIsInitialized = 3;
  }
  memset( &m_soAddr, 0, sizeof( m_soAddr ) );
}

SIPCReqOperator::~SIPCReqOperator()
{
  if ( 0 != m_iIsInitialized ) {
    m_iLetsWork = 0;
    if ( 1 <= m_iIsInitialized ) {
      pthread_mutex_unlock( &m_mutexIPCSetData );
      pthread_mutex_destroy( &m_mutexIPCSetData );
    }
    if ( 2 <= m_iIsInitialized ) {
      pthread_mutex_unlock( &m_mutexIPCOperData );
      pthread_mutex_destroy( &m_mutexIPCOperData );
    }
    if ( 3 <= m_iIsInitialized ) {
      pthread_join( m_thrdThread, NULL );
    }
  }
  if ( NULL != m_pmui8Data ) {
    delete[ ]m_pmui8Data;
    m_pmui8Data = NULL;
    m_stDataLen = 0;
  }
  memset( &m_soAddr, 0, sizeof( m_soAddr ) );
}
