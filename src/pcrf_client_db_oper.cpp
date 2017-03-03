#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <stdio.h>

extern CLog *g_pcoLog;
extern SStat *g_psoDBStat;

int pcrf_client_db_refqueue (otl_connect &p_coDBConn, std::vector<SRefQueue> &p_vectQueue)
{
	int iRetVal = 0;
	SRefQueue soQueueElem;
  CTimeMeasurer coTM;

	otl_nocommit_stream coStream;
	try {
		/* создаем объект класса потока ДБ */
		coStream.open(
			1000,
			"select rowid, identifier, identifier_type, action from ps.refreshQueue where module = 'pcrf' and refresh_date < sysdate",
			p_coDBConn);
		/* делаем выборку из БД */
		while (! coStream.eof ()) {
			coStream
				>> soQueueElem.m_strRowId
				>> soQueueElem.m_strIdentifier
				>> soQueueElem.m_strIdentifierType
				>> soQueueElem.m_coAction;
			p_vectQueue.push_back (soQueueElem);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

  stat_measure(g_psoDBStat, __FUNCTION__, &coTM);

	return iRetVal;
}

void pcrf_client_db_fix_staled_sess ( otl_value<std::string> &p_coSessionId )
{
	/* закрываем зависшую сессию */
  otl_value<otl_datetime> coSysdate;
  otl_value<std::string> coTermCause;

  pcrf_fill_otl_datetime( coSysdate, NULL );

  pcrf_db_update_session( p_coSessionId , coSysdate, coSysdate, coTermCause);
	/* фиксируем правила зависшей сессиии */
  pcrf_db_close_session_rule_all( p_coSessionId );
	/* фиксируем локации зависшей сессиии */
  pcrf_server_db_close_user_loc( p_coSessionId );
}

int pcrf_client_db_load_session_list(otl_connect &p_coDBConn, SRefQueue &p_soReqQueue, std::vector<std::string> &p_vectSessionList)
{
	int iRetVal = 0;
  CTimeMeasurer coTM;

	/* очищаем список перед выполнением */
	p_vectSessionList.clear ();

	otl_nocommit_stream coStream;

	try {
		std::string strSessionId;
		if (0 == p_soReqQueue.m_strIdentifierType.compare("subscriber_id")) {
			coStream.open (
				10,
				"select session_id from ps.sessionList where subscriber_id = :subscriber_id/*char[64]*/ and time_end is null",
				p_coDBConn);
			coStream
				<< p_soReqQueue.m_strIdentifier;
			while (!coStream.eof()) {
				coStream
					>> strSessionId;
				p_vectSessionList.push_back (strSessionId);
			}
			coStream.close();
		} else if (0 == p_soReqQueue.m_strIdentifierType.compare("session_id")) {
			p_vectSessionList.push_back(p_soReqQueue.m_strIdentifier);
		} else {
			UTL_LOG_F(*g_pcoLog, "unsupported identifier type: '%s'", p_soReqQueue.m_strIdentifierType.c_str());
			iRetVal = -1;
		}
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

  stat_measure(g_psoDBStat, __FUNCTION__, &coTM);

	return iRetVal;
}

int pcrf_client_db_delete_refqueue (otl_connect &p_coDBConn, SRefQueue &p_soRefQueue)
{
  if (0 == p_soRefQueue.m_strRowId.length()) {
    return 0;
  }

	int iRetVal = 0;
  CTimeMeasurer coTM;

	otl_nocommit_stream coStream;
	try {
		coStream.open (
			1,
			"delete from ps.refreshQueue where rowid = :row_id /*char[256]*/",
			p_coDBConn);
		coStream
			<< p_soRefQueue.m_strRowId;
		coStream.flush ();
		p_coDBConn.commit ();
		coStream.close();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

  stat_measure(g_psoDBStat, __FUNCTION__, &coTM);

	return iRetVal;
}
