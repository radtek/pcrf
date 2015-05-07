#include "app_pcrf.h"
#include "app_pcrf_header.h"
#include <stdio.h>

int pcrf_client_db_refqueue (otl_connect &p_coDBConn, std::vector<SRefQueue> &p_vectQueue)
{
	int iRetVal = 0;
	char mcSubscriberId[256];
	SRefQueue soQueueElem;

	otl_stream coStream;
	try {
		/* создаем объект класса потока ДБ */
		coStream.open(
			1000,
			"select rowid, identifier, identifier_type, refresh_date, action from ps.refreshQueue where module = 'pcrf' and refresh_date < sysdate",
			p_coDBConn);
		/* делаем выборку из БД */
		while (! coStream.eof ()) {
			coStream
				>> soQueueElem.m_strRowId
				>> soQueueElem.m_strIdentifier
				>> soQueueElem.m_strIdentifierType
				>> soQueueElem.m_coRefreshDate
				>> soQueueElem.m_coAction;
			p_vectQueue.push_back (soQueueElem);
		}
		coStream.close();
	} catch (otl_exception &coExcept) {
		LOG_E("code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
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
	CHECK_POSIX(pcrf_db_pool_get((void**)&pcoDBConn, __func__));

	otl_stream coStream;
	try {
		/* создаем объект класса потока ДБ */
		coStream.set_commit (0);
		/* закрываем зависшую сессию */
		coStream.open (
			1000,
			"update ps.SessionList "
				"set time_end = time_last_req "
				"where session_id = :1/*char[255],in*/ "
				"returning time_end into :2/*timestamp,out*/",
			*pcoDBConn);
		/* делаем выборку из БД */
		coStream << p_pcszSessionId;
		coStream.flush ();
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
		coStream << coDateTime << p_pcszSessionId;
		pcoDBConn->commit ();
		if (coStream.good())
			coStream.close();
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	/* возвращаем подключение к БД, если оно было получено */
	if (pcoDBConn) {
		pcrf_db_pool_rel(pcoDBConn, __func__);
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

	otl_stream coStream;

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
			LOG_F("unsupported identifier type");
			iRetVal = -1;
		}
	} catch (otl_exception &coExcept) {
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}

int pcrf_client_db_delete_refqueue (otl_connect &p_coDBConn, SRefQueue &p_soRefQueue)
{
	int iRetVal = 0;

	otl_stream coStream;
	try {
		coStream.set_commit (0);
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
		LOG(FD_LOG_ERROR, "code: '%d'; message: '%s'; query: '%s'", coExcept.code, coExcept.msg, coExcept.stm_text);
		iRetVal = coExcept.code;
		if (coStream.good()) {
			coStream.close();
		}
	}

	return iRetVal;
}
