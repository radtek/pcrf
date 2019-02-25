# The app_pcrf extension
PROJECT("PCRF Diameter Application" C)

#SET(CMAKE_BUILD_TYPE DebugValgrind)
SET(CMAKE_CXX_FLAGS "--std=c++0x")

# Parser files
BISON_FILE(app_pcrf_conf.y)
FLEX_FILE(app_pcrf_conf.l)

include_directories(/oracle/orahome/rdbms/public)
include_directories(~/project/utils/src)
include_directories(..)

SET_SOURCE_FILES_PROPERTIES(lex.app_pcrf_conf.c app_pcrf_conf.tab.c PROPERTIES COMPILE_FLAGS "-I ${CMAKE_CURRENT_SOURCE_DIR}" )
SET_SOURCE_FILES_PROPERTIES(pcrf_client_db_oper.cpp pcrf_db_pool.cpp pcrf_server_db_oper.cpp pcrf_server.cpp pcrf_client.cpp pcrf_avp_oper.cpp pcrf_peer.cpp pcrf_tracer.cpp pcrf_ULI.cpp pcrf_session_cache.cpp PROPERTIES COMPILE_FLAGS "-I /oracle/orahome/rdbms/public" )
SET_SOURCE_FILES_PROPERTIES(app_pcrf.c pcrf_client_db_oper.cpp pcrf_db_pool.cpp pcrf_server_db_oper.cpp pcrf_server.cpp pcrf_client.cpp pcrf_avp_oper.cpp pcrf_peer.cpp pcrf_tracer.cpp pcrf_ULI.cpp pcrf_session_cache.cpp pcrf_rule_cache.cpp PROPERTIES COMPILE_FLAGS "-I ~/project/utils/src/" )

# List of source files
SET( PCRF_APP_SRC
	app_pcrf.c
	lex.app_pcrf_conf.c
	app_pcrf_conf.tab.c
	app_pcrf_conf.tab.h
	app_pcrf_dict.cpp
	pcrf_server.cpp
	pcrf_client.cpp
	pcrf_client_db_oper.cpp
	pcrf_db_pool.cpp
	pcrf_server_db_oper.cpp
	pcrf_avp_oper.cpp
	pcrf_peer.cpp
	pcrf_tracer.cpp
	pcrf_ULI.cpp
	pcrf_session_cache.cpp
	pcrf_rule_cache.cpp
	pcrf_session_rule_cache.cpp
	pcrf_sql_queue.cpp
	pcrf_umi_rar.cpp
	pcrf_cdr.cpp
	pcrf_session_cache_index.cpp
	pcrf_linked_session.cpp
	pcrf_crc.cpp
	pcrf_ipc.cpp
	~/project/utils/src/utils/timemeasurer/timemeasurer.cpp
	~/project/utils/src/utils/log/log.cpp
	~/project/utils/src/utils/stat/stat.cpp
	~/project/utils/src/utils/pspacket/pspacket.cpp
)

# Compile as a module
FD_ADD_EXTENSION(app_pcrf ${PCRF_APP_SRC})

TARGET_LINK_LIBRARIES(app_pcrf "-L/oracle/orahome/lib -lclntsh")

####
## INSTALL section ##

INSTALL(TARGETS app_pcrf
	LIBRARY DESTINATION ${INSTALL_EXTENSIONS_SUFFIX}
	COMPONENT freeDiameter-pcrf)
