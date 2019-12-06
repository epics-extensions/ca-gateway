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

#ifndef _GATEAS_H_
#define _GATEAS_H_

/*+*********************************************************************
 *
 * File:       gateAs.h
 * Project:    CA Proxy Gateway
 *
 * Descr.:     Access Security part - handles all Gateway configuration:
 *             - Reads PV list file
 *             - Reads Access Security file
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 *********************************************************************-*/

#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include <epicsVersion.h>


extern "C" {
#include <ellLib.h>
#include "asLib.h"
#include "errMdef.h"
#include "gpHash.h"
#include "asTrapWrite.h"
}

// KE: Put these here to avoid redefining RE_DUP_MAX as defined in
// regex.h
#include "tsSLList.h"
#include "tsHash.h"
#include "aitTypes.h"

#ifdef USE_PCRE
#include <pcre.h>
#else

extern "C" {
// Patch for regex.h not testing __cplusplus, only __STDC__
// KE: This leaves __STDC_ changed in some cases
#ifndef __STDC__
#    define __STDC__ 1
#    include "regex.h"
#    undef __STDC__
#else
#    if ! __STDC__
#        undef __STDC__
#        define __STDC__ 1
#    endif
#    include "regex.h"
#endif
}

#endif /* USE_PCRE */
/*
 * Standard FALSE and TRUE macros
 */
#ifndef FALSE
#define FALSE 0
#endif
#ifndef TRUE
#define TRUE 1
#endif

#define GATE_DENY_FIRST 0
#define GATE_ALLOW_FIRST 1

// Function prototypes
void gateAsCa(void);
void gateAsCaClear(void);

class gateVcData;
class gateAsEntry;
class gateAsLine;

typedef tsSLList<gateAsEntry> gateAsList;
typedef tsSLList<gateAsLine> gateLineList;

//  ----------------- AS host (to build up host list) ------------------

#ifdef USE_DENYFROM
class gateAsHost;
typedef tsSLList<gateAsHost> gateHostList;
class gateAsHost : public tsSLNode<gateAsHost>
{
public:
	const char* host;

	gateAsHost(void) : host(NULL) { }
	gateAsHost(const char* name) : host(name) { }
};
#endif

//  ------------ AS entry (deny or deny from or alias or allow) ------------

class gateAsEntry : public tsSLNode<gateAsEntry>
{
public:
	gateAsEntry(const char* pattern=NULL, const char* realname=NULL,
	  const char* asg=NULL, int asl=1);
	~gateAsEntry(void);

	aitBool init(gateAsList& n, int line);
#ifdef USE_DENYFROM
	aitBool init(const char* host, tsHash<gateAsList>& h, gateHostList& hl,
	  int line);
#endif
	long removeMember(void);

	void getRealName(const char* pv, char* real, int len);

	const char* pattern;
	const char* alias;
	const char* group;
	int level;
	ASMEMBERPVT asmemberpvt;
#ifdef USE_PCRE
	pcre* pat_buff;
	int substrings;
	int ovecsize;
	int *ovector;
#else
	char pat_valid;
	struct re_pattern_buffer pat_buff;
	struct re_registers regs;
#endif
#ifdef USE_NEG_REGEXP
	bool negate_pattern;
#endif

private:
	aitBool compilePattern(int line);
};

//  -------------- AS node (information for CAS Channels) --------------

class gateAsClient
{
public:
	gateAsClient(void);
	gateAsClient(gateAsEntry *pase, const char *user, const char *host
#ifdef EPICS_HAS_AS_IPAG
                     ,epicsUInt32 ip_addr
#endif
                    );
	~gateAsClient(void);

	aitBool readAccess(void)  const
	  { return (asclientpvt==NULL||asCheckGet(asclientpvt))?aitTrue:aitFalse; }
	aitBool writeAccess(void) const
	  { return (asclientpvt&&asCheckPut(asclientpvt))?aitTrue:aitFalse; }

	gateAsEntry* getEntry(void)
	  { return asentry; }

#ifdef SUPPORT_OWNER_CHANGE
    // Used in virtual function setOwner from casChannel, not called
    // from Gateway.  It is a security hole to support this, and it is
    // no longer implemented in base.
	long changeInfo(const char* user, const char* host
#ifdef EPICS_HAS_AS_IPAG
                        ,epicsUInt32 ip_addr
#endif
                       )
	  { return asChangeClient(asclientpvt,asentry->level, (char*)user ,
                                  (char*)host
#ifdef EPICS_HAS_AS_IPAG
                                  ,ip_addr
#endif
                                  );}
#endif

	const char *user(void) { return (const char*)asclientpvt->user; }
	const char *host(void) { return (const char*)asclientpvt->host; }
	ASCLIENTPVT clientPvt(void) { return asclientpvt; }

	void setUserFunction(void (*ufunc)(void*),void* uarg)
	  { user_arg=uarg; user_func=ufunc; }

private:
	ASCLIENTPVT asclientpvt;
	gateAsEntry* asentry;
	void* user_arg;
	void (*user_func)(void*);

public:
	static void clientCallback(ASCLIENTPVT p, asClientStatus s);
};

class gateAsLine : public tsSLNode<gateAsLine>
{
public:
	gateAsLine(void) : buf(NULL) { }
    gateAsLine(const char* line, size_t len, tsSLList<gateAsLine>& n) :
		buf(new char[len+1])
	{
		strncpy(buf,line,len+1);
		buf[len] = '\0';
		n.add(*this);
	}
	~gateAsLine(void)
		{ delete [] buf; }

	char* buf;
};

class gateAs
{
public:
	gateAs(const char* pvlist_file, const char* as_file=NULL);
	~gateAs(void);

#ifdef USE_DENYFROM
	inline gateAsEntry* findEntry(const char* pv, const char* host = 0);
	bool isDenyFromListUsed(void) const { return denyFromListUsed; }
#else
	inline gateAsEntry* findEntry(const char* pv);
#endif

	int readPvList(const char* pvlist_file);
	void report(FILE*);
	long reInitialize(const char* as_file, const char* pvlist_file);

	// These are static only so they can be used in readFunc callback
	static const char* default_group;
	static const char* default_pattern;

private:
#ifdef USE_DENYFROM
	bool denyFromListUsed;
	tsHash<gateAsList> deny_from_table;
	gateHostList host_list;
#endif
	gateAsList deny_list;
	gateAsList allow_list;
	gateLineList line_list;

	long initialize(const char* as_file);
	void clearAsList(gateAsList& list);
	void clearAsList(gateLineList& list);
#ifdef USE_DENYFROM
	void clearHostList(gateHostList& list);
#endif
	gateAsEntry* findEntryInList(const char* pv, gateAsList& list) const;

	// These are static only so they can be used in readFunc callback
	static unsigned char eval_order;
	static aitBool rules_installed;
	static aitBool use_default_rules;
	static FILE* rules_fd;

public:
    static int readFunc(char* buf, size_t max_size);
};

#ifdef USE_DENYFROM
inline gateAsEntry* gateAs::findEntry(const char* pv, const char* host)
{
	gateAsList* pl=NULL;

	if(host && deny_from_table.find(host,pl)==0 &&	// DENY FROM
	   findEntryInList(pv, *pl)) return NULL;

	if(eval_order == GATE_ALLOW_FIRST &&			// DENY takes precedence
	   findEntryInList(pv, deny_list)) return NULL;

	return findEntryInList(pv, allow_list);
}
#else
inline gateAsEntry* gateAs::findEntry(const char* pv)
{
	if(eval_order == GATE_ALLOW_FIRST &&
	   findEntryInList(pv, deny_list)) return NULL;

	return findEntryInList(pv, allow_list);
}
#endif

#endif /* _GATEAS_H_ */

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
