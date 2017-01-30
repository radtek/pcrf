# The app_pcrf extension
PROJECT("Rx Diameter Application" C)

SET(CMAKE_BUILD_TYPE debug)

#SET(CMAKE_C_FLAGS "${CMAKE_C_FLAGS} -fmudflapth")

# Parser files
# BISON_FILE(app_pcrf_conf.y)
# FLEX_FILE(app_pcrf_conf.l)

include_directories(/oracle/orahome/rdbms/public)
include_directories(~/project/utils/src)
include_directories(..)

# SET_SOURCE_FILES_PROPERTIES(lex.app_pcrf_conf.c app_pcrf_conf.tab.c PROPERTIES COMPILE_FLAGS "-I ${CMAKE_CURRENT_SOURCE_DIR}" )
# SET_SOURCE_FILES_PROPERTIES(pcrf_client_db_oper.cpp pcrf_db_pool.cpp pcrf_server_db_oper.cpp pcrf_server.cpp pcrf_client.cpp pcrf_avp_oper.cpp pcrf_peer.cpp pcrf_tracer.cpp pcrf_ULI.cpp pcrf_session_cache.cpp PROPERTIES COMPILE_FLAGS "-I /oracle/orahome/rdbms/public" )
# SET_SOURCE_FILES_PROPERTIES(app_pcrf.c pcrf_client_db_oper.cpp pcrf_db_pool.cpp pcrf_server_db_oper.cpp pcrf_server.cpp pcrf_client.cpp pcrf_avp_oper.cpp pcrf_peer.cpp pcrf_tracer.cpp pcrf_ULI.cpp pcrf_session_cache.cpp PROPERTIES COMPILE_FLAGS "-I ~/project/utils/src/" )

# List of source files
SET( PCRF_APP_SRC
	app_rx.h
	app_rx.c
	app_rx_aa.cpp
	app_rx_data_conv.cpp
	app_rx_data_types.h
	app_rx_dict_cache.c
	app_rx_st.cpp
	~/project/utils/src/utils/timemeasurer/timemeasurer.cpp
	~/project/utils/src/utils/log/log.cpp
	~/project/utils/src/utils/stat/stat.cpp
)

# Compile as a module
FD_ADD_EXTENSION(app_rx ${PCRF_APP_SRC})

TARGET_LINK_LIBRARIES(app_rx "-L/oracle/orahome/lib -lclntsh")
#TARGET_LINK_LIBRARIES(app_rx "-L/oracle/orahome/lib -lmudflapth")

####
## INSTALL section ##

INSTALL(TARGETS app_rx
	LIBRARY DESTINATION ${INSTALL_EXTENSIONS_SUFFIX}
	COMPONENT freeDiameter-gx)
