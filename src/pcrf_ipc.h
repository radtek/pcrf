#ifndef __PCRF_IPC_H__
#define __PCRF_IPC_H__

#include <inttypes.h>
#include <sys/types.h>
#include <string>

struct SSessionCache;

#ifdef __cplusplus
extern "C" {
#endif

  /* инициализация */
  int pcrf_ipc_init();
  /* деинициализация */
  void pcrf_ipc_fini();

  /* передача данных другим нодам */
  void pcrf_ipc_cmd2remote( const std::string &p_strSessionId, const SSessionCache *p_psoSessionInfo, const uint16_t p_uiCmdType, const std::string *p_pstrOptionalParam );

#ifdef __cplusplus
}
#endif

#endif /* __PCRF_IPC_H__ */
