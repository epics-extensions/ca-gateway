/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory.
* Copyright (c) 2002 Berliner Speicherring-Gesellschaft fuer Synchrotron-
* Strahlung mbH (BESSY).
* Copyright (c) 2002 The Regents of the University of California, as
* Operator of Los Alamos National Laboratory.
* This file is distributed subject to a Software License Agreement found
* in the file LICENSE that is included with this distribution.
\*************************************************************************/
/* Author: Jim Kowalkowski
 * Date: 2/96 */

#ifndef __GATE_RESOURCES_H
#define __GATE_RESOURCES_H

#define GATE_LOG_FILE       "gateway.log"
#define GATE_SCRIPT_FILE    "gateway.killer"
#define GATE_RESTART_FILE   "gateway.restart"
#define GATE_PV_LIST_FILE   "gateway.pvlist"
#define GATE_PV_ACCESS_FILE "gateway.access"
#define GATE_COMMAND_FILE   "gateway.command"
#define GATE_PUTLOG_FILE    "gateway.putlog"
#define GATE_REPORT_FILE    "gateway.report"

#define GATE_CONNECT_TIMEOUT      1
#define GATE_INACTIVE_TIMEOUT   (60*60*2)
#define GATE_DEAD_TIMEOUT       (60*2)
#define GATE_DISCONNECT_TIMEOUT (60*60*2)
#define GATE_RECONNECT_INHIBIT  (60*5)

#define GATE_REALLY_SMALL    0.0000001
#define GATE_CONNECT_SECONDS 1

#define GATE_MAX_PVNAME_LENGTH 256u
#define GATE_MAX_HOSTNAME_LENGTH 64u
#define GATE_MAX_PVLIST_LINE_LENGTH 1024u


#include <string.h>
#include <stdio.h>

#ifdef _WIN32
#else
#include <sys/time.h>
#endif

#include "cadef.h"
#include "gdd.h"

#include "gateVersion.h"

// Utilities
char *timeStamp(void);
char *timeString(time_t time);
char *getComputerName(void);
void printRecentHistory(void);

class gateAs;

// Parameters to both client and server sides
class gateResources
{
public:
	gateResources(void);
	~gateResources(void);

	int setListFile(const char* file);
	int setAccessFile(const char* file);
	int setCommandFile(const char* file);
	int setPutlogFile(const char* file);
	int setReportFile(const char* file);
	int setUpAccessSecurity(void);
	int setDebugLevel(int level);

	void setReadOnly(void)		{ ro=1; }
	int isReadOnly(void)		{ return ro; }

	void setEventMask(unsigned long mask)
		{
			event_mask=mask;
			event_mask_string[0]='\0';
			if (event_mask & DBE_VALUE) strcat(event_mask_string, "v");
			if (event_mask & DBE_ALARM) strcat(event_mask_string, "a");
			if (event_mask & DBE_LOG)   strcat(event_mask_string, "l");
			if (event_mask & DBE_PROPERTY)   strcat(event_mask_string, "p");
		}
	unsigned long eventMask(void) const		{ return event_mask; }
	const char* eventMaskString(void) const	{ return event_mask_string; }

	void setConnectTimeout(time_t sec)		{ connect_timeout=sec; }
	void setInactiveTimeout(time_t sec)		{ inactive_timeout=sec; }
	void setDeadTimeout(time_t sec)			{ dead_timeout=sec; }
	void setDisconnectTimeout(time_t sec)	{ disconnect_timeout=sec; }
	void setReconnectInhibit(time_t sec)	{ reconnect_inhibit=sec; }

	int debugLevel(void) const			{ return debug_level; }
	time_t connectTimeout(void) const	{ return connect_timeout; }
	time_t inactiveTimeout(void) const	{ return inactive_timeout; }
	time_t deadTimeout(void) const		{ return dead_timeout; }
	time_t disconnectTimeout(void) const	{ return disconnect_timeout; }
	time_t reconnectInhibit(void) const	{ return reconnect_inhibit; }

	const char* listFile(void) const	{ return pvlist_file?pvlist_file:"NULL"; }
	const char* accessFile(void) const	{ return access_file?access_file:"NULL"; }
	const char* commandFile(void) const	{ return command_file?command_file:"NULL"; }
	const char* putlogFile(void) const	{ return putlog_file?putlog_file:"NULL"; }
	const char* reportFile(void) const	{ return report_file?report_file:"NULL"; }

	void setPutlogFp(FILE* fp) { putlogFp = fp; }
	FILE* getPutlogFp(void) const { return putlogFp; }

	void setServerMode(bool mode)        { serverMode=mode; }
	bool getServerMode(void) const       { return serverMode; }

	void setCacheMode(bool mode)		 { cacheMode=mode; }
	bool getCacheMode(void) const	     { return cacheMode; }

	void setArchiveMode(bool mode)		 { archiveMode=mode; }
	bool getArchiveMode(void) const	     { return archiveMode; }

	void setMaxBytes(unsigned long bytes){ maxBytes=bytes; }
	unsigned long getMaxBytes() const	 { return maxBytes; }

	gateAs* getAs(void);
	bool isAsSetUp(void) const { return as?true:false; }

#ifdef WITH_CAPUTLOG
	int setCaPutlogAddress(const char* address);
	const char* caputlogAddress(void) const	{ return caputlog_address?caputlog_address:"NULL"; }
    void setCaPutlogJson(bool json) { caputlog_json = json;}
    const bool caputlogJson(void) const { return caputlog_json; }
    int hasCaPutlogAddress(void) const { return caputlog_address?1:0; }
	int caPutLog_Init(void);
    void caPutLog_Send(const char *user,
                       const char *host,
                       const char *pvname,
                       const gdd *old_value,
                       const gdd *new_value);
	void caPutLog_Term(void);
    void putLog(	   FILE *fp,
    				   const char *user,
                       const char *host,
                       const char *pvname,
                       const gdd *old_value,
                       const gdd *new_value);
#endif

	// here for convenience
	static int appValue;
	static int appEnum;
	static int appAll;
	static int appMenuitem;
	static int appFixed;
	static int appUnits;
	static int appAttributes;
	static int appSTSAckString;

private:
	char *access_file, *pvlist_file, *command_file, *putlog_file, *report_file;
#ifdef WITH_CAPUTLOG
    char *caputlog_address;
    bool caputlog_json;
#endif
	int debug_level, ro;
	bool serverMode;
	bool cacheMode;
	bool archiveMode;
	unsigned long maxBytes;
	unsigned long event_mask;
	char event_mask_string[4];
	time_t connect_timeout,inactive_timeout,dead_timeout;
	time_t disconnect_timeout,reconnect_inhibit;
	gateAs* as;
	FILE *putlogFp;
};

#ifndef GATE_RESOURCE_FILE
extern gateResources* global_resources;
#endif

/* debug macro creation */
#ifdef NODEBUG
#define gateDebug(l,f,v) ;
#define gateDebug0(l,f) ;
#define gateDebug1(l,f,v) ;
#define gateDebug2(l,f,v1,v2) ;
#define gateDebug3(l,f,v1,v2,v3) ;
#define gateDebug4(l,f,v1,v2,v3,v4) ;
#else
#define gateDebug(l,f,v) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f,v); fflush(stderr); }}
#define gateDebug0(l,f) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f); fflush(stderr); } }
#define gateDebug1(l,f,v) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f,v); fflush(stderr); }}
#define gateDebug2(l,f,v1,v2) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f,v1,v2); fflush(stderr); }}
#define gateDebug3(l,f,v1,v2,v3) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f,v1,v2,v3); fflush(stderr); }}
#define gateDebug4(l,f,v1,v2,v3,v4) { if(l<=global_resources->debugLevel()) \
   { fprintf(stderr,f,v1,v2,v3,v4); fflush(stderr); }}
#endif

#endif

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
