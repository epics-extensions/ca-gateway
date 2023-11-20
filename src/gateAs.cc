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

/*+*********************************************************************
 *
 * File:       gateAs.cc
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

#define DEBUG_DELAY 0
#define DEBUG_AS 0
#define DEBUG_ACCESS 0

#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>

#ifdef _WIN32
# define strcasecmp stricmp
# include <string.h>
#else
# include <unistd.h>
# include <string.h>
#endif

#include "tsSLList.h"
#include <epicsVersion.h>
#include <osiSock.h>

#include "gateAs.h"
#include "gateResources.h"

void gateAsCa(void);
void gateAsCaClear(void);

const char* gateAs::default_group = "DEFAULT";
const char* gateAs::default_pattern = "*";
unsigned char gateAs::eval_order = GATE_ALLOW_FIRST;

aitBool gateAs::rules_installed = aitFalse;
aitBool gateAs::use_default_rules = aitFalse;
FILE* gateAs::rules_fd = NULL;

// extern "C" wrappers needed for callbacks
extern "C" {
	static void clientCallback(ASCLIENTPVT p, asClientStatus s) {
		gateAsClient::clientCallback(p, s);
	}
	static int readFunc(char* buf, int max_size) {
		return gateAs::readFunc(buf, max_size);
	}
}

// pattern  PV name pattern (regex)
// realname Real name substitution pattern
// asg      ASG
// asl      ASL
gateAsEntry::gateAsEntry(const char* pattern, const char* realname, const char* asg,
  int asl) :
	pattern(pattern),
	alias(realname),
	group(asg),
	level(asl),
	asmemberpvt(NULL)
{
#ifdef USE_PCRE
	pat_buff = NULL;
	ovector = NULL;
	ovecsize = 0;
#else
	// Set pointers in the pattern buffer
	pat_buff.buffer=NULL;
	pat_buff.translate=NULL;
	pat_buff.fastmap=NULL;

	// Initialize registers
	re_set_registers(&pat_buff,&regs,0,0,0);
#endif
}

gateAsEntry::~gateAsEntry(void)
{
	// Free allocated stuff in the pattern buffer
#ifdef USE_PCRE
	pcre_free(pat_buff);
	pcre_free(ovector);
#else
	regfree(&pat_buff);
	// Free allocted stuff in registers
	if(regs.start) free(regs.start);
	if(regs.end) free(regs.end);
	regs.num_regs=0;
#endif
}

long gateAsEntry::removeMember(void)
{
	long rc=0;
	if(asmemberpvt) {
		rc=asRemoveMember(&asmemberpvt);
		if(rc == S_asLib_clientsExist) {
			printf("Cannot remove member because client exists for:\n");
			printf(" %-30s %-16s %d ",pattern,group,level);
		}
		asmemberpvt=NULL;
	}
	return rc;
}

void gateAsEntry::getRealName(const char* pv, char* rname, int len)
{
	char c;
	int in, ir, j, n;

	if (alias) {  // Build real name from substitution pattern
		ir = 0;
		for (in=0; ir<len; in++) {
			if ((c = alias[in]) == '\\') {
				c = alias[++in];
				if(c >= '0' && c <= '9') {
					n = c - '0';
#ifdef USE_PCRE
					if(n < substrings && ovector[2*n] >= 0) {
						for(j=ovector[2*n];
							ir<len && j<ovector[2*n+1];
							j++)
						  rname[ir++] = pv[j];
						if(ir==len)	{
							rname[ir-1] = '\0';
							break;
						}
					}
#else
					if(regs.start[n] >= 0) {
						for(j=regs.start[n];
							ir<len && j<regs.end[n];
							j++)
						  rname[ir++] = pv[j];
						if(ir==len)	{
							rname[ir-1] = '\0';
							break;
						}
					}
#endif
					continue;
				}
			}
			rname[ir++] = c;
			if(c) continue;
			else break;
		}
		if(ir==len) rname[ir-1] = '\0';
		gateDebug4(6,"gateAsEntry::getRealName() PV %s matches %s -> alias %s"
		  " yields real name %s\n",
		  pv, pattern, alias, rname);
	} else {
		// Not an alias: PV name _is_ real name
		strncpy(rname, pv, len);
	}
    return;
}

aitBool gateAsEntry::init(gateAsList& n, int line) {
	if (compilePattern(line)==aitFalse) return aitFalse;
	n.add(*this);
	if (group == NULL || asAddMember(&asmemberpvt,(char*)group) != 0) {
		asmemberpvt = NULL;
	} else {
		asPutMemberPvt(asmemberpvt,this);
	}
	return aitTrue;
}

#ifdef USE_DENYFROM
aitBool gateAsEntry::init(const char* host,	// Host name to deny
  tsHash<gateAsList>& h,                // Where this entry is added to
  gateHostList& hl,				        // Where a new key should be added
  int line) {					        // Line number
	gateAsList* l;

	if(compilePattern(line)==aitFalse) return aitFalse;
	if(h.find(host,l)==0) {
		l->add(*this);
	} else {
		l = new gateAsList;
		l->add(*this);
		h.add(host,*l);
		hl.add(*(new gateAsHost(host)));
	}
	return aitTrue;
}
#endif

aitBool gateAsEntry::compilePattern(int line) {
	const char *err;

#ifdef USE_NEG_REGEXP
        negate_pattern = (pattern[0] == '!');
        if (negate_pattern) pattern++;
#endif

#ifdef USE_PCRE
	int erroffset;
        pat_buff = pcre_compile(pattern, 0, &err, &erroffset, NULL);
	if(!pat_buff)	{
		fprintf(stderr,"Line %d: Error after %d chars in Perl regexp: %s\n",
                    line, erroffset, err);
                fprintf(stderr,"%s\n", pattern);
                fprintf(stderr,"%*c\n", erroffset+1, '^');
		return aitFalse;
	}
        pcre_fullinfo(pat_buff, NULL, PCRE_INFO_CAPTURECOUNT, &ovecsize);
        ovecsize = (ovecsize+1)*3;
        ovector = (int*) pcre_malloc (sizeof(int)*ovecsize);
#else
	pat_buff.translate=0; pat_buff.fastmap=0;
	pat_buff.allocated=0; pat_buff.buffer=0;

    if((err = re_compile_pattern(pattern, (int) strlen(pattern), &pat_buff)))	{
		fprintf(stderr,"Line %d: Error in regexp %s : %s\n", line, pattern, err);
		return aitFalse;
	}
#endif
#ifdef USE_NEG_REGEXP
        if (negate_pattern) pattern--;
#endif
	return aitTrue;
}

gateAsClient::gateAsClient(void) :
	asclientpvt(NULL),
	asentry(NULL),
	user_arg(NULL),
	user_func(NULL)
{
}

gateAsClient::gateAsClient(gateAsEntry *pEntry, const char *user,
  const char *host) :
	asclientpvt(NULL),
	asentry(pEntry),
	user_arg(NULL),
	user_func(NULL)
{
	if(pEntry&&asAddClient(&asclientpvt,pEntry->asmemberpvt,pEntry->level,
		 (char*)user,(char*)host) == 0) {
		asPutClientPvt(asclientpvt,this);
	}

#if DEBUG_DELAY
	gateAsClient *v=this;
	printf("%s gateAsClient::gateAsClient pattern=%s user_func=%d\n",
	  timeStamp(),
	  v->asentry?(v->asentry->pattern?v->asentry->pattern:"[NULL pattern]"):"[NULL entry]",
	  v->user_func);
#endif

	// Callback is called if rights are changed by rereading the
	// access file, etc.  Callback will be called once right now, but
	// won't do anything since the user_func is NULL.  The user_func
	// is set in gateChan::gateChan.
	asRegisterClientCallback(asclientpvt,::clientCallback);

#if DEBUG_DELAY
	printf("%s gateAsClient::gateAsClient finished\n",
	  timeStamp());
#endif
}

gateAsClient::~gateAsClient(void)
{
	// client callback
	if(asclientpvt) asRemoveClient(&asclientpvt);
	asclientpvt=NULL;
}

void gateAsClient::clientCallback(ASCLIENTPVT p, asClientStatus /*s*/)
{
	gateAsClient* pClient = (gateAsClient*)asGetClientPvt(p);
#if DEBUG_DELAY
	printf("%s gateAsClient::clientCallback pattern=%s user_func=%d\n",
	  timeStamp(),
	  pClient->asentry?(pClient->asentry->pattern?
		pClient->asentry->pattern:"[NULL pattern]"):"[NULL entry]",
	  pClient->user_func);
#endif
	if(pClient->user_func) pClient->user_func(pClient->user_arg);
}

gateAs::gateAs(const char* lfile, const char* afile)
#ifdef USE_DENYFROM
	: denyFromListUsed(false)
#endif
{
	// These are static only so they can be used in readFunc.  Be
	// careful about having two instances of this class as when
	// rereading access security.  These variables will be valid for
	// the last instance only.
	default_group = "DEFAULT";
	default_pattern = "*";
	eval_order = GATE_ALLOW_FIRST;
	rules_installed = aitFalse;
	use_default_rules = aitFalse;
	rules_fd = NULL;

	if(afile) {
		if(initialize(afile))
		  fprintf(stderr,"Failed to install access security file %s\n",afile);
	}

	readPvList(lfile);
}

gateAs::~gateAs(void)
{
#ifdef USE_DENYFROM
// Probably OK but not checked for reinitializing all of access
// security including the pvlist.
	tsSLIter<gateAsHost> pi = host_list.firstIter();
    gateAsList * l = NULL;

	gateAsHost *pNode;
	while(pi.pointer())	{
		pNode=pi.pointer();
		deny_from_table.remove(pNode->host,l);
		clearAsList(*l);
		pi++;
	}
	clearHostList(host_list);
#endif

	clearAsList(deny_list);
	clearAsList(allow_list);
	clearAsList(line_list);
}

// Remove the items from the list and delete them
void gateAs::clearAsList(gateAsList& list)
{
	while(list.first()) {
		gateAsEntry *pe=list.get();
		if(pe) {
			// Remove the member from access security
			pe->removeMember();
			delete pe;
		}
	}
}

// Remove the items from the list and delete them
void gateAs::clearAsList(gateLineList& list)
{
	while(list.first()) {
		gateAsLine *pl=list.get();
		if(pl) delete pl;
	}
}
#ifdef USE_DENYFROM
// Remove the items from the list and delete them
void gateAs::clearHostList(gateHostList& list)
{
	while(list.first()) {
		gateAsHost *ph=list.get();
		if(ph) delete ph;
	}
}
#endif

gateAsEntry* gateAs::findEntryInList(const char* pv, gateAsList& list) const
{
	tsSLIter<gateAsEntry> pi = list.firstIter();

	while(pi.pointer()) {
        int len = (int) strlen(pv);
#ifdef USE_PCRE
		pi->substrings=pcre_exec(pi->pat_buff, NULL,
                    pv, len, 0, PCRE_ANCHORED, pi->ovector, 30);
		if((pi->substrings>=0 && pi->ovector[1] == len)
#ifdef USE_NEG_REGEXP
		    ^ pi->negate_pattern
#endif
		) break;
#else
        if((re_match(&pi->pat_buff, pv, len, 0, &pi->regs) == len)
#ifdef USE_NEG_REGEXP
		    ^ pi->negate_pattern
#endif
		) break;
#endif
		pi++;
	}
	return pi.pointer();
}

int gateAs::readPvList(const char* lfile)
{
	int line=0;
	FILE* fd;
	char inbuf[GATE_MAX_PVLIST_LINE_LENGTH];
#ifdef USE_DENYFROM
	char inbufWithIPs[GATE_MAX_PVLIST_LINE_LENGTH];
	char tempInbuf[GATE_MAX_PVLIST_LINE_LENGTH];
#endif
	const char *pattern,*rname,*hname;
	char *cmd,*asg,*asl,*ptr;
	gateAsEntry* pe;
	gateAsLine*  pl;

#ifdef USE_DENYFROM
	denyFromListUsed=false;
#endif

	if(lfile) {
		errno=0;
		fd=fopen(lfile,"r");
		if(fd == NULL) {
			fprintf(stderr,"Failed to open PV list file %s\n",lfile);
			fflush(stderr);
			perror("Reason");
			fflush(stderr);
			return -1;
		}
	} else {
		// Create a ".* allow" rule if no file is specified
		pe = new gateAsEntry(".*",NULL,default_group,1);
		if(pe->init(allow_list,line)==aitFalse) delete pe;

		return 0;
	}

	// Read all PV file lines
	while(fgets(inbuf,sizeof(inbuf),fd)) {
		int lev=1;

		++line;
		pattern=rname=hname=NULL;

#ifdef USE_DENYFROM
		//All deny from rules with host names will be conveted to ip addresses
		strncpy(tempInbuf,inbuf,strlen(inbuf));
		tempInbuf[strlen(inbuf)-1]='\0';

		if((ptr=strchr(inbuf,'#'))) *ptr='\0'; // Take care of comments

		if(!(pattern=strtok(inbuf," \t\n"))) continue;

		if(!(cmd=strtok(NULL," \t\n")))	{
			fprintf(stderr,"Error in PV list file (line %d): "
			  "missing command\n",line);
			continue;
		}
		if(strcasecmp(cmd,"DENY")==0) {
			// Arbitrary number of arguments: [from] host names
			if((hname=strtok(NULL,", \t\n")) && strcasecmp(hname,"FROM")==0)
			  hname=strtok(NULL,", \t\n");
			if(hname) {           // host pattern(s) present
				struct sockaddr_in sockAdd;
				struct sockaddr_in* pSockAdd;
				char hostname[GATE_MAX_HOSTNAME_LENGTH];
				int status;
				char *ch;
				char *pIPInput;

				pSockAdd = &sockAdd;

				strncpy(inbufWithIPs,tempInbuf,hname - inbuf);

				pIPInput = inbufWithIPs + (hname - inbuf);

				do {
					/*convert all host names to ip addresses*/
					status = aToIPAddr(hname,0,pSockAdd);

					if(status != -1)
					{
						ipAddrToDottedIP(pSockAdd,hostname,sizeof(hostname));
						ch=strchr(hostname,':');
						if(ch != NULL) hostname[ch-hostname]=0;

						strncpy(pIPInput,hostname,strlen(hostname));
						*(pIPInput+strlen(hostname)) = ' ';
						pIPInput = pIPInput + strlen(hostname) + 1;
					}
					else{
						fprintf(stderr,"Error in PV list file (line %d): "
			  				"cannot resolve host name >%s<\n",line,hname);
					}

				} while((hname=strtok(NULL,", \t\n")));

				*(pIPInput)='\0';

			}else
			{
				strncpy(inbufWithIPs,tempInbuf,strlen(tempInbuf));
				inbufWithIPs[strlen(tempInbuf)]='\0';
			}

		}else
		{
			strncpy(inbufWithIPs,tempInbuf,strlen(tempInbuf));
			inbufWithIPs[strlen(tempInbuf)]='\0';
		}

		pl=new gateAsLine(inbufWithIPs,strlen(inbufWithIPs),line_list);
#else
		if((ptr=strchr(inbuf,'#'))) *ptr='\0'; // Take care of comments
		pl=new gateAsLine(inbuf,strlen(inbuf),line_list);
#endif
		if(!(pattern=strtok(pl->buf," \t\n"))) continue;

		if(!(cmd=strtok(NULL," \t\n")))	{
			fprintf(stderr,"Error in PV list file (line %d): "
			  "missing command\n",line);
			continue;
		}


#ifdef USE_DENYFROM
		if(strcasecmp(cmd,"DENY")==0) {                          // DENY [FROM]
			// Arbitrary number of arguments: [from] host names
			if((hname=strtok(NULL,", \t\n")) && strcasecmp(hname,"FROM")==0)
			  hname=strtok(NULL,", \t\n");
			if(hname) {           // host pattern(s) present
				do {
					pe = new gateAsEntry(pattern);
					if(pe->init(hname,deny_from_table,host_list,line)==aitFalse) {
						delete pe;
					} else {
						denyFromListUsed=true;
					}
				} while((hname=strtok(NULL,", \t\n")));
			} else {
				// no host name specified
				pe = new gateAsEntry(pattern);
				if(pe->init(deny_list,line)==aitFalse) delete pe;
			}
			continue;
		}
#else
		if(strcasecmp(cmd,"DENY")==0) {                          // DENY [FROM]
			// Arbitrary number of arguments: [from] host names
			if((hname=strtok(NULL,", \t\n")) && strcasecmp(hname,"FROM")==0)
			  hname=strtok(NULL,", \t\n");
			if(hname) {           // host name(s) present
				fprintf(stderr,"Error in PV list file (line %d): "
				  "DENY FROM is not supported\n"
				  "  Use EPICS_CAS_IGNORE_ADDR_LIST instead\n",
				  line);
			} else {
				// no host name specified
				pe = new gateAsEntry(pattern);
				if(pe->init(deny_list,line)==aitFalse) delete pe;
			}
			continue;
		}
#endif

		if(strcasecmp(cmd,"ORDER")==0) {                               // ORDER
			// Arguments: "allow, deny" or "deny, allow"
			if(!(hname=strtok(NULL,", \t\n")) ||
			  !(rname=strtok(NULL,", \t\n"))) {
				fprintf(stderr,"Error in PV list file (line %d): "
				  "missing argument to '%s' command\n",line,cmd);
				continue;
			}
			if(strcasecmp(hname,"ALLOW")==0 &&
			  strcasecmp(rname,"DENY")==0)	{
				eval_order = GATE_ALLOW_FIRST;
			} else if(strcasecmp(hname,"DENY")==0 &&
			  strcasecmp(rname,"ALLOW")==0)	{
				eval_order = GATE_DENY_FIRST;
			} else {
				fprintf(stderr,"Error in PV list file (line %d): "
				  "invalid argument to '%s' command\n",line,cmd);
			}
			continue;
		}

		if(strcasecmp(cmd,"ALIAS")==0) {                     // ALIAS extra arg
			// Additional (first) argument: real PV name
			if(!(rname=strtok(NULL," \t\n"))) {
				fprintf(stderr,"Error in PV list file (line %d): "
				  "missing real name in ALIAS command\n",line);
				continue;
			}
		}

		if((asg=strtok(NULL," \t\n"))) {                           // ASG / ASL
			if((asl=strtok(NULL," \t\n")) &&
			  (sscanf(asl,"%d",&lev)!=1)) lev=1;
		} else {
			asg=(char*)default_group;
			lev=1;
		}

		if(strcasecmp(cmd,"ALLOW")==0   ||                           // ALLOW / ALIAS
		  strcasecmp(cmd,"ALIAS")==0   ||
		  strcasecmp(cmd,"PATTERN")==0 ||
		  strcasecmp(cmd,"PV")==0) {
			pe = new gateAsEntry(pattern,rname,asg,lev);
			if(pe->init(allow_list,line)==aitFalse) delete pe;
			continue;
		} else {
			// invalid
			fprintf(stderr,"Error in PV list file (line %d): "
			  "invalid command '%s'\n",line,cmd);
		}
	}

	fclose(fd);
	return 0;
}

long gateAs::initialize(const char* afile)
{
	long rc=0;

	if(rules_installed==aitTrue) {
		fprintf(stderr,"Access security rules already installed\n");
		return -1;
	}

	if(afile) {
		errno=0;
		rules_fd=fopen(afile,"r");
		if(rules_fd == NULL) {
			// Open failed
			fprintf(stderr,"Failed to open security file: %s\n",afile);
			fflush(stderr);
			perror("Reason");
			fflush(stderr);
			fprintf(stderr,"Setting default security rules\n");
			fflush(stderr);
			use_default_rules=aitTrue;
			rc=asInitialize(::readFunc);
			if(rc) {
				fprintf(stderr,"Failed to set default security rules\n");
				fflush(stderr);
			}
		} else {
			// Open succeeded
			rc=asInitialize(::readFunc);
			if(rc) fprintf(stderr,"Failed to read security file: %s\n",afile);
			fclose(rules_fd);
		}
	} else {
		// afile is NULL
		use_default_rules=aitTrue;
		rc=asInitialize(::readFunc);
		if(rc) fprintf(stderr,"Failed to set default security rules\n");
	}

	if(rc==0) rules_installed=aitTrue;
	return rc;
}

long gateAs::reInitialize(const char* afile, const char* lfile)
{
	// Stop in INP PV clients
	gateAsCaClear();

	// Cleanup
#ifdef USE_DENYFROM
	tsSLIter<gateAsHost> pi = host_list.firstIter();
    gateAsList *l = NULL;

	gateAsHost *pNode;
	while(pi.pointer())	{
		pNode=pi.pointer();
		deny_from_table.remove(pNode->host,l);
		clearAsList(*l);
		pi++;
	}
	clearHostList(host_list);
#endif
	clearAsList(deny_list);
	clearAsList(allow_list);
	clearAsList(line_list);

	// Reset defaults
	default_group = "DEFAULT";
	default_pattern = "*";
	eval_order = GATE_ALLOW_FIRST;
	rules_installed = aitFalse;
	use_default_rules = aitFalse;
	rules_fd = NULL;

	// Reread the access file
	if(afile) {
		if(initialize(afile))
		  fprintf(stderr,"Failed to install access security file %s\n",afile);
	}

	// Restart INP PV clients
	gateAsCa();

	// Reread the pvlist file (Will use defaults if lfile is NULL)
	readPvList(lfile);

	return 0;
}

int gateAs::readFunc(char* buf, size_t max)
{
    size_t l, n;
	static aitBool one_pass=aitFalse;
	static char rbuf[150];
	static char* rptr=NULL;

	if(rptr==NULL) {
		rbuf[0]='\0';
		rptr=rbuf;

		if(use_default_rules==aitTrue) {
			if(one_pass==aitFalse) {
				strcpy(rbuf,"ASG(DEFAULT) { RULE(1,READ) }");
				one_pass=aitTrue;
			} else {
				n=0;
			}
		} else if(fgets(rbuf,sizeof(rbuf),rules_fd)==NULL) {
			n=0;
		}
    }

	l=strlen(rptr);
    n = (l <= max) ? l : max;
	if(n) {
		memcpy(buf,rptr,n);
		rptr+=n;
	}

	if(rptr[0]=='\0')
	  rptr=NULL;

    return (int) n;
}

void gateAs::report(FILE* fd)
{
	time_t t;
	time(&t);

	fprintf(fd,"---------------------------------------------------------------------------\n"
	  "Configuration Report: %s",ctime(&t));
	fprintf(fd,"\n============================ Allowed PV Report ============================\n");
	fprintf(fd," Pattern                        ASG             ASL Alias\n");
	tsSLIter<gateAsEntry> pi1 = allow_list.firstIter();
	gateAsEntry *pEntry1;
	while(pi1.pointer()) {
		pEntry1=pi1.pointer();
		fprintf(fd," %-30s %-16s %d ",pEntry1->pattern,pEntry1->group,pEntry1->level);
		if(pEntry1->alias) fprintf(fd," %s\n",pEntry1->alias);
		else fprintf(fd,"\n");
		pi1++;
	}

	fprintf(fd,"\n============================ Denied PV Report  ============================\n");
	tsSLIter<gateAsEntry> pi2 = deny_list.firstIter();
	gateAsEntry *pEntry2;
	if(pi2.pointer()) {
		fprintf(fd,"\n==== Denied from ALL Hosts:\n");
		while(pi2.pointer()) {
			pEntry2=pi2.pointer();
			fprintf(fd," %s\n",pEntry2->pattern);
			pi2++;
		}
	}

#ifdef USE_DENYFROM
	tsSLIter<gateAsHost> pi3 = host_list.firstIter();
	gateAsHost *pEntry3;
	while(pi3.pointer()) {
		pEntry3=pi3.pointer();
		fprintf(fd,"\n==== Denied from Host %s:\n",pEntry3->host);
		gateAsList* pl=NULL;
		if(deny_from_table.find(pEntry3->host,pl)==0) {
			tsSLIter<gateAsEntry> pi4 = pl->firstIter();
			gateAsEntry *pEntry4;
			while(pi4.pointer()) {
				pEntry4=pi4.pointer();
				fprintf(fd," %s\n",pEntry4->pattern);
				pi4++;
			}
		}
		pi3++;
	}
#endif

	if(eval_order==GATE_DENY_FIRST)
	  fprintf(fd,"\nEvaluation order: deny, allow\n");
	else
	  fprintf(fd,"\nEvaluation order: allow, deny\n");

	if(rules_installed==aitTrue) fprintf(fd,"Access Rules are installed.\n");
	if(use_default_rules==aitTrue) fprintf(fd,"Using default access rules.\n");
	
#if (EPICS_VERSION > 3 || (EPICS_REVISION == 14 && EPICS_MODIFICATION >= 6) || EPICS_REVISION > 14)
	// Dumping to a file pointer became available sometime during 3.14.5.
	fprintf(fd,"\n============================ Access Security Dump =========================\n");
	asDumpFP(fd,NULL,NULL,TRUE);
#else
	// KE: Could use asDump, but it would go to stdout, probably
	// gateway.log, and not be in gateway.report
#endif
	fprintf(fd,"-----------------------------------------------------------------------------\n");
}

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
