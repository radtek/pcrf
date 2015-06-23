#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <signal.h>
#include <pthread.h>
#include <stdio.h>
#include <sys/time.h>
#include <unistd.h>

extern CLog *g_pcoLog;

/* ������������ ��������� ������ �� �� ��������� */
#define DB_REQ_INTERVAL 1

static struct session_handler * g_psoSessionHandler = NULL;
static pthread_mutex_t g_tDBReqMutex;
static pthread_t g_tThreadId = -1;
static int g_iStop = 0;

extern struct SAppPCRFConf *g_psoConf;

struct sess_state {
	struct timespec m_soTS;      /* Time of sending the message */
	char *m_pszSessionId; /* Session-Id */
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque);

/* ��������� ������ �� Re-Auth ��������� */
static void pcrf_client_RAA (void * data, struct msg ** msg)
{
	int iFnRes;
	struct sess_state * mi = NULL;
	struct session * sess;
	struct avp * avp;
	struct avp_hdr * hdr;
	int iRC;

	/* Search the session, retrieve its data */
	{
		int iIsNew;
		CHECK_FCT_DO (fd_msg_sess_get (fd_g_config->cnf_dict, *msg, &sess, &iIsNew), return);
		ASSERT (iIsNew == 0);

		CHECK_FCT_DO (fd_sess_state_retrieve (g_psoSessionHandler, sess, &mi), return);
		ASSERT ((void *) mi == data);
	}

	/* Value of Result Code */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictRC, &avp), return);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), return);
		iRC = hdr->avp_value->i32;
	} else {
		iRC = -1;
	}

	/* ������������ Result-Code */
	switch (iRC) {
	case 5002: /* DIAMETER_UNKNOWN_SESSION_ID */
		CHECK_FCT_DO (iFnRes = pcrf_client_db_fix_staled_sess (mi->m_pszSessionId), /*continue*/);
		break;
	}

	/* Value of Origin-Host */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictOrignHost, &avp), /*continue*/);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), /*continue*/);
	}

	/* Value of Origin-Realm */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictOrignRealm, &avp), /*continue*/);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), /*continue*/);
	}

	/* Free the message */
	CHECK_FCT_DO (fd_msg_free (*msg), /*continue*/);
	*msg = NULL;

	return;
}

/* �������� Re-Auth ��������� */
static int pcrf_client_RAR (
	otl_connect *p_pcoDBConn,
	SMsgDataForDB p_soReqInfo,
	std::vector<SDBAbonRule> &p_vectActiveRules,
	std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;
	struct msg * psoReq = NULL;
	struct avp * psoAVP;
	union avp_value soAVPValue;
	struct sess_state * psoMsgState = NULL, *svg;
	struct session *psoSess = NULL;

	/* Create the request */
	CHECK_FCT_DO (fd_msg_new (g_psoDictRAR, MSGFL_ALLOC_ETEID, &psoReq), goto out);

	{
		struct msg_hdr * psoData;
		CHECK_FCT_DO (fd_msg_hdr (psoReq, &psoData), goto out);
		psoData->msg_appl = 16777238;
	}

	/* ������ ����� ������ */
	{
		int iIsNew;
		CHECK_FCT_DO (fd_sess_fromsid_msg ((uint8_t *) p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str (), p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length (), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str ();
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coSessionId.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO (fd_msg_sess_set (psoReq, psoSess), goto out);
	}

	/* �������� ������ ��� ���������, �������� ��������� ������ (�������) */
	psoMsgState = (sess_state *) malloc (sizeof (struct sess_state));
	if (psoMsgState == NULL) {
		fd_log_debug ("malloc failed: %s", strerror (errno));
		goto out;
	}

	/* Now set all AVPs values */

	/* Set the Auth-Application-Id */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
		soAVPValue.u32 = 16777238;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue ), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.c_str ();
		soAVPValue.os.len  = p_soReqInfo.m_psoSessInfo->m_coOriginHost.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.c_str ();
		soAVPValue.os.len = p_soReqInfo.m_psoSessInfo->m_coOriginRealm.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Re-Auth-Request-Type AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* AUTHORIZE_ONLY */
/*		soAVPValue.u32 = 1; /* AUTHORIZE_AUTHENTICATE */
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Event-Trigger */
	CHECK_FCT_DO(set_ULCh_event_trigger(*(p_soReqInfo.m_psoSessInfo), psoReq), /* continue */);

	/* Usage-Monitoring-Information */
	CHECK_POSIX_DO(pcrf_make_UMI(psoReq, *(p_soReqInfo.m_psoSessInfo), false), /* continue */);

	/* Charging-Rule-Remove */
	psoAVP = pcrf_make_CRR(p_pcoDBConn, &p_soReqInfo, p_vectActiveRules);
	if (psoAVP) {
		/* put 'Charging-Rule-Remove' into request */
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), );
	}

	/* Charging-Rule-Install */
	psoAVP = pcrf_make_CRI (p_pcoDBConn, &p_soReqInfo, p_vectAbonRules, psoReq);
	if (psoAVP) {
		/* put 'Charging-Rule-Install' into request */
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), );
	}

	CHECK_SYS_DO (clock_gettime (CLOCK_REALTIME, &psoMsgState->m_soTS), goto out);
	psoMsgState->m_pszSessionId = strdup (p_soReqInfo.m_psoSessInfo->m_coSessionId.v.c_str ());

	/* Keep a pointer to the session data for debug purpose, in real life we would not need it */
	svg = psoMsgState;

	/* Store this value in the session */
	CHECK_FCT_DO (fd_sess_state_store (g_psoSessionHandler, psoSess, &psoMsgState), goto out);

	/* Send the request */
	CHECK_FCT_DO (fd_msg_send (&psoReq, pcrf_client_RAA, svg), goto out);

out:

	return iRetVal;
}

/* ��������� ������ �� Abort-Session ��������� */
static void pcrf_ASA (void * data, struct msg ** msg)
{
	int iFnRes;
	struct sess_state * mi = NULL;
	struct session * sess;
	struct avp * avp;
	struct avp_hdr * hdr;
	int iRC;

	/* Search the session, retrieve its data */
	{
		int iIsNew;
		CHECK_FCT_DO (fd_msg_sess_get (fd_g_config->cnf_dict, *msg, &sess, &iIsNew), return);
		ASSERT (iIsNew == 0);

		CHECK_FCT_DO (fd_sess_state_retrieve (g_psoSessionHandler, sess, &mi), return);
		TRACE_DEBUG (INFO, "%p %p", mi, data);
		ASSERT ((void *) mi == data);
	}

	/* Value of Result Code */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictRC, &avp), /*continue*/);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), /*continue*/);
		iRC = hdr->avp_value->i32;
	} else {
		iRC = -1;
	}

	/* ������������ Result-Code */
	switch (iRC) {
	case 5002: /* DIAMETER_UNKNOWN_SESSION_ID */
		CHECK_FCT_DO (iFnRes = pcrf_client_db_fix_staled_sess (mi->m_pszSessionId), /*continue*/);
		break;
	}

	/* Value of Origin-Host */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictOrignHost, &avp), /*continue*/);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), /*continue*/);
	}

	/* Value of Origin-Realm */
	CHECK_FCT_DO (fd_msg_search_avp (*msg, g_psoDictOrignRealm, &avp), /*continue*/);
	if (avp) {
		CHECK_FCT_DO (fd_msg_avp_hdr (avp, &hdr), /*continue*/);
	}

	/* Free the message */
	CHECK_FCT_DO (fd_msg_free (*msg), /*continue*/);
	*msg = NULL;

	return;
}

/* �������� Abort-Session ��������� */
static int pcrf_ASR (SSessionInfo &p_soSessInfo)
{
	int iRetVal = 0;
	struct msg * psoReq = NULL;
	struct avp * psoAVP;
	union avp_value soAVPValue;
	struct sess_state * psoMsgState = NULL, *svg;
	struct session *psoSess = NULL;

	/* Create the request */
	CHECK_FCT_DO (fd_msg_new (g_psoDictRAR, MSGFL_ALLOC_ETEID, &psoReq), goto out);

	/* ������ ������������� ���������� */
	{
		struct msg_hdr * psoData;
		CHECK_FCT_DO (fd_msg_hdr (psoReq, &psoData), goto out);
		psoData->msg_appl = 16777238;
	}

	/* ������ ����� ������ */
	{
		int iIsNew;
		CHECK_FCT_DO(fd_sess_fromsid_msg((uint8_t *)p_soSessInfo.m_coSessionId.v.data(), p_soSessInfo.m_coSessionId.v.length(), &psoSess, &iIsNew), goto out);
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionID, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *)p_soSessInfo.m_coSessionId.v.data();
		soAVPValue.os.len = p_soSessInfo.m_coSessionId.v.length();
		CHECK_FCT_DO(fd_msg_avp_setvalue(psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO(fd_msg_avp_add(psoReq, MSG_BRW_FIRST_CHILD, psoAVP), goto out);
		CHECK_FCT_DO(fd_msg_sess_set(psoReq, psoSess), goto out);
	}

	/* �������� ������ ��� �������� ���������� � ��������� ������ (�������) */
	psoMsgState = (sess_state *) malloc (sizeof (struct sess_state));
	if (psoMsgState == NULL) {
		fd_log_debug ("malloc failed: %s", strerror (errno));
		goto out;
	}

	/* Now set all AVPs values */

	/* Set the Auth-Application-Id */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictAuthApplicationId, 0, &psoAVP), goto out);
		soAVPValue.u32 = 16777238;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue ), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Origin-Host & Origin-Realm */
	CHECK_FCT_DO (fd_msg_add_origin (psoReq, 0), goto out);

	/* Set the Destination-Host AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestHost, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginHost.v.c_str ();
		soAVPValue.os.len  = p_soSessInfo.m_coOriginHost.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set the Destination-Realm AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictDestRealm, 0, &psoAVP), goto out);
		soAVPValue.os.data = (uint8_t *) p_soSessInfo.m_coOriginRealm.v.c_str ();
		soAVPValue.os.len = p_soSessInfo.m_coOriginRealm.v.length ();
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Re-Auth-Request-Type AVP */
	{
		CHECK_FCT_DO (fd_msg_avp_new (g_psoDictRARType, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0;
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	/* Set Session-Release-Cause AVP */
	{
		CHECK_FCT_DO(fd_msg_avp_new(g_psoDictSessionReleaseCause, 0, &psoAVP), goto out);
		soAVPValue.u32 = 0; /* UNSPECIFIED_REASON */
		CHECK_FCT_DO (fd_msg_avp_setvalue (psoAVP, &soAVPValue), goto out);
		CHECK_FCT_DO (fd_msg_avp_add (psoReq, MSG_BRW_LAST_CHILD, psoAVP), goto out);
	}

	CHECK_SYS_DO (clock_gettime (CLOCK_REALTIME, &psoMsgState->m_soTS), goto out);
	psoMsgState->m_pszSessionId = strdup (p_soSessInfo.m_coSessionId.v.c_str ());

	/* Keep a pointer to the session data for debug purpose, in real life we would not need it */
	svg = psoMsgState;

	/* Store this value in the session */
	CHECK_FCT_DO (fd_sess_state_store (g_psoSessionHandler, psoSess, &psoMsgState), goto out);

	/* Send the request */
	CHECK_FCT_DO (fd_msg_send (&psoReq, pcrf_client_RAA, svg), goto out);

out:
	return iRetVal;
}

/* �������� ������� ��������� � ��������� */
int pcrf_client_is_any_changes(std::vector<SDBAbonRule> &p_vectActive, std::vector<SDBAbonRule> &p_vectAbonRules)
{
	int iRetVal = 0;

	/* ��������� ������� �������� ������������ ������ */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectActive.begin(); iter != p_vectActive.end(); ++iter) {
		if (!iter->m_bIsRelevant && iter->m_bIsActivated) {
			iRetVal = 1;
			break;
		}
	}

	if (iRetVal)
		return iRetVal;

	/* ��������� ������� ���������� ���������� ������ */
	for (std::vector<SDBAbonRule>::iterator iter = p_vectAbonRules.begin(); iter != p_vectAbonRules.end(); ++iter) {
		if (!iter->m_bIsActivated && iter->m_bIsRelevant) {
			iRetVal = 1;
			break;
		}
	}

	return iRetVal;
}

/* ������� ��������� ������ ������� ���������� ������� */
int pcrf_client_operate_refqueue_record (otl_connect *p_pcoDBConn, SRefQueue &p_soRefQueue)
{
	int iRetVal = 0;
	std::vector<std::string> vectSessionList;

	/* ��������� �� �� ������ ������ �������� */
	CHECK_POSIX(pcrf_client_db_load_session_list(*p_pcoDBConn, p_soRefQueue, vectSessionList));

	/* ������� ��� ������ �������� */
	for (std::vector<std::string>::iterator iterSess = vectSessionList.begin (); iterSess != vectSessionList.end (); ++iterSess) {
		{
			/* �������� � ������ */
			SMsgDataForDB soSessInfo;
			/* ������ ������ ������� �������� */
			std::vector<SDBAbonRule> vectAbonRules;
			/* ������ �������� ������ �������� */
			std::vector<SDBAbonRule> vectActive;

			/* ������������� ��������� �������� ������ ��������� */
			CHECK_POSIX_DO(pcrf_server_DBstruct_init(&soSessInfo), );
			/* ������ ������������ ������ */
			soSessInfo.m_psoSessInfo->m_coSessionId = *iterSess;
			/* ��������� �� �� ���������� � ������ �������� */
			CHECK_POSIX_DO(pcrf_server_db_load_session_info(*p_pcoDBConn, soSessInfo), goto clear_and_continue);
			/* ���������� ������������� ��������� ���� */
			CHECK_POSIX_DO(pcrf_peer_proto(*(soSessInfo.m_psoSessInfo)), goto clear_and_continue);
			/* ���� � ���� action ������ �������� abort_session */
			if (!p_soRefQueue.m_coAction.is_null() && 0 == p_soRefQueue.m_coAction.v.compare("abort_session")) {
				CHECK_POSIX_DO(pcrf_ASR(*(soSessInfo.m_psoSessInfo)), );
				/* ����������� ������*/
				pcrf_server_DBStruct_cleanup(&soSessInfo);
				continue;
			}
			/* ��������� �� �� ������� �������� */
			CHECK_POSIX_DO(pcrf_server_db_abon_rule(*p_pcoDBConn, soSessInfo, vectAbonRules), );
			/* ���� � �������� ��� �������� ������� ��������� ��� ������ */
			if (0 == vectAbonRules.size()) {
				CHECK_POSIX_DO(pcrf_ASR(*(soSessInfo.m_psoSessInfo)), );
				/* ����������� ������*/
				pcrf_server_DBStruct_cleanup(&soSessInfo);
				continue;
			}
			/* ��������� ������ �������� ������ */
			CHECK_POSIX_DO(pcrf_server_db_load_active_rules(*p_pcoDBConn, soSessInfo, vectActive), );
			/* ��������� ������ ������������ ������ */
			CHECK_POSIX_DO(pcrf_server_select_notrelevant_active(*p_pcoDBConn, soSessInfo, vectAbonRules, vectActive), );
			/* ��������� ���������� � ����������� */
			CHECK_POSIX_DO(pcrf_server_db_monit_key(*p_pcoDBConn, *(soSessInfo.m_psoSessInfo)), /* continue */);
			/* ��������� ������� ��������� � ��������� */
			/* ������ ��� ugw */
			if (1 == soSessInfo.m_psoSessInfo->m_uiPeerProto &&
					!pcrf_client_is_any_changes(vectActive, vectAbonRules)) {
				/* ����������� ������*/
				pcrf_server_DBStruct_cleanup(&soSessInfo);
				continue;
			}
			/* �������� RAR-������ */
			CHECK_POSIX_DO(pcrf_client_RAR(p_pcoDBConn, soSessInfo, vectActive, vectAbonRules), );
			/* ����������� ������*/
		clear_and_continue:
			pcrf_server_DBStruct_cleanup(&soSessInfo);
		}
	}

	return iRetVal;
}

/* ������� ������������ ������� ���������� � �� */
static void * pcrf_client_operate_refreshqueue (void *p_pvArg)
{
	int iFnRes;
	struct timeval soCurTime;
	struct timespec soWaitTime;
	otl_connect *pcoDBConn = NULL;

	/* ����������� ������� ����� */
	CHECK_POSIX_DO (gettimeofday (&soCurTime, NULL), return NULL);
	/* ������ ����� ���������� �������� �������� */
	soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL);
	soWaitTime.tv_nsec = 0;
	/* ������� ������ �� ���������� */
	std::vector<SRefQueue> vectQueue;
	std::vector<SRefQueue>::iterator iter;

	while (! g_iStop) {
		/* � ������� ������ ������� ������ ����� ���������� � ��������������� ��������� � ��������� ����� ����������� �� ��������� �������� */
		/* ��� ���������� ������ ������ ������� ������������� �������������� ����� �� ���������� ��������� �������� */
		iFnRes = pthread_mutex_timedlock (&g_tDBReqMutex, &soWaitTime);
		/* ���� ���� ��������� ������ ������� �� ����� */
		if (g_iStop) {
			break;
		}

		/* ���� ������ �� ������� � ��������� ��������� ���� */
		if (ETIMEDOUT != iFnRes) {
			break;
		}

		/* ������ ����� ���������� ������� */
		/* ����������� ������� ����� */
		gettimeofday (&soCurTime, NULL);
		/* ������ ����� ���������� �������� �������� */
		soWaitTime.tv_sec = soCurTime.tv_sec + (g_psoConf->m_iDBReqInterval ? g_psoConf->m_iDBReqInterval : DB_REQ_INTERVAL);
		soWaitTime.tv_nsec = 0;

		/* ����������� ����������� � �� */
		CHECK_POSIX_DO(pcrf_db_pool_get((void**)&(pcoDBConn), __func__), continue);
		/* ������� ������ ���������� ������� */
		CHECK_POSIX_DO(pcrf_client_db_refqueue((*pcoDBConn), vectQueue), goto clear_and_continue);

		for (iter = vectQueue.begin(); iter != vectQueue.end(); ++iter) {
			/* ����������� ������ ������� ���������� ������� */
			CHECK_POSIX_DO (pcrf_client_operate_refqueue_record (pcoDBConn, *iter), continue);
			CHECK_POSIX_DO (pcrf_client_db_delete_refqueue (*(pcoDBConn), *iter), continue);
		}

		clear_and_continue:
		vectQueue.clear();
		/* ���� �� �������� � ������������ ����������� � �� ��� ���� ���������� */
		if (pcoDBConn) {
			pcrf_db_pool_rel(pcoDBConn, __func__);
			pcoDBConn = NULL;
		}
	}

	pthread_exit (0);
}

/* ������������� ������� */
int pcrf_cli_init (void)
{
	/* ���� ������� ���������� ������� �� �������������� */
	if (!g_psoConf->m_iOperateRefreshQueue)
		return 0;

	/* �������� ������ ������ */
	CHECK_FCT (fd_sess_handler_create (&g_psoSessionHandler, sess_state_cleanup, NULL, NULL));

	/* ������������� �������� ��������� � �� */
	CHECK_POSIX (pthread_mutex_init (&g_tDBReqMutex, NULL));
	/* ��������� ������� ����� ��������� ����������� ���� ����� � ��������� �������� */
	CHECK_POSIX (pthread_mutex_lock (&g_tDBReqMutex));

	/* ������ ������ ��� ���������� �������� � �� */
	CHECK_POSIX (pthread_create (&g_tThreadId, NULL, pcrf_client_operate_refreshqueue, NULL));

	return 0;
}

/* ��������������� ������� */
void pcrf_cli_fini (void)
{
	/* ���� ������� ���������� ������� �� �������������� */
	if (!g_psoConf->m_iOperateRefreshQueue)
		return;

	/* ������������ ��������, ������� ������� ��������� ������ */
	if (g_psoSessionHandler) {
		CHECK_FCT_DO (fd_sess_handler_destroy (&g_psoSessionHandler, NULL), /* continue */ );
	}

	/* ������������� ����� ��������� ������� � �� */
	/* ������������� ���� ���������� ������ ������*/
	g_iStop = 1;
	/* ��������� ������� */
	CHECK_POSIX_DO (pthread_mutex_unlock (&g_tDBReqMutex), );
	/* ���� ��������� ������ ������ */
	CHECK_POSIX_DO (pthread_join (g_tThreadId, NULL), );
	/* ������������ ��������, ������� ��������� */
	CHECK_POSIX_DO (pthread_mutex_destroy (&g_tDBReqMutex), );
};

extern "C"
void sess_state_cleanup (struct sess_state * state, os0_t sid, void * opaque)
{
	UTL_LOG_D(*g_pcoLog, "%p:%p:%p", state, sid, opaque);

	if (state->m_pszSessionId) {
		free (state->m_pszSessionId);
		state->m_pszSessionId = NULL;
	}
	if (state) {
		free (state);
	}
}
