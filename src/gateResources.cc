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

// KE: strDup() comes from base/src/gdd/aitHelpers.h
// Not clear why strdup() is not used

#define GATE_RESOURCE_FILE 1

#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include <time.h>

#ifdef _WIN32
/* WIN32 does not have unistd.h and does not define the following constants */
# define F_OK 00
# define W_OK 02
# define R_OK 04
# include <direct.h>     /* for getcwd (usually in sys/parm.h or unistd.h) */
# include <io.h>         /* for access, chmod  (usually in unistd.h) */
#else
# include <unistd.h>
# include <sys/utsname.h>
#endif

#include "cadef.h"

#include "gateResources.h"
#include "gateAs.h"
#include <gddAppTable.h>
#include <dbMapper.h>

#ifdef WITH_CAPUTLOG
  #include <caPutLog.h>
  #include <caPutLogTask.h>
  #include <caPutLogAs.h>
#endif

// Global variables
gateResources* global_resources;

// ---------------------------- utilities ------------------------------------


// Gets current time and puts it in a static array The calling program
// should copy it to a safe place e.g. strcpy(savetime,timestamp());
char *timeStamp(void)
{
	static char timeStampStr[20];
	time_t now;
	struct tm *tblock;
	
	time(&now);
	tblock=localtime(&now);
	strftime(timeStampStr,sizeof(timeStampStr),"%b %d %H:%M:%S",tblock);
	
	return timeStampStr;
}

// Gets current time and puts it in a static array The calling program
// should copy it to a safe place e.g. strcpy(savetime,timestamp());
char *timeString(time_t time)
{
    static char timeStr[80];
    int rem = (int) time;
    int days=rem/86400;
    rem-=days*86400;
    int hours=rem/3600;
    rem-=hours*3600;
    int min=rem/60;
    rem-=min*60;
    int sec=rem;
    sprintf(timeStr,"%3d:%02d:%02d:%02d",days,hours,min,sec);
    return timeStr;
}

// Gets the computer name and allocates memory for it using strDup
// (from base/src/gdd/aitHelpers.h)
char *getComputerName(void)
{
	char*name=NULL;

#ifdef _WIN32
	TCHAR computerName[MAX_COMPUTERNAME_LENGTH+1];
	DWORD size=MAX_COMPUTERNAME_LENGTH+1;
	// Will probably be uppercase
	BOOL status=GetComputerName(computerName,&size);
	if(status && size > 0) {
		// Convert to lowercase and copy
		// OK for ANSI.  Won't work for Unicode w/o conversion.
		char *pChar=computerName;
		while(*pChar) *pChar=tolower(*pChar++);
		name=strDup(computerName);
	}
#else
	struct utsname ubuf;
	if(uname(&ubuf) >= 0) {
		// Use the name of the host
		name=strDup(ubuf.nodename);
	}
#endif

	return name;
}

#ifdef WITH_CAPUTLOG
/*
  We need to define these here, as caPutLog is using dbFldTypes.h defs for
  DBR_xxx and our code is loading db_access.h defs elsewhere, and thse ARE
  DIFFERENT.

  DBR_FLOAT in db_access.h is 6, for example but in dbFldTypes.h that means a
  DBR_ULONG.
*/
#define OUR_DBR_STRING   0
#define OUR_DBR_CHAR     1
#define OUR_DBR_UCHAR    2
#define OUR_DBR_SHORT    3
#define OUR_DBR_USHORT   4
#define OUR_DBR_LONG     5
#define OUR_DBR_ULONG    6
#define OUR_DBR_FLOAT    7
#define OUR_DBR_DOUBLE   8

static int gddGetOurType(const gdd *gddVal)
{
  switch ( gddVal->primitiveType() ) {
    case aitEnumInt8    : return(OUR_DBR_CHAR);
    case aitEnumUint8   : return(OUR_DBR_UCHAR);
    case aitEnumInt16   : return(OUR_DBR_SHORT);
    case aitEnumEnum16  : return(OUR_DBR_USHORT);
    case aitEnumUint16  : return(OUR_DBR_USHORT);
    case aitEnumInt32   : return(OUR_DBR_LONG);
    case aitEnumUint32  : return(OUR_DBR_ULONG);
    case aitEnumFloat32 : return(OUR_DBR_FLOAT);
    case aitEnumFloat64 : return(OUR_DBR_DOUBLE);
    case aitEnumFixedString:
    case aitEnumString:
    default:
      return(OUR_DBR_STRING);
  }
}

static int gddToVALUE(const gdd *gddVal, short ourdbrtype, VALUE *valueStruct)
{
  memset(valueStruct,0,sizeof(VALUE));
  switch (ourdbrtype) {
    case OUR_DBR_CHAR: {
          aitInt8 x;
          gddVal->get(x);
          valueStruct->v_int8 = x;
        }
        return(0);

    case OUR_DBR_UCHAR: {
          aitUint8 x;
          gddVal->get(x);
          valueStruct->v_uint8 = x;
        }
        return(0);

    case OUR_DBR_SHORT: {
          aitInt16 x;
          gddVal->get(x);
          valueStruct->v_int16 = x;
        }
        return(0);

    case OUR_DBR_USHORT: {
          aitUint16 x;
          gddVal->get(x);
          valueStruct->v_uint16 = x;
        }
        return(0);

    case OUR_DBR_LONG: {
          aitInt32 x;
          gddVal->get(x);
          valueStruct->v_int32 = x;
        }
        return(0);

    case OUR_DBR_ULONG: {
          aitUint32 x;
          gddVal->get(x);
          valueStruct->v_uint32 = x;
        }
        return(0);

    case OUR_DBR_FLOAT: {
          aitFloat32 x;
          gddVal->get(x);
          valueStruct->v_float = x;
        }
        return(0);

    case OUR_DBR_DOUBLE: {
          aitFloat64 x;
          gddVal->get(x);
          valueStruct->v_double = x;
        }
        return(0);

    case OUR_DBR_STRING:
    default: {
          aitString x;
          gddVal->get(x);
          int len = strlen(x);
          int siz = sizeof(valueStruct->v_string);
          if (len >= siz) {
            strncpy(valueStruct->v_string,x,siz-1);
            valueStruct->v_string[siz-1] = 0;
          } else {
            strcpy(valueStruct->v_string,x);
          }
          return(0);
        }
  }
}

#if 0
static char *debugVALUEString(VALUE *v, int ourdbrtype, char *buffer)
{
  switch (ourdbrtype) {
    case OUR_DBR_CHAR:
      sprintf(buffer,"v_int8 %d",v->v_int8);
      break;
    case OUR_DBR_UCHAR:
      sprintf(buffer,"v_uint8 %d",v->v_uint8);
      break;
    case OUR_DBR_SHORT:
      sprintf(buffer,"v_int16 %hd",v->v_int16);
      break;
    case OUR_DBR_USHORT:
      sprintf(buffer,"v_uint16 %hu",v->v_uint16);
      break;
    case OUR_DBR_LONG:
      sprintf(buffer,"v_int32 %d",v->v_int32);
      break;
    case OUR_DBR_ULONG:
      sprintf(buffer,"v_uint32 %u",v->v_uint32);
      break;
    case OUR_DBR_FLOAT:
      sprintf(buffer,"v_float %g",v->v_float);
      break;
    case OUR_DBR_DOUBLE:
      sprintf(buffer,"v_double %g",v->v_double);
      break;
    case OUR_DBR_STRING:
      sprintf(buffer,"v_string '%s'",v->v_string);
      break;
    default:
      sprintf(buffer,"unknown type %d",ourdbrtype);
  }
  return(buffer);
}
#endif

#endif // WITH_CAPUTLOG

gateResources::gateResources(void)
{
	as = NULL;
    if(access(GATE_PV_ACCESS_FILE,F_OK)==0)
      access_file=strDup(GATE_PV_ACCESS_FILE);
    else
      access_file=NULL;
    
    if(access(GATE_PV_LIST_FILE,F_OK)==0)
      pvlist_file=strDup(GATE_PV_LIST_FILE);
    else
      pvlist_file=NULL;
    
    if(access(GATE_COMMAND_FILE,F_OK)==0)
      command_file=strDup(GATE_COMMAND_FILE);
    else
      command_file=NULL;
      
      
    
	// Miscellaneous initializations
	putlog_file=NULL;
#ifdef WITH_CAPUTLOG
    caputlog_address=NULL;
#endif
	putlogFp=NULL;
	report_file=strDup(GATE_REPORT_FILE);
    debug_level=0;
    ro=0;
	serverMode=false;
#ifdef RESERVE_FOPEN_FD
	reserveFp = NULL;
#endif
    
    setEventMask(DBE_VALUE | DBE_ALARM);
    setConnectTimeout(GATE_CONNECT_TIMEOUT);
    setInactiveTimeout(GATE_INACTIVE_TIMEOUT);
    setDeadTimeout(GATE_DEAD_TIMEOUT);
    setDisconnectTimeout(GATE_DISCONNECT_TIMEOUT);
    setReconnectInhibit(GATE_RECONNECT_INHIBIT);
    
    gddApplicationTypeTable& tt = gddApplicationTypeTable::AppTable();
    
	gddMakeMapDBR(tt);
	
	appValue=tt.getApplicationType("value");
	appUnits=tt.getApplicationType("units");
	appEnum=tt.getApplicationType("enums");
	appAll=tt.getApplicationType("all");
	appFixed=tt.getApplicationType("fixed");
	appAttributes=tt.getApplicationType("attributes");
	appMenuitem=tt.getApplicationType("menuitem");
	// RL: Should this rather be included in the type table?
	appSTSAckString=gddDbrToAit[DBR_STSACK_STRING].app;
}

gateResources::~gateResources(void)
{
	if(access_file)	delete [] access_file;
	if(pvlist_file)	delete [] pvlist_file;
	if(command_file) delete [] command_file;
	if(putlog_file) delete [] putlog_file;
	if(report_file) delete [] report_file;
#ifdef WITH_CAPUTLOG
    caPutLog_Term();
	if (caputlog_address) delete [] caputlog_address;
#endif
}

int gateResources::appValue=0;
int gateResources::appEnum=0;
int gateResources::appAll=0;
int gateResources::appMenuitem=0;
int gateResources::appFixed=0;
int gateResources::appUnits=0;
int gateResources::appAttributes=0;
int gateResources::appSTSAckString=0;

int gateResources::setListFile(const char* file)
{
	if(pvlist_file) delete [] pvlist_file;
	pvlist_file=strDup(file);
	return 0;
}

int gateResources::setAccessFile(const char* file)
{
	if(access_file) delete [] access_file;
	access_file=strDup(file);
	return 0;
}

int gateResources::setCommandFile(const char* file)
{
	if(command_file) delete [] command_file;
	command_file=strDup(file);
	return 0;
}

int gateResources::setPutlogFile(const char* file)
{
	if(putlog_file) delete [] putlog_file;
	putlog_file=strDup(file);
	return 0;
}

#ifdef WITH_CAPUTLOG
int gateResources::setCaPutlogAddress(const char* address)
{
	if (caputlog_address) {
      delete [] caputlog_address;
    }
	caputlog_address = strDup(address);
    return 0;
}

int gateResources::caPutLog_Init(void)
{
  if (caputlog_address) {
    return caPutLogInit(caputlog_address,caPutLogAll);
  }
  return 1;
}

void gateResources::caPutLog_Term(void)
{
  caPutLogTaskStop();
}

void gateResources::caPutLog_Send
     (const char *user,
      const char *host,
      const char *pvname,
      const gdd *old_value,
      const gdd *new_value)
{
  if ((! new_value) || (new_value->primitiveType() == aitEnumInvalid)) return;

  // get memory for a LOGDATA item from caPutLog's free list
  LOGDATA *pdata = caPutLogDataCalloc();
  if (pdata == NULL) {
    errlogPrintf("gateResources::caPutLogSend: memory allocation failed\n");
    return;
  }
  strcpy(pdata->userid,user);
  strcpy(pdata->hostid,host);
  strcpy(pdata->pv_name,pvname);
  pdata->pfield = (void *) pvname;
  pdata->type = gddGetOurType(new_value);
  gddToVALUE(new_value,pdata->type,&pdata->new_value.value);
  new_value->getTimeStamp(&pdata->new_value.time);
  if ((old_value) && (old_value->primitiveType() != aitEnumInvalid) && (gddGetOurType(old_value) == pdata->type)) {
    gddToVALUE(old_value,pdata->type,&pdata->old_value);
  } else {
    // if no usable old_value provided, fill in data.old_value with copy of new value
    // as there's no way to flag a VALUE struct as invalid
    memcpy(&pdata->old_value,&pdata->new_value.value,sizeof(VALUE));
  }
  caPutLogTaskSend(pdata);
}

void gateResources::putLog(
       FILE            *       fp,
       const char      *       user,
       const char      *       host,
       const char      *       pvname,
       const gdd       *       old_value,
       const gdd       *       new_value       )
{
       if(fp) {
               VALUE   oldVal,                 newVal;
               char    acOldVal[20],   acNewVal[20];
               if ( old_value == NULL )
               {
                       acOldVal[0] = '?';
                       acOldVal[1] = '\0';
               }
               else
               {
                       gddToVALUE( old_value, gddGetOurType(old_value), &oldVal );
                       VALUE_to_string( acOldVal, 20, &oldVal, gddGetOurType(old_value) );
               }
               gddToVALUE( new_value, gddGetOurType(new_value), &newVal );
               VALUE_to_string( acNewVal, 20, &newVal, gddGetOurType(new_value) );
               fprintf(fp,"%s %s@%s %s %s old=%s\n",
                 timeStamp(),
                 user?user:"Unknown",
                 host?host:"Unknown",
                 pvname,
                 acNewVal,
                 acOldVal );
               fflush(fp);
       }
}

#endif // WITH_CAPUTLOG

int gateResources::setReportFile(const char* file)
{
	if(report_file) delete [] report_file;
	report_file=strDup(file);
	return 0;
}

int gateResources::setDebugLevel(int level)
{
	debug_level=level;
	return 0;
}

int gateResources::setUpAccessSecurity(void)
{
	as=new gateAs(pvlist_file,access_file);
	return 0;
}

#ifdef RESERVE_FOPEN_FD
// Functions to try to reserve a file descriptor to use for fopen.  On
// Solaris, at least, fopen is limited to FDs < 256.  These could all
// be used by CA and CAS sockets if there are connections to enough
// IOCs  These functions try to reserve a FD < 256.
FILE *gateResources::fopen(const char *filename, const char *mode)
{
	// Close the dummy file holding the FD open
    if(reserveFp) ::fclose(reserveFp);
    reserveFp=NULL;
	
	// Open the file.  It should use the lowest available FD, that is,
	// the one we just made available.
    FILE *fp=::fopen(filename,mode);
    if(!fp) {
		// Try to get the reserved one back
		reserveFp=::fopen(GATE_RESERVE_FILE,"w");
    }
	
    return fp;
}

int gateResources::fclose(FILE *stream)
{
	// Close the file
    int ret=::fclose(stream);
	
	// Open the dummy file to reserve the FD just made available
    reserveFp=::fopen(GATE_RESERVE_FILE,"w");
	
    return ret;
}

FILE *gateResources::openReserveFile(void)
{
    reserveFp=::fopen(GATE_RESERVE_FILE,"w");
    return reserveFp;
}
#endif

gateAs* gateResources::getAs(void)
{
	if(as==NULL) setUpAccessSecurity();
	return as;
}

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
