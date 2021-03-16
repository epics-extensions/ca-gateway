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
#include "epicsStdio.h"

#include "gateResources.h"
#include "gateAs.h"
#include "gateDbFld.h"

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
		while (*pChar) {
			*pChar = tolower(*pChar);
			++pChar;
		}
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

// caPutLog uses dbFldTypes.h whereas elsewhere in the gateway uses  db_access.h
// and the DBR_xxx codes ARE DIFFERENT. We access the dbFldTypes numbers here via
// a mapping in DBFLD:: to avoid clash with db_access.h definitions

static int gddGetOurType(const gdd *gddVal)
{
  switch ( gddVal->primitiveType() ) {
    case aitEnumInt8    : return(DBFLD::D_CHAR);
    case aitEnumUint8   : return(DBFLD::D_UCHAR);
    case aitEnumInt16   : return(DBFLD::D_SHORT);
    case aitEnumEnum16  : return(DBFLD::D_USHORT);
    case aitEnumUint16  : return(DBFLD::D_USHORT);
    case aitEnumInt32   : return(DBFLD::D_LONG);
    case aitEnumUint32  : return(DBFLD::D_ULONG);
    case aitEnumFloat32 : return(DBFLD::D_FLOAT);
    case aitEnumFloat64 : return(DBFLD::D_DOUBLE);
    case aitEnumFixedString:
    case aitEnumString:
    default:
      return(DBFLD::D_STRING);
  }
}

static int gddToVALUE(const gdd *gddVal, short dbfld_dbrtype, VALUE *valueStruct)
{
  memset(valueStruct,0,sizeof(VALUE));
  if (dbfld_dbrtype == DBFLD::D_CHAR) {
          aitInt8 x;
          gddVal->get(x);
          valueStruct->v_int8 = x;
   } else if (dbfld_dbrtype == DBFLD::D_UCHAR) {
          aitUint8 x;
          gddVal->get(x);
          valueStruct->v_uint8 = x;
   } else if (dbfld_dbrtype == DBFLD::D_SHORT) {
          aitInt16 x;
          gddVal->get(x);
          valueStruct->v_int16 = x;
   } else if (dbfld_dbrtype == DBFLD::D_USHORT) {
          aitUint16 x;
          gddVal->get(x);
          valueStruct->v_uint16 = x;
   } else if (dbfld_dbrtype == DBFLD::D_LONG) {
          aitInt32 x;
          gddVal->get(x);
          valueStruct->v_int32 = x;
   } else if (dbfld_dbrtype == DBFLD::D_ULONG) {
          aitUint32 x;
          gddVal->get(x);
          valueStruct->v_uint32 = x;
   } else if (dbfld_dbrtype == DBFLD::D_FLOAT) {
          aitFloat32 x;
          gddVal->get(x);
          valueStruct->v_float = x;
   } else if (dbfld_dbrtype == DBFLD::D_DOUBLE) {
          aitFloat64 x;
          gddVal->get(x);
          valueStruct->v_double = x;
   } else { // DBFLD::D_STRING and unknown
          aitString x;
          gddVal->get(x);
          size_t siz = sizeof(valueStruct->v_string);
          strncpy(valueStruct->v_string,x,siz);
          valueStruct->v_string[siz-1] = 0;
   }
   return(0);
}

/*
 * VALUE_to_string(): convert VALUE to string
 */
static int VALUE_to_string(char *pbuf, size_t buflen, const VALUE *pval, short dbfld_dbrtype, bool prefix_with_type = false)
{
    /* CHAR and UCHAR are typically used as SHORTSHORT,
	 * so avoid mounting NULL-bytes into the string
	 */
	if (dbfld_dbrtype == DBFLD::D_CHAR) {
		/* we print v_uint8 rather than v_int8, this is what caPutLog does */
        return epicsSnprintf(pbuf, buflen, "%s%d", (prefix_with_type ? "v_int8 " : ""), (int)pval->v_uint8);
	} else if (dbfld_dbrtype == DBFLD::D_UCHAR) {
        return epicsSnprintf(pbuf, buflen, "%s%d", (prefix_with_type ? "v_uint8 " : ""), (int)pval->v_uint8);
	} else if (dbfld_dbrtype == DBFLD::D_SHORT) {
        return epicsSnprintf(pbuf, buflen, "%s%hd", (prefix_with_type ? "v_int16 " : ""), pval->v_int16);
	} else if (dbfld_dbrtype == DBFLD::D_USHORT || dbfld_dbrtype == DBFLD::D_ENUM) {
        return epicsSnprintf(pbuf, buflen, "%s%hu", (prefix_with_type ? "v_uint16 " : ""), pval->v_uint16);
	} else if (dbfld_dbrtype == DBFLD::D_LONG) {
        return epicsSnprintf(pbuf, buflen, "%s%ld", (prefix_with_type ? "v_int32 " : ""), (long)pval->v_int32);
	} else if (dbfld_dbrtype == DBFLD::D_ULONG) {
        return epicsSnprintf(pbuf, buflen, "%s%lu", (prefix_with_type ? "v_uint32 " : ""), (unsigned long)pval->v_uint32);
	} else if (dbfld_dbrtype == DBFLD::D_FLOAT) {
        return epicsSnprintf(pbuf, buflen, "%s%g", (prefix_with_type ? "v_float " : ""), pval->v_float);
	} else if (dbfld_dbrtype == DBFLD::D_DOUBLE) {
        return epicsSnprintf(pbuf, buflen, "%s%g", (prefix_with_type ? "v_double " : ""), pval->v_double);
	} else if (dbfld_dbrtype == DBFLD::D_STRING) {
        return epicsSnprintf(pbuf, buflen, "%s%s", (prefix_with_type ? "v_string " : ""), pval->v_string);
	} else {
		char type[32];
		epicsSnprintf(type, sizeof(type), "unknown type %d ", dbfld_dbrtype);
        return epicsSnprintf(pbuf, buflen, "%s%s", (prefix_with_type ? type : ""), pval->v_string);
    }
}

#if 0
static char *debugVALUEString(const VALUE *v, int dbfld_dbrtype, char *buffer, size_t buflen)
{
	VALUE_to_string(buffer, buflen, v, dbfld_dbrtype, true);
	return buffer;
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
                       VALUE_to_string( acOldVal, 20, &oldVal, gddGetOurType(old_value), false );
               }
               gddToVALUE( new_value, gddGetOurType(new_value), &newVal );
               VALUE_to_string( acNewVal, 20, &newVal, gddGetOurType(new_value), false );
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
    if (as) delete as;
	as=new gateAs(pvlist_file,access_file);
	return 0;
}

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
