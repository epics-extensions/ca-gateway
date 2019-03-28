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
// Author: Jim Kowalkowski
// Date: 2/96

#define DEBUG_ENV 0
#define DEBUG_OPENFILES 0

// Use this to truncate the core file if GATEWAY_CORE_SIZE is
// specified in the environment.  (Truncating makes it unusable so
// consider truncation to 0 if anything.)
#define TRUNC_CORE_FILE 1

#define NRESTARTS 10
#define RESTART_INTERVAL 10*60  // sec
#define RESTART_DELAY 10        // sec

#if DEBUG_OPENFILES
#include <limits.h>
#include <unistd.h>
#endif

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <stdlib.h>
#ifdef USE_SYSLOG
#include <syslog.h>
#endif
#include <signal.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <errno.h>

#ifdef _WIN32
# include <direct.h>
# include <process.h>
# define WIN32_MAXSTDIO 2048
# define strcasecmp _stricmp
#else
# include <sys/wait.h>
# include <unistd.h>
# include <sys/resource.h>
#endif

#include <envDefs.h>
#include <osiProcess.h>
#include <gdd.h>
#include <epicsVersion.h>
#include <caProto.h>
#include <errSymTbl.h>
#include "gateResources.h"
#include "gateServer.h"

// Function Prototypes
static int startEverything(char *prefix);
static void stopEverything(void);
static void print_instructions(void);
static int manage_gateway(void);
static int setEnv(const char *var, const char *val, char **envString);
static int setEnv(const char *var, const int ival, char **envString);
static void printEnv(FILE *fp, const char *var);

// Global variables
#ifndef _WIN32
static pid_t gate_pid;
#endif
static int death_flag=0;
static int nStart=0;
static time_t startTime[NRESTARTS];
static gateServer *server=NULL;

// still need to add client and server IP addr info using 
// the CA environment variables.

// still need to add ability to load and run user servers dynamically

#if 0
void* operator new(size_t x)
{
	void* y = (void*)malloc(x);
	fprintf(stderr,"in op new for %d %8.8x\n",(int)x,y);
	return y;
}
void operator delete(void* x)
{
	fprintf(stderr,"in op del for %8.8x\n",x);
	free((char*)x);
}
#endif

// The parameters passed in from the user are:
//	-debug ? = set debug level, ? is the integer level to set
//	-pvlist file_name = process variable list file
//	-log
//	-access file_name = access security file
//	-command file_name = USR1 command list file
//	-putlog file_name = putlog file
//  -caputlog = address (IP:port) for caPutLog server
//	-report file_name = report file
//	-home directory = the program's home directory
//	-connect_timeout number = clear PV connect requests every number seconds
//	-inactive_timeout number = Hold inactive PV connections for number seconds
//	-dead_timeout number = Hold PV connections with no user for number seconds
//	-cip ip_addr_list = CA client library IP address list (exclusive)
//	-sip ip_addr = IP address where CAS listens for requests
//	-signore ip_addr_list = IP address CAS ignores
//	-cport port_number = CA client library port
//	-sport port_number = CAS port number
//	-ro = read only server, no puts allowed
//	-? = display usage
//
//	GATEWAY_HOME = environment variable pointing to the home of the gateway
//
// Defaults:
//	Home directory = .
//	Access security file = gateway.access
//	process variable list file = gateway.pvlist
//	USR1 command list file = gateway.command
//	putlog file = gateway.putlog
//  caputlog address = none, not used
//	report file = gateway.report
//	log file = gateway.log
//	debug level = 0 (none)
//  connect_timeout = 1 second
//  inactive_timeout = 60*60*2 seconds (2 hours)
//  dead_timeout = 60*2 seconds (2 minutes)
//	cport/sport = CA default port number
//	cip = nothing, the normal interface
//	sip = nothing, the normal interface
//
// Precedence:
//	(1) Command line parameter 
//	(2) environment variables
//	(3) defaults

#define PARM_DEBUG             0
#define PARM_PVLIST            1
#define PARM_LOG               2
#define PARM_ACCESS            3
#define PARM_HOME              4
#define PARM_COMMAND           5
#define PARM_PUTLOG            6
#define PARM_REPORT            7
#define PARM_CONNECT           8
#define PARM_INACTIVE          9
#define PARM_DEAD             10
#define PARM_USAGE            11
#define PARM_SERVER_IP        12
#define PARM_CLIENT_IP        13
#define PARM_SERVER_PORT      14
#define PARM_CLIENT_PORT      15
#define PARM_HELP             16
#define PARM_SERVER           17
#define PARM_RO               18
#define PARM_UID              19
#define PARM_PREFIX           20
#define PARM_GID              21
#define PARM_RECONNECT        22
#define PARM_DISCONNECT       23
#define PARM_MASK             24
#define PARM_SERVER_IGNORE_IP 25
#define PARM_CACHE 			  26
#define PARM_ARCHIVE		  27
#ifdef WITH_CAPUTLOG
  #define PARM_CAPUTLOG       28
#endif

#define HOME_DIR_SIZE    300

static char *gate_ca_auto_list=NULL;
static char *gate_beacon_ca_auto_list=NULL;
static char *server_ip_addr=NULL;
static char *server_ignore_ip_addr=NULL;
static char *client_ip_addr=NULL;
static int server_port=0;
static int client_port=0;
static int make_server=0;
static bool cache=1;
static bool archive=0;
static char *home_directory;
static const char *log_file=NULL;
static const char *putlog_file=NULL;
static const char *report_file=NULL;
#ifdef WITH_CAPUTLOG
static const char *caputlog_address=NULL;
#endif
#ifndef _WIN32
static pid_t parent_pid;
#endif

struct parm_stuff
{
	const char* parm;
	int len;
	int id;
	const char* desc;
};
typedef struct parm_stuff PARM_STUFF;

// Second parameter is length of first not including null
static PARM_STUFF ptable[] = {
    { "-debug",               6, PARM_DEBUG,       "value" },
    { "-log",                 4, PARM_LOG,         "file_name" },
    { "-pvlist",              7, PARM_PVLIST,      "file_name" },
    { "-access",              7, PARM_ACCESS,      "file_name" },
    { "-command",             8, PARM_COMMAND,     "file_name" },
    { "-putlog",              7, PARM_PUTLOG,      "file_name" },
#ifdef WITH_CAPUTLOG
    { "-caputlog",            9, PARM_CAPUTLOG,    "address (ip:port)" },
#endif
    { "-report",              7, PARM_REPORT,      "file_name" },
    { "-home",                5, PARM_HOME,        "directory" },
    { "-sip",                 4, PARM_SERVER_IP,   "IP_address" },
    { "-cip",                 4, PARM_CLIENT_IP,   "IP_address_list" },
    { "-signore",             8, PARM_SERVER_IGNORE_IP, "IP_address_list" },
    { "-sport",               6, PARM_SERVER_PORT, "CA_server_port" },
    { "-cport",               6, PARM_CLIENT_PORT, "CA_client_port" },
    { "-connect_timeout",    16, PARM_CONNECT,     "seconds" },
    { "-inactive_timeout",   17, PARM_INACTIVE,    "seconds" },
    { "-dead_timeout",       13, PARM_DEAD,        "seconds" },
    { "-disconnect_timeout", 19, PARM_DISCONNECT,  "seconds" },
    { "-reconnect_inhibit",  18, PARM_RECONNECT,   "seconds" },
    { "-server",              9, PARM_SERVER,      "(start as server)" },
    { "-uid",                 4, PARM_UID,         "user_id_number" },
    { "-gid",                 4, PARM_GID,         "group_id_number" },
    { "-ro",                  3, PARM_RO,          NULL },
    { "-prefix",              7, PARM_PREFIX,      "statistics_prefix" },
    { "-mask",                5, PARM_MASK,        "event_mask" },
	{ "-no_cache",            9, PARM_CACHE,       "(no caching)" },
	{ "-archive",             8, PARM_ARCHIVE,     "archive monitor" },	
    { "-help",                5, PARM_HELP,        NULL },
    { NULL,                  -1, -1,               NULL }
};

extern "C" {

typedef void (*SIG_FUNC)(int);

static SIG_FUNC save_hup = NULL;
static SIG_FUNC save_int = NULL;
static SIG_FUNC save_term = NULL;
static SIG_FUNC save_bus = NULL;
static SIG_FUNC save_ill = NULL;
static SIG_FUNC save_segv = NULL;
static SIG_FUNC save_chld = NULL;

static void sig_end(int sig)
{
	fflush(stdout);
	fflush(stderr);
	
	switch(sig)	{
#ifndef _WIN32
	case SIGHUP:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Ending (SIGHUP)");
#endif
		fprintf(stderr,"%s PV Gateway Ending (SIGHUP)\n",
		  timeStamp());
		if(save_hup) save_hup(sig);
		break;
#endif //#ifndef _WIN32
	case SIGTERM:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Ending (SIGTERM)");
#endif
		fprintf(stderr,"%s PV Gateway Ending (SIGTERM)\n",
		  timeStamp());
		if(save_term) save_term(sig);
		break;
	case SIGINT:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Ending (SIGINT)");
#endif
		fprintf(stderr,"%s PV Gateway Ending (SIGINT)\n",
		  timeStamp());
		if(save_int) save_int(sig);
		break;
	case SIGILL:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Aborting (SIGILL)");
#endif
		fprintf(stderr,"PV Gateway Aborting (SIGILL)\n");
		if(save_ill) save_ill(sig);
		abort();
#ifndef _WIN32
	case SIGBUS:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Aborting (SIGBUS)");
#endif
		fprintf(stderr,"%s PV Gateway Aborting (SIGBUS)\n",
		  timeStamp());
		if(save_bus) save_bus(sig);
		abort();
#endif //#ifndef _WIN32
	case SIGSEGV:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Aborting (SIGSEGV)");
#endif
		fprintf(stderr,"%s PV Gateway Aborting (SIGSEGV)\n",
		  timeStamp());
		if(save_segv) save_segv(sig);
		abort();
	default:
#ifdef USE_SYSLOG
		syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Exiting (Unknown Signal)");
#endif
		fprintf(stderr,"%s PV Gateway Exiting (Unknown Signal)\n",
		  timeStamp());
		break;
	}
	
	// Have it stop itself if possible by setting the quit_flag
	stopEverything();
}

#ifndef _WIN32
static void sig_chld(int /*sig*/)
{
#ifdef SOLARIS
	while(waitpid(-1,NULL,WNOHANG)>0);
#else
	while(wait3(NULL,WNOHANG,NULL)>0);
#endif
	signal(SIGCHLD,sig_chld);
}
#endif //#ifndef _WIN32

#ifndef _WIN32
static void sig_stop(int /*sig*/)
{
	if(gate_pid)
	  kill(gate_pid,SIGTERM);
	
	death_flag=1;
}
#endif //#ifndef _WIN32

} // End extern "C"

static int startEverything(char *prefix)
{
	char *gate_cas_port=NULL;
	char *gate_cas_addr=NULL;
	char *gate_cas_ignore_addr=NULL;
	char *gate_ca_list=NULL;
	char *gate_ca_port=NULL;
	int sid;
#ifndef _WIN32
	FILE* fp;
	struct rlimit lim;
#endif

	if(client_ip_addr) {
        setEnv("EPICS_CA_ADDR_LIST", client_ip_addr, &gate_ca_list);
		// In addition, make EPICS_CA_AUTO_LIST=NO to avoid sending
		// search requests to ourself.  Note that if
		// EPICS_CA_ADDR_LIST is specified instead of -cip, then
		// EPICS_CA_AUTO_ADDR_LIST=NO must be set also as this branch
		// will not be taken.
        setEnv("EPICS_CA_AUTO_ADDR_LIST", "NO", &gate_ca_auto_list);
		/*gjansa: for beacons*/
		char *tempBuff;
		tempBuff = getenv("EPICS_CAS_AUTO_BEACON_ADDR_LIST");
		if(tempBuff != NULL){
			if(strcasecmp(tempBuff,"NO")){
                setEnv("EPICS_CAS_AUTO_BEACON_ADDR_LIST","YES",&gate_beacon_ca_auto_list);
				gateDebug1(15,"gateway setting <%s>\n",gate_beacon_ca_auto_list);
			}
		}
		gateDebug1(15,"gateway setting <%s>\n",gate_ca_auto_list);
		gateDebug1(15,"gateway setting <%s>\n",gate_ca_list);
	}

	if(server_ip_addr) {
		setEnv("EPICS_CAS_INTF_ADDR_LIST",server_ip_addr,
		  &gate_cas_addr);
		gateDebug1(15,"gateway setting <%s>\n",gate_cas_addr);
	}

	if(server_ignore_ip_addr) {
		setEnv("EPICS_CAS_IGNORE_ADDR_LIST",server_ignore_ip_addr,
		  &gate_cas_ignore_addr);
		gateDebug1(15,"gateway setting <%s>\n",gate_cas_ignore_addr);
	}

	if(client_port) {
		setEnv("EPICS_CA_SERVER_PORT",client_port,
		  &gate_ca_port);
		gateDebug1(15,"gateway setting <%s>\n",gate_ca_port);
	}

	if(server_port) {
		setEnv("EPICS_CAS_SERVER_PORT",server_port,
		  &gate_cas_port);
		gateDebug1(15,"gateway setting <%s>\n",gate_cas_port);
	}

	sid=getpid();

#ifdef RESERVE_FOPEN_FD
	// Open a dummy file to keep a file descriptor open to use for
	// fopen to avoid its limit of 256 on Solaris
	if(!global_resources->openReserveFile()) {
		fprintf(stderr,"Opening reserve file failed: %s\n",
		  GATE_RESERVE_FILE);
	}
#endif
	
#ifndef _WIN32
	// Make script file ("gateway.killer" by default)
	errno=0;
	if((fp=fopen(GATE_SCRIPT_FILE,"w"))==(FILE*)NULL) {
		fprintf(stderr,"Opening script file failed: %s\n",
		  GATE_SCRIPT_FILE);
		fflush(stderr);
		perror("Reason");
		fflush(stderr);
		fp=stderr;
	}
	fprintf(fp,"\n");
	fprintf(fp,"# options:\n");
	fprintf(fp,"# home=<%s>\n",home_directory);
	fprintf(fp,"# log file=<%s>\n",log_file);
	fprintf(fp,"# access file=<%s>\n",global_resources->accessFile());
	fprintf(fp,"# pvlist file=<%s>\n",global_resources->listFile());
	fprintf(fp,"# command file=<%s>\n",global_resources->commandFile());
	fprintf(fp,"# putlog file=<%s>\n",global_resources->putlogFile());
#ifdef WITH_CAPUTLOG
	fprintf(fp,"# caputlog address=<%s>\n",global_resources->caputlogAddress());
#endif
	fprintf(fp,"# report file=<%s>\n",global_resources->reportFile());
	fprintf(fp,"# debug level=%d\n",global_resources->debugLevel());
	fprintf(fp,"# dead timeout=%ld\n",global_resources->deadTimeout());
	fprintf(fp,"# connect timeout=%ld\n",global_resources->connectTimeout());
	fprintf(fp,"# disconnect timeout=%ld\n",global_resources->disconnectTimeout());
	fprintf(fp,"# reconnect inhibit time=%ld\n",global_resources->reconnectInhibit());
	fprintf(fp,"# inactive timeout=%ld\n",global_resources->inactiveTimeout());
	fprintf(fp,"# event mask=%s\n",global_resources->eventMaskString());
	fprintf(fp,"# user id=%ld\n",(long)getuid());
	fprintf(fp,"# group id=%ld\n",(long)getgid());
	fprintf(fp,"# caching=%s\n",global_resources->getCacheMode() ? "enabled" : "disabled" );
	fprintf(fp,"# archive monitor=%s\n",global_resources->getArchiveMode() ? "enabled" : "disabled" );	

	// Print command-line arguments
	fprintf(fp,"# \n");
	fprintf(fp,"# environment set from command line:\n");
	if(client_ip_addr) {
		fprintf(fp,"# %s\n",gate_ca_list);
		fprintf(fp,"# %s\n",gate_ca_auto_list);
	}
	if(server_ip_addr) fprintf(fp,"# %s\n",gate_cas_addr);
	if(server_ignore_ip_addr) fprintf(fp,"# %s\n",gate_cas_ignore_addr);
	if(client_port) fprintf(fp,"# %s\n",gate_ca_port);
	if(server_port) fprintf(fp,"# %s\n",gate_cas_port);

	// Print kill information
	fprintf(fp,"# \n");
	fprintf(fp,"# use the following to execute commands in command file:\n");
	fprintf(fp,"#    kill -USR1 %d\n",sid);
	fprintf(fp,"# use the following to get a PV summary report in the log:\n");
	fprintf(fp,"#    kill -USR2 %d\n",sid);

	fprintf(fp,"# \n");

	if(global_resources->isReadOnly()) {
		fprintf(fp,"# Gateway running in read-only mode.\n");
	}

	fprintf(fp,"\n kill %ld # to kill everything\n\n",(long)parent_pid);
	fprintf(fp,"\n # kill %u # to kill off this gateway\n\n",sid);
	fflush(fp);
	
	if(fp!=stderr) fclose(fp);
	chmod(GATE_SCRIPT_FILE,00755);
#endif  //#ifndef _WIN32
	
#ifndef _WIN32
	// Make script file ("gateway.restart" by default)
	errno=0;
	if((fp=fopen(GATE_RESTART_FILE,"w"))==(FILE*)NULL) {
		fprintf(stderr,"Opening restart file failed: %s\n",
		  GATE_RESTART_FILE);
		fflush(stderr);
		perror("Reason");
		fflush(stderr);
		fp=stderr;
	}
	
	fprintf(fp,"\n kill %d # to kill off this gateway\n\n",sid);
	fflush(fp);
	
	if(fp!=stderr) fclose(fp);
	chmod(GATE_RESTART_FILE,00755);
#endif  //#ifndef _WIN32
	
	// Increase process limits to max
#ifdef _WIN32
	// Set open file limit (512 by default, 2048 is max)
# if DEBUG_OPENFILES
    int maxstdio=_getmaxstdio();
    printf("Permitted open files: %d\n",maxstdio);
    printf("\nSetting limits to %d...\n",WIN32_MAXSTDIO);
# endif
  // This will fail and not do anything if WIN32_MAXSTDIO > 2048
    int status=_setmaxstdio(WIN32_MAXSTDIO);
    if(!status) {
		printf("Failed to set STDIO limit\n");
    }
# if DEBUG_OPENFILES
    maxstdio=_getmaxstdio();
    printf("Permitted open files (after): %d\n",maxstdio);
# endif
#else  //#ifdef _WIN32
	// Set process limits
	if(getrlimit(RLIMIT_NOFILE,&lim)<0) {
		fprintf(stderr,"Cannot retrieve the process FD limits\n");
	} else	{
# if DEBUG_OPENFILES
		printf("RLIMIT_NOFILE (before): rlim_cur=%d rlim_rlim_max=%d "
		  "OPEN_MAX=%d SC_OPEN_MAX=%d FOPEN_MAX=%d\n",
		  lim.rlim_cur,lim.rlim_max,
		  OPEN_MAX,_SC_OPEN_MAX,FOPEN_MAX);
		printf("  sysconf: _SC_OPEN_MAX %d _SC_STREAM_MAX %d\n",
		  sysconf(_SC_OPEN_MAX), sysconf(_SC_STREAM_MAX));
# endif
		if(lim.rlim_cur<lim.rlim_max) {
			lim.rlim_cur=lim.rlim_max;
			if(setrlimit(RLIMIT_NOFILE,&lim)<0)
			  fprintf(stderr,"Failed to set FD limit %d\n",
				(int)lim.rlim_cur);
		}
#if DEBUG_OPENFILES
		if(getrlimit(RLIMIT_NOFILE,&lim)<0) {
			printf("RLIMIT_NOFILE (after): Failed\n");
		} else {
			printf("RLIMIT_NOFILE (after): rlim_cur=%d rlim_rlim_max=%d "
			  "OPEN_MAX=%d SC_OPEN_MAX=%d FOPEN_MAX=%d\n",
			  lim.rlim_cur,lim.rlim_max,
			  OPEN_MAX,_SC_OPEN_MAX,FOPEN_MAX);
			printf("  sysconf: _SC_OPEN_MAX %d _SC_STREAM_MAX %d\n",
			  sysconf(_SC_OPEN_MAX), sysconf(_SC_STREAM_MAX));
		}
#endif
	}
	
	if(getrlimit(RLIMIT_CORE,&lim)<0) {
		fprintf(stderr,"Cannot retrieve the process FD limits\n");
	} else {
# if TRUNC_CORE_FILE
		// KE: Used to truncate it to 20000000 if GATEWAY_CORE_SIZE
		// was not specified.  Truncating the core file makes it
		// unusable.  Now only does it if GATEWAY_CORE_SIZE is
		// specified.
		long core_len=0;
		char *core_size=getenv("GATEWAY_CORE_SIZE");
		if(core_size && sscanf(core_size,"%ld",&core_len) == 1) {
			lim.rlim_cur=core_len;
			if(setrlimit(RLIMIT_CORE,&lim) < 0) {
				fprintf(stderr,"Failed to set core limit to %d\n",
				  (int)lim.rlim_cur);
			}
		}
# endif
	}
#endif
	
#ifndef _WIN32
	save_hup=signal(SIGHUP,sig_end);
	save_bus=signal(SIGBUS,sig_end);
#endif
	save_term=signal(SIGTERM,sig_end);
	save_int=signal(SIGINT,sig_end);
	save_ill=signal(SIGILL,sig_end);
	save_segv=signal(SIGSEGV,sig_end);

#ifdef USE_SYSLOG
	syslog(LOG_NOTICE|LOG_DAEMON,"PV Gateway Starting");
#endif

	// Write file headers
	// Output
	printf("%s %s [%s %s]\n",
	  timeStamp(),GATEWAY_VERSION_STRING,__DATE__,__TIME__);
#ifndef _WIN32
	if(global_resources->getServerMode()) {
		printf("%s PID=%d ServerPID=%d\n",EPICS_VERSION_STRING,
		  sid,parent_pid);
	} else {
		printf("%s PID=%d\n",EPICS_VERSION_STRING,sid);
	}
#else
	printf("%s PID=%d\n",EPICS_VERSION_STRING,sid);
#endif
	printf("CA Protocol version %s\n", ca_version());
	printEnv(stdout,"EPICS_CA_ADDR_LIST");
	printEnv(stdout,"EPICS_CA_AUTO_ADDR_LIST");
	printEnv(stdout,"EPICS_CA_SERVER_PORT");
	printEnv(stdout,"EPICS_CA_MAX_ARRAY_BYTES");
	printEnv(stdout,"EPICS_CAS_INTF_ADDR_LIST");
	printEnv(stdout,"EPICS_CAS_SERVER_PORT");
	printEnv(stdout,"EPICS_CAS_IGNORE_ADDR_LIST");
	printEnv(stdout,"EPICS_CAS_AUTO_BEACON_ADDR_LIST");
	printEnv(stdout,"EPICS_CAS_BEACON_ADDR_LIST");

	// Get user name
	char userName[21];
	osiGetUserNameReturn ret=osiGetUserName(userName,21);
	if(ret != osiGetUserNameSuccess) {
		strcpy(userName,"Unknown");
	}
	userName[20]='\0';
	
	// Get host name
	char *hostName=getComputerName();
	if(!hostName) hostName=strDup("Unknown");
	printf("Running as user %s on host %s\n",userName,hostName);
	delete [] hostName;

	// Put log
	FILE *putFp=global_resources->getPutlogFp();
	if(putFp) {
		fprintf(putFp,"%s %s [%s %s]\n",
		  timeStamp(),GATEWAY_VERSION_STRING,__DATE__,__TIME__);	  
		fprintf(putFp,"%s PID=%d\n",EPICS_VERSION_STRING,sid);
		fprintf(putFp,"Attempted Writes:\n");
		fflush(putFp);
	}

#if DEBUG_ENV
	system("printenv | grep EPICS");
	fflush(stdout); fflush(stderr);
#endif
    // start caPutLog in global_resources, if user defined a caPutLog ip:port address
#ifdef WITH_CAPUTLOG
    if (global_resources->hasCaPutlogAddress()) {
  	  int caputlog_status = global_resources->caPutLog_Init();
      if (caputlog_status != 0) {
        fprintf(stderr,"%s global_resources caPutLogInit failed, aborting\n",timeStamp());
        fflush(stdout);
        fflush(stderr);
        if (server) {
          delete server;
          server = NULL;
        }
        return 1;
      }
    }
#endif

	// Start the gateServer
	try {
		server = new gateServer(prefix);
	} catch(int status) {
		fprintf(stderr,"%s Failed to start gateServer, aborting\n",
		  timeStamp());
		char name[256];
		name[0]='\0';
		if(status == 0) status=errno;
		if(status > 0) {
			errSymLookup(status,name,sizeof(name));
		}
		if(name[0]) fprintf(stderr,"  Reason: %s\n",name);
		else fprintf(stderr,"  Reason: Not available\n");
		fflush(stdout); fflush(stderr);
		if(server) {
			delete server;
			server=NULL;
		}
		return 1;
	} catch(...) {
		fprintf(stderr,"%s Failed to start gateServer, aborting\n",
		  timeStamp());
		fflush(stdout); fflush(stderr);
		if(server) {
			delete server;
			server=NULL;
		}
		return 1;
	}
	server->mainLoop();
	delete server;
	server = NULL;

	return 0;
}

int main(int argc, char** argv)
{
#ifndef _WIN32
	uid_t uid;
	gid_t gid;
#endif
	int i,j,k;
	int not_done=1;
	int no_error=1;
	int level=0;
	int read_only=0;
	unsigned long mask=0;
	int connect_tout=-1;
	int inactive_tout=-1;
	int dead_tout=-1;
	int disconnect_tout=-1;
	int reconnect_tinhib=-1;
	char* home_dir=NULL;
	char* pvlist_file=NULL;
	char* access_file=NULL;
	char* command_file=NULL;
	char* stat_prefix=NULL;
	time_t t;
#ifndef _WIN32
	char logSaveFile[1024];  // Should use MAX_PATH or PATH_MAX
	char putlogSaveFile[1024];  // Should use MAX_PATH or PATH_MAX
	struct stat sbuf;
#endif

	home_dir=getenv("GATEWAY_HOME");
	home_directory=new char[HOME_DIR_SIZE];

	// Parse command line
	for(i=1;i<argc && no_error;i++)	{
		for(j=0;not_done && no_error && ptable[j].parm;j++)	{
			if(strncmp(ptable[j].parm,argv[i],ptable[j].len)==0) {
				switch(ptable[j].id) {
				case PARM_DEBUG:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&level)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_MASK:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							for (k=0; argv[i][k]; k++) {
								switch (argv[i][k]) {
								case 'a' :
								case 'A' :
									mask |= DBE_ALARM;
									break;
								case 'v' :
								case 'V' :
									mask |= DBE_VALUE;
									break;
								case 'l' :
								case 'L' :
									mask |= DBE_LOG;
									break;
								case 'p' :
								case 'P' :
									mask |= DBE_PROPERTY;
									break;
								default :
									break;
								}
							}
							not_done=0;
						}
					}
					break;
				case PARM_HELP:
					print_instructions();
					return 0;
				case PARM_SERVER:
					make_server=1;
					not_done=0;
					break;
				case PARM_CACHE:
					cache=0;
					not_done=0;
					break;	
				case PARM_ARCHIVE:
					archive=1;
					not_done=0;
					break;						
				case PARM_RO:
					read_only=1;
					not_done=0;
					break;
#ifndef _WIN32
				case PARM_UID:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",(int *)&uid)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								setuid(uid);
								not_done=0;
							}
						}
					}
					break;
#endif
#ifndef _WIN32
				case PARM_GID:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",(int *)&gid)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								setgid(gid);
								not_done=0;
							}
						}
					}
					break;
#endif
				case PARM_PVLIST:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							pvlist_file=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_LOG:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							log_file=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_COMMAND:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							command_file=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_PUTLOG:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							putlog_file=argv[i];
							not_done=0;
						}
					}
					break;
#ifdef WITH_CAPUTLOG
				case PARM_CAPUTLOG:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							caputlog_address=argv[i];
							not_done=0;
						}
					}
					break;
#endif
				case PARM_REPORT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							report_file=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_ACCESS:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							access_file=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_HOME:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							home_dir=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_SERVER_IP:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							server_ip_addr=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_SERVER_IGNORE_IP:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							server_ignore_ip_addr=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_CLIENT_IP:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							client_ip_addr=argv[i];
							not_done=0;
						}
					}
					break;
				case PARM_CLIENT_PORT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&client_port)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_SERVER_PORT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&server_port)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_DEAD:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&dead_tout)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_INACTIVE:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&inactive_tout)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_CONNECT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&connect_tout)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_DISCONNECT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&disconnect_tout)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_RECONNECT:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							if(sscanf(argv[i],"%d",&reconnect_tinhib)<1) {
								fprintf(stderr,"\nBad value %s for %s\n",
								  argv[i],ptable[j].parm);
								no_error=0;
							} else {
								not_done=0;
							}
						}
					}
					break;
				case PARM_PREFIX:
					if(++i>=argc) no_error=0;
					else {
						if(argv[i][0]=='-') no_error=0;
						else {
							stat_prefix=argv[i];
							not_done=0;
						}
					}
					break;
				default:
					no_error=0;
					fprintf(stderr,"\nBad option: %s\n",argv[i]);
					break;
				}
			}
		}
		not_done=1;
		if(ptable[j].parm==NULL) {
			fprintf(stderr,"\nBad option: %s\n",argv[i]);
			no_error=0;
		}
	}

	// Check if command line was valid
	if(no_error==0)	{
		int ii;

		// Print command line
		fprintf(stderr,"\nBad command line\n");
		for(ii=0; ii < argc; ii++)	{
			fprintf(stderr,"%s ",argv[ii]);
		}
		fprintf(stderr,"\n\n");

		// Print usage
		fprintf(stderr,"Usage: %s followed by the these options:\n",argv[0]);
		for(ii=0; ptable[ii].parm; ii++)
		{
			if(ptable[ii].desc)
				fprintf(stderr,"\t[%s %s ]\n",ptable[ii].parm,ptable[ii].desc);
			else
				fprintf(stderr,"\t[%s]\n",ptable[ii].parm);
		}

		getcwd(home_directory,HOME_DIR_SIZE);

		// Get the default resources. The values of access_file,
		// pvlist_file, command_file, putlog_file, and report_file
		// depend on whether the default filenames exist in the cwd.
		global_resources = new gateResources;
		gateResources* gr = global_resources;

		// Print defaults
		fprintf(stderr,"\nDefaults are:\n");
		fprintf(stderr,"\tdebug=%d\n",gr->debugLevel());
		fprintf(stderr,"\thome=%s\n",
		  home_directory?home_directory:"Not available");
		fprintf(stderr,"\tlog=%s\n",GATE_LOG_FILE);
		fprintf(stderr,"\taccess=%s\n",gr->accessFile());
		fprintf(stderr,"\tpvlist=%s\n",gr->listFile());
		fprintf(stderr,"\tcommand=%s\n",gr->commandFile());
		fprintf(stderr,"\tputlog=%s\n",gr->putlogFile());
#ifdef WITH_CAPUTLOG
		fprintf(stderr,"\tcaputlog=%s\n",gr->caputlogAddress());
#endif
		fprintf(stderr,"\treport=%s\n",gr->reportFile());
		fprintf(stderr,"\tdead=%ld\n",gr->deadTimeout());
		fprintf(stderr,"\tconnect=%ld\n",gr->connectTimeout());
		fprintf(stderr,"\tdisconnect=%ld\n",gr->disconnectTimeout());
		fprintf(stderr,"\treconnect=%ld\n",gr->reconnectInhibit());
		fprintf(stderr,"\tinactive=%ld\n",gr->inactiveTimeout());
		fprintf(stderr,"\tmask=%s\n",gr->eventMaskString());
		fprintf(stderr,"\tcaching = %s\n",gr->getCacheMode() ? "enabled" : "disabled" );
		fprintf(stderr,"\tarchive monitor = %s\n",gr->getArchiveMode() ? "enabled" : "disabled" );		
#ifndef _WIN32
		fprintf(stderr,"\tuser id=%ld\n",(long)getuid());
		fprintf(stderr,"\tgroup id=%ld\n",(long)getgid());
#endif
		if(gr->isReadOnly())
			fprintf(stderr,"\tread only mode\n");
		fprintf(stderr,"  (The default filenames depend on which "
		  "files exist in home)\n");
		return -1;
	}
	
	// Change to the specified home directory
	if(home_dir)
	{
		if(chdir(home_dir)<0)
		{
			perror("Change to home directory failed");
			fprintf(stderr,"-->Bad home <%s>\n",home_dir); fflush(stderr);
			return -1;
		}
	}
	getcwd(home_directory,HOME_DIR_SIZE);

	// Get the default resources. The values of access_file,
	// pvlist_file, command_file, putlog_file, and report_file depend
	// on whether the default filenames exist in the cwd.
	global_resources = new gateResources;
	gateResources* gr = global_resources;

	// Set command-line values, order is somewhat important
	if(level)				gr->setDebugLevel(level);
	if(read_only)			gr->setReadOnly();
	if(mask)				gr->setEventMask(mask);
	if(connect_tout>=0)		gr->setConnectTimeout(connect_tout);
	if(inactive_tout>=0)	gr->setInactiveTimeout(inactive_tout);
	if(dead_tout>=0)		gr->setDeadTimeout(dead_tout);
	if(disconnect_tout>=0)	gr->setDisconnectTimeout(disconnect_tout);
	if(reconnect_tinhib>=0)	gr->setReconnectInhibit(reconnect_tinhib);
	if(access_file)			gr->setAccessFile(access_file);
	if(pvlist_file)			gr->setListFile(pvlist_file);
	if(command_file)		gr->setCommandFile(command_file);
	if(putlog_file)	    	gr->setPutlogFile(putlog_file);
#ifdef WITH_CAPUTLOG
	if(caputlog_address)  	gr->setCaPutlogAddress(caputlog_address);
#endif
	if(report_file)	    	gr->setReportFile(report_file);
	
	//set caching and archive mode 
	gr->setCacheMode(cache);
	gr->setArchiveMode(archive);
	
	//get EPICS_CA_MAX_ARRAY_BYTES
    long maxBytesAsALong;
    long byteStatus =  envGetLongConfigParam ( & EPICS_CA_MAX_ARRAY_BYTES, & maxBytesAsALong );	
	if ( byteStatus || maxBytesAsALong <= 0 || ((unsigned)maxBytesAsALong < MAX_TCP)) maxBytesAsALong = MAX_TCP;
	gr->setMaxBytes((unsigned)maxBytesAsALong);	

	
#ifndef _WIN32
	gr->setServerMode(make_server);
#endif

#ifndef _WIN32
	if(make_server)
	{
		// Start watcher process
		if(manage_gateway()) return 0;
	}
	else
	{
		parent_pid=getpid();
	}
#endif

	// *******************************************
	// gets here if this is an interactive gateway
	// *******************************************

	// Change stderr and stdout to the log file
	if(log_file || make_server)
	{
		if(log_file==NULL) log_file=GATE_LOG_FILE;
		time(&t);

#ifndef _WIN32
		char *logerror=NULL;
		char *putlogerror=NULL;
		// Save log file if it exists
		if(stat(log_file,&sbuf)==0)
		{
			if(sbuf.st_size>0)
			{
				sprintf(logSaveFile,"%s.%lu",log_file,(unsigned long)t);
				if(link(log_file,logSaveFile)<0)
				{
					fprintf(stderr,"%s Failed to move old log to %s\n",
						timeStamp(),logSaveFile);
					char *error=strerror(errno);
					if(error && *error) {
						logerror=(char *)malloc(strlen(error)+1);
						if(logerror) {
							strcpy(logerror,error);
							fprintf(stderr,"  Reason: %s\n",logerror);
						}
					}
				}
				else
					unlink(log_file);
			}
		}

		// Save putlog file if it exists
		if(stat(putlog_file,&sbuf)==0)
		{
			if(sbuf.st_size>0)
			{
				sprintf(putlogSaveFile,"%s.%lu",putlog_file,(unsigned long)t);
				if(link(putlog_file,putlogSaveFile)<0)
				{
					fprintf(stderr,"%s Failed to move old putlog to %s\n",
						timeStamp(),putlogSaveFile);
					char *error=strerror(errno);
					if(error && *error) {
						putlogerror=(char *)malloc(strlen(error)+1);
						if(putlogerror) {
							strcpy(putlogerror,error);
							fprintf(stderr,"  Reason: %s\n",putlogerror);
						}
					}
				}
				else
					unlink(putlog_file);
			}
		}
#endif

		// Redirect stdout and stderr
		// Open it and close it to empty it (Necessary on WIN32,
		// apparently not necessary on Solaris)
		FILE *fp=fopen(log_file,"w");
		if(fp == NULL) {
			fprintf(stderr,"Cannot open %s\n",log_file);
			fflush(stderr);
		} else {
			fclose(fp);
		}
		// KE: This was formerly "w" instead of "a" and stderr was
		//  overwriting the top of the log file
		if((freopen(log_file,"a",stderr))==NULL ) {
			fprintf(stderr,"Redirect of stderr to file %s failed\n",log_file);
			fflush(stderr);
		}
		if((freopen(log_file,"a",stdout))==NULL ) {
			fprintf(stderr,"Redirect of stdout to file %s failed\n",log_file);
			fflush(stderr);
		}

#ifndef _WIN32
		// Repeat error messages to log
		if(logerror) {
			fprintf(stderr,"%s Failed to move old log to %s\n",
			  timeStamp(),logSaveFile);
			if(logerror) {
				fprintf(stderr,"  Reason: %s\n",logerror);
				fflush(stderr);
				free(logerror);
			}
		}
		if(putlogerror) {
			fprintf(stderr,"%s Failed to move old putlog to %s\n",
			  timeStamp(),putlogSaveFile);
			if(putlogerror) {
				fprintf(stderr,"  Reason: %s\n",putlogerror);
				fflush(stderr);
				free(putlogerror);
			}
		}
#endif
	}
	else
	{
		log_file="<terminal>";
	}

	gr->setUpAccessSecurity();

	if(gr->debugLevel()>10)
	{
		fprintf(stderr,"\noption dump:\n");
		fprintf(stderr," home = <%s>\n",home_directory);
		fprintf(stderr," log file = <%s>\n",log_file);
		fprintf(stderr," access file = <%s>\n",gr->accessFile());
		fprintf(stderr," list file = <%s>\n",gr->listFile());
		fprintf(stderr," command file = <%s>\n",gr->commandFile());
		fprintf(stderr," putlog file = <%s>\n",gr->putlogFile());
#ifdef WITH_CAPUTLOG
		fprintf(stderr," caputlog address = <%s>\n",gr->caputlogAddress());
#endif
		fprintf(stderr," report file = <%s>\n",gr->reportFile());
		fprintf(stderr," debug level = %d\n",gr->debugLevel());
		fprintf(stderr," connect timeout = %ld\n",gr->connectTimeout());
		fprintf(stderr," disconnect timeout = %ld\n",gr->disconnectTimeout());
		fprintf(stderr," reconnect inhibit time = %ld\n",gr->reconnectInhibit());
		fprintf(stderr," inactive timeout = %ld\n",gr->inactiveTimeout());
		fprintf(stderr," dead timeout = %ld\n",gr->deadTimeout());
		fprintf(stderr," event mask = %s\n",gr->eventMaskString());
		fprintf(stderr," caching = %s\n",gr->getCacheMode() ? "enabled" : "disabled" );
		fprintf(stderr," archive monitor = %s\n",gr->getArchiveMode() ? "enabled" : "disabled" );		
#ifndef _WIN32
		fprintf(stderr," user id= %ld\n",(long)getuid());
		fprintf(stderr," group id= %ld\n",(long)getgid());
#endif
		if(gr->isReadOnly()) {
			fprintf(stderr," read only mode\n");
		}
		fflush(stderr);
	}

	// Open putlog file.  This could be done the first time it is
	// used, but we want to open it and keep it open because of
	// possible problems with FOPEN_MAX.
	if(putlog_file)
	{
		FILE *fp=fopen(putlog_file,"w");
		if(fp == NULL) {
			fprintf(stderr,"Cannot open %s\n",putlog_file);
			fflush(stderr);
		}
		gr->setPutlogFp(fp);
	}
	
	startEverything(stat_prefix);
	delete global_resources;
	return 0;
}

static void stopEverything(void)
{
	if(server) {
		// Set the flag and let it stop itself.  This will cause it to
		// clean up.  Setting quit_flag to 2 keeps it from printing a
		// message in the main loop.
		server->setQuitFlag(2u);
	} else {
		exit(0);
	}
}

#define pr fprintf

static void print_instructions(void)
{
	pr(stderr,"-debug value: Enter value between 0-100.  50 gives lots of\n");
	pr(stderr," info, 1 gives small amount.\n\n");
	
	pr(stderr,"-pvlist file_name: Name of file with all the allowed PVs in it\n");
	pr(stderr," See the sample file gateway.pvlist in the source distribution\n");
	pr(stderr," for a description of how to create this file.\n");
	
	pr(stderr,"-access file_name: Name of file with all the EPICS access\n");
	pr(stderr," security rules in it.  PVs in the pvlist file use groups\n");
	pr(stderr," and rules defined in this file.\n");
	
	pr(stderr,"-log file_name: Name of file where all messages from the\n");
	pr(stderr," gateway go, including stderr and stdout.\n\n");
	
	pr(stderr,"-command file_name: Name of file where gateway command(s) go\n");
	pr(stderr," Commands are executed when a USR1 signal is sent to gateway.\n\n");
	
	pr(stderr,"-putlog file_name: Name of file where gateway put logging goes.\n");
	pr(stderr," Put logging is specified with TRAPWRITE in the access file.\n\n");
	
	pr(stderr,"-report file_name: Name of file where gateway reports go.\n");
	pr(stderr," Reports are appended to this file if it exists.\n\n");
	
	pr(stderr,"-home directory: Home directory where all your gateway\n");
	pr(stderr," configuration files are kept where log and command files go.\n\n");
	
	pr(stderr,"-sip IP_address: IP address that gateway's CA server listens\n");
	pr(stderr," for PV requests.  Sets env variable EPICS_CAS_INTF_ADDR.\n\n");
	
	pr(stderr,"-signore IP_address_list: IP address that gateway's CA server\n");
	pr(stderr," ignores.  Sets env variable EPICS_CAS_IGNORE_ADDR_LIST.\n\n");
	
	pr(stderr,"-cip IP_address_list: IP address list that the gateway's CA\n");
	pr(stderr," client uses to find the real PVs.  See CA reference manual.\n");
	pr(stderr," This sets environment variables EPICS_CA_AUTO_LIST=NO and\n");
	pr(stderr," EPICS_CA_ADDR_LIST.\n\n");
	
	pr(stderr,"-sport CA_server_port: The port which the gateway's CA server\n");
	pr(stderr," uses to listen for PV requests.  Sets environment variable\n");
	pr(stderr," EPICS_CAS_SERVER_PORT.\n\n");
	
	pr(stderr,"-cport CA_client_port:  The port which the gateway's CA client\n");
	pr(stderr," uses to find the real PVs.  Sets environment variable\n");
	pr(stderr," EPICS_CA_SERVER_PORT.\n\n");
	
	pr(stderr,"-connect_timeout seconds: The amount of time that the\n");
	pr(stderr," gateway will allow a PV search to continue before marking the\n");
	pr(stderr," PV as being not found.\n\n");
	
	pr(stderr,"-inactive_timeout seconds: The amount of time that the gateway\n");
	pr(stderr," will hold the real connection to an unused PV.  If no gateway\n");
	pr(stderr," clients are using the PV, the real connection will still be\n");
	pr(stderr," held for this long.\n\n");
	
	pr(stderr,"-dead_timeout seconds:  The amount of time that the gateway\n");
	pr(stderr," will hold requests for PVs that are not found on the real\n");
	pr(stderr," network that the gateway is using.  Even if a client's\n");
	pr(stderr," requested PV is not found on the real network, the gateway\n");
	pr(stderr," marks the PV dead, holds the request and continues trying\n");
	pr(stderr," to connect for this long.\n\n");
	
	pr(stderr,"-disconnect_timeout seconds:  The amount of time that the gateway\n");
	pr(stderr," will hold requests for PVs that were connected but have been\n");
	pr(stderr," disconnected. When a disconnected PV reconnects, the gateway will\n");
	pr(stderr," broadcast a beacon signal to inform the clients that they may\n");
	pr(stderr," reconnect to the gateway.\n\n");

	pr(stderr,"-reconnect_inhibit seconds:  The minimum amount of time between\n");
	pr(stderr," additional beacons that the gateway will send to its clients\n");
	pr(stderr," when channels from the real network reconnect.\n\n");

	pr(stderr,"-server: Start as server. Detach from controlling terminal\n");
	pr(stderr," and start a daemon that watches the gateway and automatically\n");
	pr(stderr," restarts it if it dies.\n");
	
	pr(stderr,"-mask event_mask: Event mask that is used for connections on the\n");
	pr(stderr," real network: use any combination of v (value), a (alarm), l (log).\n");
	pr(stderr," Default is va (forward value and alarm change events).\n");
	
	pr(stderr,"-prefix string: Set the prefix for the gateway statistics PVs.\n");
	pr(stderr," Defaults to the hostname the gateway is running on.\n");
	
	pr(stderr,"-uid number: Run the server with this id, server does a\n");
	pr(stderr," setuid(2) to this user id number.\n\n");

	pr(stderr,"-gid number: Run the server with this id, server does a\n");
	pr(stderr," setgid(2) to this group id number.\n\n");
	
	pr(stderr,"-no_cache: Disables caching. Every get request will be forwarded to\n");
	pr(stderr," the ioc and monitor will be created only if needed.\n");
	
	pr(stderr,"-archive: Enables archive monitor. Additional log event monitor is \n");
	pr(stderr," is created.\n");	

}

#ifndef _WIN32
// -------------------------------------------------------------------
//  part that watches the gateway process and ensures that it stays up

static int manage_gateway(void)
{
	time_t t,prevt=0;
	int rc;
	int i;

	// Initialize time array
	for(i=0; i < NRESTARTS; i++) {
		startTime[i]=0;
	}

	save_chld=signal(SIGCHLD,sig_chld);
	save_hup=signal(SIGHUP,sig_stop);
	save_term=signal(SIGTERM,sig_stop);
	save_int=signal(SIGINT,sig_stop);
	
	// Fork.  Parent will exit, child will be a session leader
	pid_t pid=fork();
	switch(pid)	{
	case -1:
		// Error
		perror("Cannot create gateway processes");
		return -1;  // Will cause main to return 0;
	case 0:
		// Child
		// Make this process a session leader
		// KE: Not sure why this is necessary
		// Used to use setgrp() for UNIX, else setgrp(0,0)
		//   but this failed for Darwin
		setpgid(0,0);
		// Make a new session
		setsid();
		break;
	default:
		// Parent
		return 1;  // Will cause main to return 0;
	}
	
	parent_pid=getpid();
	
	// 
	do {
		// Don't allow runaway restarts
		time(&t);
		if(nStart < NRESTARTS) {
			startTime[nStart]=t;
			nStart++;
		} else {
			// Check the interval since NRESTARTSth previous start
			if(t-startTime[0] < RESTART_INTERVAL) {
				// Too many recent starts
				fprintf(stderr,
				  "\nGateway: There were too many [%d] restarts in the last %d seconds\n",
				  NRESTARTS+1,RESTART_INTERVAL);
				fprintf(stderr,"Aborting Gateway ServerPID %d\n",(int)parent_pid);
				exit(1);
			} else {
				// Reset the start times
				for(i=0; i < NRESTARTS-1; i++) {
					startTime[i]=startTime[i+1];
				}
				nStart=NRESTARTS;
				startTime[NRESTARTS-1]=t;
			}
		}
		
		// Don't respawn faster than every RESTART_DELAY seconds
		if((t-prevt) < RESTART_DELAY) sleep(RESTART_DELAY);
		prevt=t;

		// Fork.  Parent will be the server and child will be the
		// gateway.  Parent will pause until receiving a signal that is
		// handled by sig_stop, which sets death_flag to 1.
		switch(gate_pid=fork())	{
		case -1:
			// Error
			perror("Cannot create gateway processes");
			gate_pid=0;
			break;
		case 0:
			// Child
			break;
		default:
			// Pause until we receive a signal.  If the signal is HUP,
			// TERM, or INT (handled by sig_stop), then death_flag is
			// set to 1 andc the server exits.  If it is SIG_CHLD,
			// then a new fork occurs after executing sig_chld, which
			// waits for all information to come in.  Note that any
			// other non-ignored signal will end the pause and fork a
			// new gateway, if there are any.
			pause();
			break;
		}
	} while(gate_pid && death_flag==0);
	
	if(death_flag || gate_pid==-1) rc=1;
	else rc=0;

	return rc;
}
#endif //#ifdef _WIN32

static int setEnv(const char *var, const char *val, char **envString)
{
    size_t len=strlen(var)+strlen(val)+2;

	*envString=(char *)malloc(len);
	if(!*envString) {
		fprintf(stderr,"Memory allocation error for %s",var);
		return 1;
	}
	sprintf(*envString,"%s=%s",var,val);
#if 0
	// There is no putenv on Linux
	int status=putenv(*envString);
	if(status) {
		fprintf(stderr,"putenv failed for:\n  %s\n",*envString);
	}
#else
	epicsEnvSet(var,val);
#endif
	return 0;
}

static int setEnv(const char *var, int ival, char **envString)
{
	// Allow 40 for size of ival
    size_t len=strlen(var)+40+2;

	*envString=(char *)malloc(len);
	if(!*envString) {
		fprintf(stderr,"Memory allocation error for %s",var);
		return 1;
	}
	sprintf(*envString,"%s=%d",var,ival);
#if 0
	// There is no putenv on Linux
	int status=putenv(*envString);
	if(status) {
		fprintf(stderr,"putenv failed for:\n  %s\n",*envString);
	}
#else
	char *pVal=strchr(*envString,'=');
	if(!pVal || !(pVal+1)) {
		epicsEnvSet(var,"");
	} else {
		epicsEnvSet(var,pVal+1);
	}
#endif
	return 0;
}

void printRecentHistory(void)
{
#ifndef _WIN32
	int nStarts=0;
	int i;

	if(nStart < 1) return;
	for(i=0; i < nStart-1; i++) {
		if((startTime[nStart-1]-startTime[i]) < RESTART_INTERVAL) {
			nStarts++;
		}
	}
	if(nStarts) {
		fflush(stderr);
		printf("There have been %d restarts for serverPID %d "
		  "in the last %d seconds\n",
		  nStarts+1,parent_pid,RESTART_INTERVAL);
		printf("  Only %d restarts are allowed in this interval\n",NRESTARTS);
		fflush(stdout);
	}
#endif
}

static void printEnv(FILE *fp, const char *var)
{
	if(!fp || !var) return;
	
	char *value=getenv(var);
	fprintf(fp,"%s=%s\n", var, value?value:"Not specified");
}


/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
