/*********************************************************************************************************
* Software License Agreement (BSD License)                                                               *
* Author: Sebastien Decugis <sdecugis@freediameter.net>							 *
*													 *
* Copyright (c) 2013, WIDE Project and NICT								 *
* All rights reserved.											 *
* 													 *
* Redistribution and use of this software in source and binary forms, with or without modification, are  *
* permitted provided that the following conditions are met:						 *
* 													 *
* * Redistributions of source code must retain the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer.										 *
*    													 *
* * Redistributions in binary form must reproduce the above 						 *
*   copyright notice, this list of conditions and the 							 *
*   following disclaimer in the documentation and/or other						 *
*   materials provided with the distribution.								 *
* 													 *
* * Neither the name of the WIDE Project or NICT nor the 						 *
*   names of its contributors may be used to endorse or 						 *
*   promote products derived from this software without 						 *
*   specific prior written permission of WIDE Project and 						 *
*   NICT.												 *
* 													 *
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED *
* WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A *
* PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR *
* ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT 	 *
* LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS 	 *
* INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR *
* TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF   *
* ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.								 *
*********************************************************************************************************/

/* Yacc extension's configuration parser.
 * See doc/app_pcrf.conf.sample for configuration file format
 */

/* For development only : */
%debug 
%error-verbose

/* The parser receives the configuration file filename as parameter */
%parse-param {char * conffile}

/* Keep track of location */
%locations 
%pure-parser

%{
#include "app_pcrf.h"
#include "app_pcrf_conf.tab.h"	/* bison is not smart enough to define the YYLTYPE before including this code, so... */

#include <string.h>
#include <errno.h>

/* Forward declaration */
int yyparse (char * conffile);

/* Parse the configuration file */
int app_pcrf_conf_handle (char * conffile)
{
	extern FILE * app_pcrf_confin;
	int ret;

	TRACE_ENTRY ("%p", conffile);

	TRACE_DEBUG (FULL, "Parsing configuration file: %s...", conffile);

	app_pcrf_confin = fopen (conffile, "r");
	if (app_pcrf_confin == NULL) {
		ret = errno;
		fd_log_debug ("Unable to open extension configuration file %s for reading: %s", conffile, strerror(ret));
		TRACE_DEBUG (INFO, "Error occurred, message logged -- configuration file.");
		return ret;
	}

	ret = yyparse (conffile);

	fclose (app_pcrf_confin);

	if (ret != 0) {
		TRACE_DEBUG (INFO, "Unable to parse the configuration file.");
		return EINVAL;
	}

	return 0;
}

/* The Lex parser prototype */
int app_pcrf_conflex (YYSTYPE *lvalp, YYLTYPE *llocp);

/* Function to report the errors */
void yyerror (YYLTYPE *ploc, char * conffile, char const *s)
{
	TRACE_DEBUG (INFO, "Error in configuration parsing");

	if (ploc->first_line != ploc->last_line)
		fd_log_debug ("%s:%d.%d-%d.%d : %s", conffile, ploc->first_line, ploc->first_column, ploc->last_line, ploc->last_column, s);
	else if (ploc->first_column != ploc->last_column)
		fd_log_debug ("%s:%d.%d-%d : %s", conffile, ploc->first_line, ploc->first_column, ploc->last_column, s);
	else
		fd_log_debug ("%s:%d.%d : %s", conffile, ploc->first_line, ploc->first_column, s);
}

%}

/* Values returned by lex for token */
%union {
	char 		*string;	/* The string is allocated by strdup in lex.*/
	int		 integer;	/* Store integer values */
}

/* In case of error in the lexical analysis */
%token 		LEX_ERROR

/* Key words */
%token 		DB_SERVER
%token 		DB_USER
%token 		DB_PSWD
%token 		DB_DUMMY_REQUEST
%token 		DB_POOL_SIZE
%token 		DB_POOL_WAIT
%token 		DB_REQ_INTERVAL
%token 		OPERATE_REFRESH_QUEUE
%token 		LOOK4STALLEDSESSION
%token 		LOG_FILE_MASK
%token 		TRACE_REQ
%token 		GEN_CDR
%token 		CDR_MASK
%token 		CDR_DIR
%token 		CDR_CMPL_DIR

/* Tokens and types for routing table definition */
/* A (de)quoted string (malloc'd in lex parser; it must be freed after use) */
%token <string>	QSTRING

/* An integer value */
%token <integer> INTEGER


/* -------------------------------------- */
%%

	/* The grammar definition */
conffile:		/* empty grammar is OK */
			| conffile db_server
			| conffile db_user
			| conffile db_pswd
			| conffile db_dummy_request
			| conffile db_pool_size
			| conffile db_pool_wait
			| conffile db_req_interval
			| conffile operate_refresh_queue
			| conffile look4stalledsession
			| conffile log_file_mask
			| conffile trace_req
			| conffile generate_cdr
			| conffile cdr_mask
			| conffile cdr_dir
			| conffile cdr_completed_dir
			;

db_server:		DB_SERVER '=' QSTRING ';'
			{
				free (g_psoConf->m_pszDBServer);
				g_psoConf->m_pszDBServer = $3;
			}
			;

db_user:		DB_USER '=' QSTRING ';'
			{
				free (g_psoConf->m_pszDBUser);
				g_psoConf->m_pszDBUser = $3;
			}
			;

db_pswd:		DB_PSWD '=' QSTRING ';'
			{
				free (g_psoConf->m_pszDBPswd);
				g_psoConf->m_pszDBPswd = $3;
			}
			;

db_dummy_request:		DB_DUMMY_REQUEST '=' QSTRING ';'
			{
				free (g_psoConf->m_pszDBDummyReq);
				g_psoConf->m_pszDBDummyReq = $3;
			}
			;

db_pool_size:		DB_POOL_SIZE '=' INTEGER ';'
			{
				g_psoConf->m_iDBPoolSize = $3;
			}
			;

db_pool_wait:		DB_POOL_WAIT '=' INTEGER ';'
			{
				g_psoConf->m_iDBPoolWait = $3;
			}
			;

db_req_interval:		DB_REQ_INTERVAL '=' INTEGER ';'
			{
				g_psoConf->m_iDBReqInterval = $3;
			}
			;

operate_refresh_queue:		OPERATE_REFRESH_QUEUE '=' INTEGER ';'
			{
				g_psoConf->m_iOperateRefreshQueue = $3;
			}
			;

look4stalledsession:		LOOK4STALLEDSESSION '=' INTEGER ';'
			{
				g_psoConf->m_iLook4StalledSession = $3;
			}
			;

log_file_mask:		LOG_FILE_MASK '=' QSTRING ';'
			{
				free (g_psoConf->m_pszLogFileMask);
				g_psoConf->m_pszLogFileMask = $3;
			}
			;

trace_req:		TRACE_REQ '=' INTEGER ';'
			{
				g_psoConf->m_iTraceReq = $3;
			}
			;

generate_cdr:		GEN_CDR '=' INTEGER ';'
			{
				g_psoConf->m_iGenerateCDR = $3;
			}
			;

cdr_mask:		CDR_MASK '=' QSTRING ';'
			{
				free (g_psoConf->m_pszCDRMask);
				g_psoConf->m_pszCDRMask = $3;
			}
			;

cdr_dir:		CDR_DIR '=' QSTRING ';'
			{
				free (g_psoConf->m_pszCDRComplDir);
				g_psoConf->m_pszCDRDir = $3;
			}
			;

cdr_completed_dir:		CDR_CMPL_DIR '=' QSTRING ';'
			{
				free (g_psoConf->m_pszCDRComplDir);
				g_psoConf->m_pszCDRComplDir = $3;
			}
			;
