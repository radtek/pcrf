#ifndef __PCRF_TRACER_H__
#define __PCRF_TRACER_H__

enum ETracerConditionType {
  m_eNotInterested = 0,
  m_eIMSI,
  m_eE164,
  m_eApplicationId,
  m_eAPN
};

#ifdef __cplusplus
extern "C" {
#endif

  void pcrf_tracer_set_condition( enum ETracerConditionType p_eTracerConditionType, const void* p_pvValue );
  void pcrf_tracer_reset_condition( enum ETracerConditionType p_eTracerConditionType, const void* p_pvValue );
  void pcrf_tracer_remove_session( const char *p_pszSessionId );

#ifdef __cplusplus
}
#endif

#endif
