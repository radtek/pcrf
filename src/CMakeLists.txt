# The app_pcrf extension
PROJECT("PCRF Diameter Application" C)

# Parser files
BISON_FILE(app_pcrf_conf.y)
FLEX_FILE(app_pcrf_conf.l)
SET_SOURCE_FILES_PROPERTIES(lex.app_pcrf_conf.c app_pcrf_conf.tab.c PROPERTIES COMPILE_FLAGS "-I ${CMAKE_CURRENT_SOURCE_DIR}" )
SET_SOURCE_FILES_PROPERTIES(pcrf_client_db_oper.cpp pcrf_db_pool.cpp pcrf_server_db_oper.cpp pcrf_server.cpp pcrf_client.cpp pcrf_avp_oper.cpp pcrf_peer.cpp pcrf_tracer.cpp pcrf_ULI.cpp PROPERTIES COMPILE_FLAGS "-I/oracle/orahome/rdbms/public" )

# List of source files
SET( PCRF_APP_SRC
	app_pcrf.h
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
	timemeasurer.h
	timemeasurer.cpp
	log.h
	log.cpp
	pcrf_tracer.cpp
	pcrf_ULI.cpp
)

# Compile as a module
FD_ADD_EXTENSION(app_pcrf ${PCRF_APP_SRC})

TARGET_LINK_LIBRARIES(app_pcrf "-L/oracle/orahome/lib -lclntsh")

####
## INSTALL section ##

INSTALL(TARGETS app_pcrf
	LIBRARY DESTINATION ${INSTALL_EXTENSIONS_SUFFIX}
	COMPONENT freeDiameter-pcrf)
