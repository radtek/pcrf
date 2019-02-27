#ifndef __PCRF_PROCERA_H__
#define __PCRF_PROCERA_H__

#include <string>

/* формирование запроса на завершение сессии Procera */
int pcrf_procera_terminate_session( std::string &p_strIPCANSessionId );

#endif /* __PCRF_PROCERA_H__ */
