#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <stdio.h>

extern CLog *g_pcoLog;

int pcrf_client_db_refqueue (otl_connect &p_coDBConn, std::vector<SRefQueue> &p_vectQueue)
{
	int iRetVal = 0;
	SRefQueue soQueueElem;

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

	return iRetVal;
}

int pcrf_client_db_fix_staled_sess (const char *p_pcszSessionId)
{
	int iRetVal = 0;
	otl_connect *pcoDBConn;
	otl_datetime coDateTime;

	/* запрашиваем указатель на объект подключения к БД */
	if (0 == pcrf_db_pool_get(reinterpret_cast<void**>(&pcoDBConn), __FUNCTION__)) {
  } else {
		return -1;
  }

	otl_nocommit_stream coStream;
	try {
		/* создаем объект класса потока ДБ */
		/* закрываем зависшую сессию */
		coStream.open (
			1,
			"update ps.SessionList "
				"set time_end = time_last_req "
				"where session_id = :1/*char[255],in*/ "
				"returning time_last_req into :2/*timestamp,out*/",
			*pcoDBConn);
		/* делаем выборку из БД */
		coStream
			<< p_pcszSessionId;
		coStream
			>> coDateTime;
		if(coStream.good())
			coStream.close ();
		/* фиксируем правила зависшей сессиии */
		coStream.open (
			1,
			"update ps.sessionRule "
				"set time_end = :time_end/*timestamp*/ "
				"where time_end is null and session_id = :session_id/*char[255]*/",
			*pcoDBConn);
		coStream
			<< coDateTime
			<< p_pcszSessionId;
		if (coStream.good())
			coStream.close();
		/* фиксируем локации зависшей сессиии */
		coStream.open (
			1,
			"update ps.sessionLocation "
				"set time_end = :time_end/*timestamp*/ "
				"where time_end is null and session_id = :session_id/*char[255]*/",
			*pcoDBConn);
		coStream
			<< coDateTime
			<< p_pcszSessionId;
		if (coStream.good())
			coStream.close();
		pcoDBConn->commit ();
	} catch (otl_exception &coExcept) {
		UTL_LOG_E(*g_pcoLog, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	/* возвращаем подключение к БД, если оно было получено */
	if (pcoDBConn) {
		pcrf_db_pool_rel(pcoDBConn, __FUNCTION__);
	}

	return iRetVal;
}

int pcrf_client_db_load_session_list (
	otl_connect &p_coDBConn,
	SRefQueue &p_soReqQueue,
	std::vector<std::string> &p_vectSessionList)
{
	int iRetVal = 0;

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

	return iRetVal;
}

int pcrf_client_db_delete_refqueue (otl_connect &p_coDBConn, SRefQueue &p_soRefQueue)
{
  if (0 == p_soRefQueue.m_strRowId.length()) {
    return 0;
  }

	int iRetVal = 0;

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

	return iRetVal;
}
