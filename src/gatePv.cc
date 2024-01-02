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
 * File:       gatePv.cc
 * Project:    CA Proxy Gateway
 *
 * Descr.:     PV = Client side (lower half) of Proxy Gateway Channel
 *             Handles all CAC related stuff:
 *             - Connections (and exception handling)
 *             - Monitors (value and ALH data)
 *             - Put operations (Gets are answered by the VC)
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 *********************************************************************-*/

#define DEBUG_TOTAL_PV 0
#define DEBUG_PV_CON_LIST 0
#define DEBUG_PV_LIST 0
#define DEBUG_VC_DELETE 0
#define DEBUG_GDD 0
#define DEBUG_PUT 0
#define DEBUG_BEAM 0
#define DEBUG_ENUM 0
#define DEBUG_DELAY 0
#define DEBUG_SLIDER 0
#define DEBUG_HISTORY 0

#if DEBUG_HISTORY
# define HISTNAME "GW:432:S05"
# define HISTNUM 10
#endif

#define OMIT_CHECK_EVENT 1

#include <stdio.h>
#include <string.h>
#include <fcntl.h>
#include <time.h>
#include <sys/types.h>

#ifdef _WIN32
#else
# include <unistd.h>
# include <sys/time.h>
#endif

#include "tsDLList.h"

#include <gdd.h>
#include <gddApps.h>
#include <gddAppTable.h>
#include <dbMapper.h>
#include <caProto.h>

#include "gateResources.h"
#include "gateServer.h"
#include "gatePv.h"
#include "gateVc.h"
#include "gateAs.h"

// extern "C" wrappers needed by CA routines for callbacks
extern "C" {
	extern void connectCB(CONNECT_ARGS args) {	// connection callback
		gatePvData::connectCB(args);
	}
	extern void accessCB(ACCESS_ARGS args) {	// access security callback
		gatePvData::accessCB(args);
	}
	extern void eventCB(EVENT_ARGS args) {      // value-changed callback
		gatePvData::eventCB(args);
	}
    extern void alhCB(EVENT_ARGS args) {        // alh info value-changed callback
		gatePvData::alhCB(args);
	}
	extern void putCB(EVENT_ARGS args) {        // put callback
		gatePvData::putCB(args);
	}
	extern void getCB(EVENT_ARGS args) {        // get callback
		gatePvData::getCB(args);
	}
	extern void getTimeCB(EVENT_ARGS args) {        // get DBR_TIME callback
		gatePvData::getTimeCB(args);
	}
	extern void logEventCB(EVENT_ARGS args) {        // log event callback
		gatePvData::logEventCB(args);
	}
	extern void propEventCB(EVENT_ARGS args) {        // prop event callback
		gatePvData::propEventCB(args);
	}
}

// quick access to global_resources
#define GR global_resources
#define GETDD(ap) gddApplicationTypeTable::AppTable().getDD(GR->ap)

const char* const gatePvData::pv_state_names[] =
	{ "dead", "inactive", "active", "connecting", "disconnected" };

// ------------------------- gdd destructors --------------------------------

// Apart from the FixedString destructor, which is definitely needed,
// these are probably not necessary.  The default gddDestructor does
// delete [] (aitUint8 *)v. (aitUint=char) Since delete calls free,
// which casts the pointer to a char * anyway, our specific casts are
// probably wasted.

// Fixed String
class gateFixedStringDestruct : public gddDestructor
{
public:
	gateFixedStringDestruct(void) { }
	void run(void *v) { delete [] (aitFixedString *)v; }
};

// Enum
class gateEnumDestruct : public gddDestructor
{
public:
	gateEnumDestruct(void) { }
	void run(void *v) { delete [] (aitEnum16 *)v; }
};

// Int
class gateIntDestruct : public gddDestructor
{
public:
	gateIntDestruct(void) { }
	void run(void *v) { delete [] (aitInt32 *)v; }
};

// Char  (Default would also work here)
class gateCharDestruct : public gddDestructor
{
public:
	gateCharDestruct(void) { }
	void run(void *v) { delete [] (aitInt8 *)v; }
};

// Float
class gateFloatDestruct : public gddDestructor
{
public:
	gateFloatDestruct(void) { }
	void run(void *v) { delete [] (aitFloat32 *)v; }
};

// Double
class gateDoubleDestruct : public gddDestructor
{
public:
	gateDoubleDestruct(void) { }
	void run(void *v) { delete [] (aitFloat64 *)v; }
};

// Short
class gateShortDestruct : public gddDestructor
{
public:
	gateShortDestruct(void) { }
	void run(void *v) { delete [] (aitInt16 *)v; }
};

// ------------------------- pv data methods ------------------------

gatePvData::gatePvData(gateServer* m,gateAsEntry* pase,const char* name)
{
	gateDebug2(5,"gatePvData(gateServer=%p,name=%s)\n",(void *)m,name);
	initClear();
	init(m,pase,name);
#ifdef STAT_PVS
	mrg->setStat(statPvTotal,++mrg->total_pv);
#endif
#if DEBUG_TOTAL_PV
	printf("gatePvdata: name=%s total_pv=%ld total_alive=%ld\n",
	  name,mrg->total_pv,mrg->total_alive);
#endif
#if DEBUG_HISTORY
	if(!strncmp(HISTNAME,name,HISTNUM)) {
		printf("%s gatePvData::gatePvData: %s state=%s\n",timeStamp(),name,
		  getStateName());
	}
#endif

	select_mask|=(mrg->alarmEventMask()|mrg->valueEventMask()|mrg->logEventMask());
	alh_mask|=mrg->alarmEventMask();
	value_alarm_mask|=(mrg->valueEventMask()|mrg->alarmEventMask());
	value_log_mask|=(mrg->valueEventMask()|mrg->logEventMask());
	value_mask|=mrg->valueEventMask();


    prop_get_state = 1; // Being used to ignore first propEventCB
}

gatePvData::~gatePvData(void)
{
	gateDebug1(5,"~gatePvData() name=%s\n",name());
#ifdef STAT_PVS
	switch(getState())
	{
	case gatePvDisconnect:
		mrg->setStat(statDisconnected,--mrg->total_disconnected);
		mrg->setStat(statUnconnected,--mrg->total_unconnected);
		break;
	case gatePvDead:
		mrg->setStat(statDead,--mrg->total_dead);
		mrg->setStat(statUnconnected,--mrg->total_unconnected);
		break;
	case gatePvConnect:
		mrg->setStat(statConnecting,--mrg->total_connecting);
		mrg->setStat(statUnconnected,--mrg->total_unconnected);
		break;
	case gatePvActive:
		mrg->setStat(statActive,--mrg->total_active);
		mrg->setStat(statAlive,--mrg->total_alive);
		break;
	case gatePvInactive:
		mrg->setStat(statInactive,--mrg->total_inactive);
		mrg->setStat(statAlive,--mrg->total_alive);
		break;
	}
	mrg->setStat(statPvTotal,--mrg->total_pv);
#endif
#if DEBUG_TOTAL_PV
	printf("~gatePvdata: name=%s total_pv=%ld total_alive=%ld\n",
	  name(),mrg->total_pv),mrg->total_alive;
#endif
	if(vc) {
		delete vc;
		vc=NULL;
	}
	unmonitor();
	logUnmonitor();
	propUnmonitor();
	alhUnmonitor();
	status=ca_clear_channel(chID);
	if(status != ECA_NORMAL) {
	    fprintf(stderr,"%s ~gatePvData: ca_clear_channel failed for %s:\n"
		  " %s\n",
		  timeStamp(),
	      name()?name():"Unknown",ca_message(status));
	}
	delete [] pv_name;

	// Clear the callback_list;
	gatePvCallbackId *id = NULL;
	while((id=callback_list.first()))
	{
		callback_list.remove(*id);
		delete id;
	}

	// Clear the async exist test list
	gateAsyncE* asynce = NULL;
	while((asynce=eio.first()))	{
		asynce->removeFromQueue();
	}
}

void gatePvData::initClear(void)
{
	setVC(NULL);
	status=0;
	markNotMonitored();
	markLogNotMonitored();
	markPropNotMonitored();
	markNoCtrlGetPending();
	markNoTimeGetPending();
	markAlhNotMonitored();
	markAlhNoGetPending();
	markNoAbort();
	markAddRemoveNotNeeded();
}

void gatePvData::init(gateServer* m,gateAsEntry* pase, const char* name)
{
	gateDebug2(5,"gatePvData::init(gateServer=%p,name=%s)\n",(void *)m,name);
	gateDebug1(5,"gatePvData::init entry pattern=%s)\n",pase->pattern);
	mrg=m;
	asentry=pase;
	setTimes();
	status=0;
	pv_name=strDup(name);

	if(asentry==NULL)
		status=-1;
	else
	{
		status=ca_create_channel(pv_name,::connectCB,this,
		  CA_PRIORITY_DEFAULT,&chID);
		if(status != ECA_NORMAL) {
			fprintf(stderr,"gatePvData::init: ca_search_and_connect for %s:\n"
			  " %s\n",
			  this->name()?this->name():"Unknown",ca_message(status));
		}
	}

	if(status==ECA_NORMAL)
	{
		status=ca_replace_access_rights_event(chID,::accessCB);
		setState(gatePvConnect);
#ifdef STAT_PVS
		mrg->setStat(statConnecting,++mrg->total_connecting);
		mrg->setStat(statUnconnected,++mrg->total_unconnected);
#endif
		if(status==ECA_NORMAL)
			status=0;
		else
			status=-1;
	}
	else
	{
		gateDebug0(5,"gatePvData::init() search and connect failed!\n");
		setState(gatePvDead);
#ifdef STAT_PVS
		mrg->setStat(statDead,++mrg->total_dead);
		mrg->setStat(statUnconnected,++mrg->total_unconnected);
#endif
		status=-1;
	}

	if(status)
	{
		// what do I do here? Nothing for now, let creator fix trouble
	}
	else
	{
		// Put PV into connecting list
		status = mrg->conAdd(pv_name,*this);
		if(status) fprintf(stderr,"%s Put into connecting list failed for %s\n",
		  timeStamp(),pv_name);

#if DEBUG_PV_CON_LIST
		printf("%s gatePvData::init: [%lu|%lu|%lu,%lu|%lu,%lu,%lu]: name=%s\n",
		  timeStamp(),
		  mrg->total_vc,mrg->total_pv,mrg->total_active,mrg->total_inactive,
		  mrg->total_connecting,mrg->total_dead,mrg->total_disconnected,
#endif

	}

#if OMIT_CHECK_EVENT
#else
	checkEvent(); // do ca_pend_event
#endif
}

aitEnum gatePvData::nativeType(void) const
{
	return gddDbrToAit[fieldType()].type;
}

int gatePvData::activate(gateVcData* vcd)
{
	gateDebug2(5,"gatePvData::activate(gateVcData=%p) name=%s\n",
	  (void *)vcd,name());

	int rc=-1;

#if DEBUG_DELAY
		if(!strncmp("Xorbit",name(),6)) {
			printf("%s gatePvData::activate: %s state=%d\n",timeStamp(),name(),
			  getState());
		}
#endif
#if DEBUG_HISTORY
		if(!strncmp(HISTNAME,name(),HISTNUM)) {
			printf("%s gatePvData::activate: %s state=%s\n",timeStamp(),name(),
			  getStateName());
		}
#endif

	switch(getState())
	{
	case gatePvInactive:
		gateDebug1(10,"gatePvData::activate() %s PV\n",getStateName());
		vc=vcd;
		setState(gatePvActive);
#ifdef STAT_PVS
		mrg->setStat(statActive,++mrg->total_active);
		mrg->setStat(statInactive,--mrg->total_inactive);
#endif
		setActiveTime();
		vc->setReadAccess(ca_read_access(chID)?aitTrue:aitFalse);
		vc->setWriteAccess(ca_write_access(chID)?aitTrue:aitFalse);
		if(!global_resources->getCacheMode()) {
		    vc->vcAdd(noCacheType);
		    rc=0;
		}
		else{
			markAddRemoveNeeded();
            if (ca_read_access(chID)) rc = get(ctrlType);
            else rc = 0;
		}
		break;
	case gatePvDisconnect:
	case gatePvDead:
		gateDebug1(3,"gatePvData::activate() %s PV ?\n",getStateName());
		vc=NULL; // NOTE: be sure vc does not respond
		break;
	case gatePvActive:
		gateDebug1(2,"gatePvData::activate() %s PV ?\n",getStateName());
		break;
	case gatePvConnect:
		// already pending, just return
		gateDebug1(3,"gatePvData::activate() %s PV ?\n",getStateName());
		markAddRemoveNeeded();
		break;
	}
	return rc;
}

int gatePvData::deactivate(void)
{
	gateDebug1(5,"gatePvData::deactivate() name=%s\n",name());
#if DEBUG_VC_DELETE
	printf("gatePvData::deactivate: %s\n",name());
#endif
	int rc=0;

	switch(getState())
	{
	case gatePvActive:
		gateDebug1(10,"gatePvData::deactivate() %s PV\n",getStateName());
		unmonitor();
		logUnmonitor();
		propUnmonitor();
		alhUnmonitor();
		setState(gatePvInactive);
#ifdef STAT_PVS
		mrg->setStat(statActive,--mrg->total_active);
		mrg->setStat(statInactive,++mrg->total_inactive);
#endif
		vc=NULL;
		setInactiveTime();
		break;
	case gatePvConnect:
		// delete from the connect pending list
		gateDebug1(10,"gatePvData::deactivate() %s PV ?\n",getStateName());
		markAddRemoveNotNeeded();
		vc=NULL;
		break;
	default:
		gateDebug1(3,"gatePvData::deactivate() %s PV ?\n",getStateName());
		rc=-1;
		break;
	}

	return rc;
}

// Called in the connectCB if ca_state is cs_conn
int gatePvData::life(void)
{
	int rc=0;
	event_count=0;

	gateDebug1(5,"gatePvData::life() name=%s\n",name());

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gatePvData::life: loop_count=%d %s state=%d\n",
		  timeStamp(),mrg->loop_count,name(),getState());
	}
#endif
#if DEBUG_HISTORY
	if(!strncmp(HISTNAME,name(),HISTNUM)) {
		printf("%s gatePvData::life: loop_count=%lu %s state=%s\n",
		  timeStamp(),mrg->loop_count,name(),getStateName());
	}
#endif

	switch(getState())
	{
	case gatePvConnect:
		gateDebug1(3,"gatePvData::life() %s PV\n",getStateName());
		setTimes();

		// Add PV from the connect pending list to PV list
		// The server's connectCleanup() routine will just delete active
		// PVs from the connecting PV list
		mrg->pvAdd(pv_name,*this);

		if(needAddRemove())	{
			if(vc) {
				setState(gatePvActive);
#ifdef STAT_PVS
				mrg->setStat(statConnecting,--mrg->total_connecting);
				mrg->setStat(statUnconnected,--mrg->total_unconnected);
				mrg->setStat(statActive,++mrg->total_active);
				mrg->setStat(statAlive,++mrg->total_alive);
#endif
				if(global_resources->getCacheMode()) get(ctrlType);
			}
		} else {
			setState(gatePvInactive);
#ifdef STAT_PVS
			mrg->setStat(statConnecting,--mrg->total_connecting);
			mrg->setStat(statUnconnected,--mrg->total_unconnected);
			mrg->setStat(statInactive,++mrg->total_inactive);
			mrg->setStat(statAlive,++mrg->total_alive);
#endif
			markNoAbort();
		}

		// Flush any accumulated exist tests
		if(eio.count()) flushAsyncETQueue(pverExistsHere);

#if DEBUG_PV_LIST
		{
		    printf("%s gatePvData::life: [%lu|%lu|%lu,%lu|%lu,%lu,%lu]: name=%s "
				   "state=%s\n",
		      timeStamp(),
			  mrg->total_vc,mrg->total_pv,mrg->total_active,mrg->total_inactive,
			  mrg->total_connecting,mrg->total_dead,mrg->total_disconnected,
			  name,getStateName());
		}
#endif
		break;

	case gatePvDisconnect:
		gateDebug1(3,"gatePvData::life() %s PV\n",getStateName());
		setReconnectTime();
		setAliveTime();
		setState(gatePvInactive);
#ifdef STAT_PVS
		mrg->setStat(statDisconnected,--mrg->total_disconnected);
		mrg->setStat(statUnconnected,--mrg->total_unconnected);
		mrg->setStat(statInactive,++mrg->total_inactive);
		mrg->setStat(statAlive,++mrg->total_alive);
#endif
		// Generate a beacon anomaly.  Rely on the gateServer to prevent
		// this from happening too frequently.
		mrg->generateBeaconAnomaly();
		break;
	case gatePvDead:
		gateDebug1(3,"gatePvData::life() %s PV\n",getStateName());
		setAliveTime();
		setState(gatePvInactive);
#ifdef STAT_PVS
		mrg->setStat(statDead,--mrg->total_dead);
		mrg->setStat(statUnconnected,--mrg->total_unconnected);
		mrg->setStat(statInactive,++mrg->total_inactive);
		mrg->setStat(statAlive,++mrg->total_alive);
#endif
		break;

	case gatePvInactive:
	case gatePvActive:
		gateDebug1(2,"gatePvData::life() %s PV ?\n",getStateName());
		rc=-1;
		break;

	default:
		break;
	}

	return rc;
}

// Called in the connectCB if ca_state is not cs_conn
// or from the gateServer's connectCleanup if the connect timeout has elapsed
int gatePvData::death(void)
{
	int rc=0;
	event_count=0;

	gateDebug1(5,"gatePvData::death() name=%s\n",name());
	gateDebug1(3,"gatePvData::death() %s PV\n",getStateName());

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gatePvData::death: loop_count=%d %s state=%d\n",
		  timeStamp(),mrg->loop_count,name(),getState());
	}
#endif
#if DEBUG_HISTORY
	if(!strncmp(HISTNAME,name(),HISTNUM)) {
		printf("%s gatePvData::death: loop_count=%lu %s state=%s\n",
		  timeStamp(),mrg->loop_count,name(),getStateName());
	}
#endif

	switch(getState())
	{
	case gatePvActive:
		// Get rid of VC
		if(vc) {
			delete vc;
			vc=NULL;
		}
		setState(gatePvDisconnect);
#ifdef STAT_PVS
		mrg->setStat(statActive,--mrg->total_active);
		mrg->setStat(statAlive,--mrg->total_alive);
		mrg->setStat(statDisconnected,++mrg->total_disconnected);
		mrg->setStat(statUnconnected,++mrg->total_unconnected);
#endif
		break;
	case gatePvInactive:
		setState(gatePvDisconnect);
#ifdef STAT_PVS
		mrg->setStat(statInactive,--mrg->total_inactive);
		mrg->setStat(statAlive,--mrg->total_alive);
		mrg->setStat(statDisconnected,++mrg->total_disconnected);
		mrg->setStat(statUnconnected,++mrg->total_unconnected);
#endif
		break;
	case gatePvConnect:
		// Flush any accumulated exist tests
		if(eio.count()) flushAsyncETQueue(pverDoesNotExistHere);

		if(needAddRemove() && vc) {
			// Should never be the case
			delete vc;
			vc=NULL;
		}

		// Leave PV on connecting list, add to the PV list as dead
		// Server's connectCleanup() will remove the PV from the
		// connecting PV list
		mrg->pvAdd(pv_name,*this);
		setState(gatePvDead);
#ifdef STAT_PVS
		mrg->setStat(statConnecting,--mrg->total_connecting);
		mrg->setStat(statDead,++mrg->total_dead);
#endif

#if DEBUG_PV_LIST
		printf("%s gatePvData::death: [%lu|%lu|%lu,%lu|%lu,%lu,%lu]: name=%s state=%s\n",
		  timeStamp(),
		  mrg->total_vc,mrg->total_pv,mrg->total_active,mrg->total_inactive,
		  mrg->total_connecting,mrg->total_dead,mrg->total_disconnected,
		  name,getStateName());
#endif
		break;
	default:
		rc=-1;
		break;
	}

	vc=NULL;
	setDeathTime();
	markNoAbort();
	markAddRemoveNotNeeded();
	markNoCtrlGetPending();
	markNoTimeGetPending();
	unmonitor();
	logUnmonitor();
	propUnmonitor();
	alhUnmonitor();

	return rc;
}

int gatePvData::unmonitor(void)
{
	gateDebug1(5,"gatePvData::unmonitor() name=%s\n",name());
	int rc=0;

	if(monitored())
	{
		rc=ca_clear_subscription(evID);
		if(rc != ECA_NORMAL) {
			fprintf(stderr,"%s gatePvData::unmonitor: ca_clear_subscription failed "
			  "for %s:\n"
			  " %s\n",
			  timeStamp(),name()?name():"Unknown",ca_message(rc));
		} else {
			rc=0;
		}
		markNotMonitored();
	}
	return rc;
}

int gatePvData::logUnmonitor(void)
{
	gateDebug1(5,"gatePvData::logUnmonitor() name=%s\n",name());
	int rc=0;

	if(logMonitored())
	{
		rc=ca_clear_subscription(logID);
		if(rc != ECA_NORMAL) {
			fprintf(stderr,"%s gatePvData::logUnmonitor: ca_clear_subscription failed "
			  "for %s:\n"
			  " %s\n",
			  timeStamp(),name()?name():"Unknown",ca_message(rc));
		} else {
			rc=0;
		}
		markLogNotMonitored();
	}
	return rc;
}

int gatePvData::propUnmonitor(void)
{
	gateDebug1(5,"gatePvData::propUnmonitor() name=%s\n",name());
	int rc=0;

	if(propMonitored())
	{
		rc=ca_clear_subscription(propID);
		if(rc != ECA_NORMAL) {
			fprintf(stderr,"%s gatePvData::propUnmonitor: ca_clear_subscription failed "
			  "for %s:\n"
			  " %s\n",
			  timeStamp(),name()?name():"Unknown",ca_message(rc));
		} else {
			rc=0;
		}
		markPropNotMonitored();
	}
	return rc;
}



int gatePvData::alhUnmonitor(void)
{
	gateDebug1(5,"gatePvData::alhUnmonitor() name=%s\n",name());
	int rc=0;

	if(alhMonitored())
	{
		rc=ca_clear_subscription(alhID);
		if(rc != ECA_NORMAL) {
			fprintf(stderr,"%s gatePvData::alhUnmonitor: "
			  "ca_clear_subscription failed for %s:\n"
			  " %s\n",
			  timeStamp(),name()?name():"Unknown",ca_message(rc));
		} else {
			rc=0;
		}
		markAlhNotMonitored();
	}
	return rc;
}

int gatePvData::monitor(void)
{
	gateDebug1(5,"gatePvData::monitor() name=%s\n",name());
	int rc=0;

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gatePvData::monitor: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	if(!monitored())
	{
		// gets only 1 element:
		// rc=ca_add_event(eventType(),chID,eventCB,this,&event);
		// gets native element count number of elements:

		if(ca_read_access(chID)) {
			gateDebug1(5,"gatePvData::monitor() type=%ld\n",eventType());
			rc=ca_create_subscription(eventType(),0,chID,GR->eventMask(),
			  ::eventCB,this,&evID);
			if(rc != ECA_NORMAL) {
				fprintf(stderr,"%s gatePvData::monitor: "
				  "ca_create_subscription failed for %s:\n"
				  " %s\n",
				  timeStamp(),name()?name():"Unknown",ca_message(rc));
				rc=-1;
			} else {
				rc=0;
				markMonitored();
#if OMIT_CHECK_EVENT
#else
				checkEvent();
#endif
			}
		} else {
			rc=-1;
		}
	}
	return rc;
}

int gatePvData::logMonitor(void)
{
	gateDebug1(5,"gatePvData::logMonitor() name=%s\n",name());
	int rc=0;

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gatePvData::logMonitor: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	if(!logMonitored())
	{
		// gets only 1 element:
		// rc=ca_add_event(eventType(),chID,eventCB,this,&event);
		// gets native element count number of elements:

		if(ca_read_access(chID)) {
			gateDebug1(5,"gatePvData::logMonitor() type=%ld\n",eventType());
			rc=ca_create_subscription(eventType(),0,chID,DBE_LOG,
			  ::logEventCB,this,&logID);
			if(rc != ECA_NORMAL) {
				fprintf(stderr,"%s gatePvData::logMonitor: "
				  "ca_create_subscription failed for %s:\n"
				  " %s\n",
				  timeStamp(),name()?name():"Unknown",ca_message(rc));
				rc=-1;
			} else {
				rc=0;
				markLogMonitored();
#if OMIT_CHECK_EVENT
#else
				checkEvent();
#endif
			}
		} else {
			rc=-1;
		}
	}
	return rc;
}

int gatePvData::propMonitor(void)
{
    gateDebug1(5,"gatePvData::propMonitor() name=%s\n",name());
    int rc=0;

#if DEBUG_DELAY
    if(!strncmp("Xorbit",name(),6)) {
        printf("%s gatePvData::propMonitor: %s state=%d\n",timeStamp(),name(),
          getState());
    }
#endif

    if(!propMonitored())
    {
        // gets only 1 element:
        // rc=ca_add_event(eventType(),chID,eventCB,this,&event);
        // gets native element count number of elements:

        if(ca_read_access(chID)) {
            gateDebug1(5,"gatePvData::propMonitor() type=%ld\n",dataType());
            rc=ca_create_subscription(dataType(),0,chID,DBE_PROPERTY,
              ::propEventCB,this,&propID);
            if(rc != ECA_NORMAL) {
                fprintf(stderr,"%s gatePvData::propMonitor: "
                  "ca_create_subscription failed for %s:\n"
                  " %s\n",
                  timeStamp(),name()?name():"Unknown",ca_message(rc));
                rc=-1;
            } else {
                rc=0;
                markPropMonitored();
#if OMIT_CHECK_EVENT
#else
                checkEvent();
#endif
            }
        } else {
            rc=-1;
        }
    }
    return rc;
}

int gatePvData::alhMonitor(void)
{
	gateDebug1(5,"gatePvData::alhMonitor() name=%s\n",name());
	int rc=0;

	if(!alhMonitored())
	{
		if(ca_read_access(chID))
		{
			gateDebug1(5,"gatePvData::alhMonitor() type=%d\n",DBR_STSACK_STRING);
			rc=ca_create_subscription(DBR_STSACK_STRING,0,chID,DBE_ALARM,
			  ::alhCB,this,&alhID);
			if(rc != ECA_NORMAL) {
				fprintf(stderr,"%s gatePvData::alhMonitor: "
				  "ca_create_subscription failed for %s:\n"
				  " %s\n",
				  timeStamp(),name()?name():"Unknown",ca_message(rc));
				rc=-1;
			} else {
				rc=0;
				markAlhMonitored();
#if OMIT_CHECK_EVENT
#else
				checkEvent();
#endif
			}
		} else {
			rc=-1;
		}
	}
	return rc;
}

int gatePvData::get(readType read_type)
{
	gateDebug1(5,"gatePvData::get() name=%s\n",name());
	int rc=ECA_NORMAL;

	// only one active get allowed at once
	switch(getState())
	{
	case gatePvActive:
		gateDebug1(3,"gatePvData::get() %s PV\n",getStateName());


		if(global_resources->getCacheMode()) /* caching enabled */
		{
			if(!pendingCtrlGet()) {
                gateDebug1(3,"gatePvData::get() CACHE doing ca_array_get_callback of type CTRL (%ld)\n",dataType());
				setTransTime();
				markCtrlGetPending();
				rc=ca_array_get_callback(dataType(), 1/*totalElements()*/,
					chID,::getCB,this);
				if(rc != ECA_NORMAL) {
					fprintf(stderr,"%s gatePvData::get: ca_array_get_callback "
					  "failed for %s:\n"
					  " %s\n",
					  timeStamp(),name()?name():"Unknown",ca_message(rc));
				}
			}
		}
		else
		{
			// do ctrl get only if requested from client
			if(read_type == ctrlType)
			{
				if(!pendingCtrlGet()) {
					/*check if array is longer than available memory*/
					if(global_resources->getMaxBytes() >= (unsigned long)(bytes*totalElements()+sizeof(caHdr) + 2 * sizeof ( ca_uint32_t ))){
                        gateDebug1(3,"gatePvData::get() NO_CACHE doing ca_array_get_callback of type CTRL (%ld)\n",dataType());
						setTransTime();
						markCtrlGetPending();
                        rc = ca_array_get_callback(dataType(), 0, chID, ::getCB, this);
						if(rc != ECA_NORMAL) {
							fprintf(stderr,"%s gatePvData::get: ca_array_get_callback "
							  "failed for %s:\n"
							  " %s\n",
							  timeStamp(),name()?name():"Unknown",ca_message(rc));
						}
					}else
					{
                      fprintf(stderr,"%s gatePvData::get: EPICS_CA_MAX_ARRAY_BYTES too small "
					  	"for %s.\n"
                        "Set EPICS_CA_MAX_ARRAY_BYTES to at least %lu\n",
                        timeStamp(),name()?name():"Unknown",
                              (unsigned long)(bytes*totalElements() + sizeof(caHdr) + 2*sizeof(ca_uint32_t)));
					}
				}
			}
			else
			{
				if(!pendingTimeGet()) {
					/*check if array is longer than available memory*/
					if(global_resources->getMaxBytes() >= (unsigned long)(bytes*totalElements()+sizeof(caHdr) + 2 * sizeof ( ca_uint32_t ))){
                        gateDebug1(3,"gatePvData::get() NO_CACHE doing ca_array_get_callback of type TIME (%ld)\n", eventType());
						setTransTime();
						markTimeGetPending();
                        rc = ca_array_get_callback(eventType(), 0, chID, ::getTimeCB, this);
						if(rc != ECA_NORMAL) {
							fprintf(stderr,"%s gatePvData::get: ca_array_get_callback for DBR_TIME "
							  "failed for %s:\n"
							  " %s\n",
							  timeStamp(),name()?name():"Unknown",ca_message(rc));
						}
					}else
					{
                      fprintf(stderr,"%s gatePvData::get: EPICS_CA_MAX_ARRAY_BYTES too small "
					  	"for %s.\n"
                        "Set EPICS_CA_MAX_ARRAY_BYTES to at least %lu\n",
                        timeStamp(),name()?name():"Unknown",
                              (unsigned long)(bytes*totalElements() + sizeof(caHdr) + 2*sizeof(ca_uint32_t)));
					}
				}
			}


#if OMIT_CHECK_EVENT
#else
			checkEvent();
#endif
		}
		break;
	default:
		gateDebug1(2,"gatePvData::get() %s PV ?\n",getStateName());
		break;
	}
	if(rc==ECA_NORMAL) return 0;
	if(rc==ECA_NORDACCESS) return 1;
	return -1;
}

// Called by gateVcData::write() and gateVcData::flushAsyncWriteQueue.
// Does a ca_array_put_callback or ca_array_put depending on
// docallback.  The former is used unless the vc is not expected to
// remain around (e.g. in its destructor).  The callback will
// eventually update the gateVcData's event_data if all goes well and
// not do so otherwise.  Returns S_casApp_success for a successful put
// and as good an error code as we can generate otherwise.  There is
// unfortunately no S_casApp return code defined for failure.
int gatePvData::put(const gdd & dd, class gateAsyncW * pWIO )
{
	gateDebug2(5,"gatePvData::put(dd=%p) name=%s\n",(void *)&dd,name());
	// KE: Check for valid index here !!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!
	chtype cht;
	gatePvCallbackId *cbid;
	aitString* str;
	const void *pValue;
	unsigned long count;
	static int full=0;

	switch(dd.applicationType())
	{
	case gddAppType_ackt:
		cht = DBR_PUT_ACKT;
		break;
	case gddAppType_acks:
		cht = DBR_PUT_ACKS;
		break;
	default:
		cht = gddAitToDbr[dd.primitiveType()];
		break;
	}

#if DEBUG_GDD
	printf("gatePvData::put(%s): at=%d pt=%d dbr=%ld ft=%ld[%s] name=%s\n",
           pWIO?"callback":"nocallback",
		   dd.applicationType(),
		   dd.primitiveType(),
		   cht,
		   fieldType(),dbr_type_to_text(fieldType()),
		   ca_name(chID));
#endif

	switch(getState())
	{
	case gatePvActive:
		caStatus stat;
		gateDebug1(3,"gatePvData::put() %s PV\n",getStateName());
		// Don't let the callback list grow forever
		if(callback_list.count() > 5000u) {
			// Only print this when it becomes full
			if(!full) {
				fprintf(stderr,"gatePvData::put:"
				  "  Callback list is full for %s\n",name());
				full=1;
				return -1;
			}
		} else {
			full=0;
		}

		setTransTime();
		switch(dd.primitiveType())
		{
		case aitEnumString:
			if(dd.isScalar())
				str=(aitString*)dd.dataAddress();
			else
				str=(aitString*)dd.dataPointer();

			// can only put one of these - arrays not valid to CA client
			count=1;
			pValue=(void *)str->string();
			gateDebug1(5," putting String <%s>\n",str->string());
			break;
		case aitEnumFixedString:     // Always a pointer
			count=dd.getDataSizeElements();
			pValue=dd.dataPointer();
			gateDebug1(5," putting FString <%s>\n",(char*)pValue);
			break;
		default:
			if(dd.isScalar()) {
				count=1;
				pValue=dd.dataAddress();
			} else {
				count=dd.getDataSizeElements();
				pValue=dd.dataPointer();
			}
			break;
		}

		// if true, its a put callback request
		if(pWIO) {
			// We need to keep track of which vc requested the put, so we
			// make a gatePvCallbackId, save it in the callback_list, and
			// use it as the puser for the callback, which is putCB.
			cbid=new (std::nothrow)
			    gatePvCallbackId(vc->getVcID(),this,pWIO);
#if DEBUG_PUT
			printf("gatePvData::put: cbid=%p this=%p dbr=%ld id=%ld pv=%p\n",
			  cbid,this,cht,cbid->getID(),cbid->getPV());
#endif
			if(!cbid) return S_casApp_noMemory;
			callback_list.add(*cbid);
#if DEBUG_SLIDER
			printf("  ca_array_put_callback [%d]: %g\n",
			  callback_list.count(),*(double *)pValue);
#endif
			stat=ca_array_put_callback(cht,count,chID,pValue,::putCB,(void *)cbid);
			if(stat != ECA_NORMAL) {
				fprintf(stderr,"%s gatePvData::put ca_array_put_callback failed "
				  "for %s:\n"
				  " %s\n",
				  timeStamp(),name()?name():"Unknown",ca_message(stat));
			}
		} else {
#if DEBUG_SLIDER
			printf("  ca_array_put: %g\n",*(double *)pValue);
#endif
			stat=ca_array_put(cht,count,chID,pValue);
			if(stat != ECA_NORMAL) {
				fprintf(stderr,"%s gatePvData::put ca_array_put failed for %s:\n"
				  " %s\n",
				  timeStamp(),name()?name():"Unknown",ca_message(stat));
			}
		}
#if OMIT_CHECK_EVENT
#else
		checkEvent();
#endif
		return (stat==ECA_NORMAL)?S_casApp_success:-1;

	default:
		gateDebug1(2,"gatePvData::put() %s PV\n",getStateName());
		return -1;
	}
}

double gatePvData::eventRate(void)
{
	time_t t = timeAlive();
	return t?(double)event_count/(double)t:0;
}

// The asynchronous exist test queue is filled from the server's
// pvExistTest() when the gatePvData is in connecting state.
// This routine, called from life() or death(), flushes the queue.
void gatePvData::flushAsyncETQueue(pvExistReturnEnum er)
{
	gateDebug1(10,"gatePvData::flushAsyncETQueue() name=%s\n",name());
	gateAsyncE* asynce;

#if DEBUG_DELAY
		if(!strncmp("Xorbit",name(),6)) {
			printf("%s gatePvData::flushAsyncETQueue: %s count=%d state=%d\n",
			  timeStamp(),name(),eio.count(),getState());
		}
#endif
#if DEBUG_HISTORY
		if(!strncmp(HISTNAME,name(),HISTNUM)) {
			printf("%s gatePvData::flushAsyncETQueue: %s count=%d state=%s\n",
			  timeStamp(),name(),eio.count(),getStateName());
		}
#endif
	while((asynce=eio.first()))	{
		gateDebug1(1,"gatePvData::flushAsyncETQueue() posting %p\n",
				   (void *)asynce);
		asynce->removeFromQueue();
		asynce->postIOCompletion(pvExistReturn(er));
	}
}

void gatePvData::connectCB(CONNECT_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);

	gateDebug1(5,"gatePvData::connectCB(gatePvData=%p)\n",(void *)pv);
	gateDebug0(9,"conCB: -------------------------------\n");
	gateDebug1(9,"conCB: name=%s\n",ca_name(args.chid));
	gateDebug1(9,"conCB: type=%d\n",ca_field_type(args.chid));
	gateDebug1(9,"conCB: number of elements=%ld\n",
	  (long)ca_element_count(args.chid));
	gateDebug1(9,"conCB: host name=%s\n",ca_host_name(args.chid));
	gateDebug1(9,"conCB: read access=%d\n",ca_read_access(args.chid));
	gateDebug1(9,"conCB: write access=%d\n",ca_write_access(args.chid));
	gateDebug1(9,"conCB: state=%d\n",ca_state(args.chid));

#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif
#if DEBUG_DELAY
	if(!strncmp("Xorbit",pv->name(),6)) {
		printf("%s gatePvData::connectCB: %s state=%d\n",timeStamp(),pv->name(),
		  pv->getState());
	}
#endif
#if DEBUG_HISTORY
	if(!strncmp(HISTNAME,pv->name(),HISTNUM)) {
		const int HOST_NAME_SZ=80;
		char hostNameStr[HOST_NAME_SZ];

		ca_get_host_name(args.chid,hostNameStr,HOST_NAME_SZ);

		printf("%s gatePvData::connectCB: %s state=%s\n",
		  timeStamp(),pv->name(),pv->getStateName());
		printf("  op=%s[%ld]\n",
		  args.op==CA_OP_CONN_UP?"CA_OP_CONN_UP":"CA_OP_CONN_DOWN",
		  args.op);
		printf("  ca_state=%d\n",ca_state(args.chid));
		printf("  ca_name=%s\n",ca_name(args.chid));
		printf("  ca_get_host_name=%s\n",hostNameStr);
		printf("  ca_field_type=%d\n",ca_field_type(args.chid));
		printf("  ca_element_count=%ld\n",
		  (long)ca_element_count(args.chid));
		printf("  ca_host_name=%s\n",ca_host_name(args.chid));
		printf("  ca_read_access=%d\n",ca_read_access(args.chid));
		printf("  ca_write_access=%d\n",ca_write_access(args.chid));
	}
#endif

#if DEBUG_ENUM
	printf("gatePvData::connectCB\n");
#endif

	// send message to user concerning connection
	if(ca_state(args.chid)==cs_conn)
	{
		gateDebug0(9,"gatePvData::connectCB() connection ok\n");

		switch(ca_field_type(args.chid))
		{
		case DBF_STRING:
			pv->data_type=DBR_STS_STRING;
			pv->event_type=DBR_TIME_STRING;
			pv->event_func=&gatePvData::eventStringCB;
			pv->data_func=&gatePvData::dataStringCB;
			pv->value_data_func=&gatePvData::dataStringCB;
			pv->bytes=sizeof(aitFixedString);
			break;
		case DBF_SHORT: // DBF_INT is same as DBF_SHORT
			pv->data_type=DBR_CTRL_SHORT;
			pv->event_type=DBR_TIME_SHORT;
			pv->event_func=&gatePvData::eventShortCB;
			pv->data_func=&gatePvData::dataShortCB;
			pv->value_data_func=&gatePvData::valueDataShortCB;
			pv->bytes=sizeof(aitInt16);
			break;
		case DBF_FLOAT:
			pv->data_type=DBR_CTRL_FLOAT;
			pv->event_type=DBR_TIME_FLOAT;
			pv->event_func=&gatePvData::eventFloatCB;
			pv->data_func=&gatePvData::dataFloatCB;
			pv->value_data_func=&gatePvData::valueDataFloatCB;
			pv->bytes=sizeof(aitFloat32);
			break;
		case DBF_ENUM:
			pv->data_type=DBR_CTRL_ENUM;
			pv->event_type=DBR_TIME_ENUM;
			pv->event_func=&gatePvData::eventEnumCB;
			pv->data_func=&gatePvData::dataEnumCB;
			pv->value_data_func=&gatePvData::valueDataEnumCB;
			pv->bytes=sizeof(aitEnum16);
			break;
		case DBF_CHAR:
			pv->data_type=DBR_CTRL_CHAR;
			pv->event_type=DBR_TIME_CHAR;
			pv->event_func=&gatePvData::eventCharCB;
			pv->data_func=&gatePvData::dataCharCB;
			pv->value_data_func=&gatePvData::valueDataCharCB;
			pv->bytes=sizeof(aitInt8);
			break;
		case DBF_LONG:
			pv->data_type=DBR_CTRL_LONG;
			pv->event_type=DBR_TIME_LONG;
			pv->event_func=&gatePvData::eventLongCB;
			pv->data_func=&gatePvData::dataLongCB;
			pv->value_data_func=&gatePvData::valueDataLongCB;
			pv->bytes=sizeof(aitInt32);
			break;
		case DBF_DOUBLE:
			pv->data_type=DBR_CTRL_DOUBLE;
			pv->event_type=DBR_TIME_DOUBLE;
			pv->event_func=&gatePvData::eventDoubleCB;
			pv->data_func=&gatePvData::dataDoubleCB;
			pv->value_data_func=&gatePvData::valueDataDoubleCB;
			pv->bytes=sizeof(aitFloat64);
			break;
		default:
#if 1
			fprintf(stderr,"gatePvData::connectCB: "
			  "Unhandled field type[%s] for %s\n",
			  dbr_type_to_text(ca_field_type(args.chid)),
			  ca_name(args.chid));
#endif
			pv->event_type=(chtype)-1;
			pv->data_type=(chtype)-1;
			pv->event_func=(gateCallback)NULL;
			pv->data_func=(gateCallback)NULL;
			pv->value_data_func=(gateCallback)NULL;
			break;
		}
		pv->max_elements=pv->totalElements();
		pv->life();
	}
	else
	{
		gateDebug0(9,"gatePvData::connectCB() connection dead\n");
		pv->death();
	}
    pv->mrg->conDelete(pv->name(), pv);
}

// This is the callback that is called when ca_array_put_callback is
// used in put().  It must be a static function and gets the pointer
// to the particular gatePvData that called it as well the vcID of the
// originating vc in that gatePvData from the args.usr, which is a
// pointer to a gatePvConnectId.  It uses the vcID to check that the
// vc which originated the put is still the current one, in which case
// if all is well, it will call the vc's putCB to update its
// event_data.  Otherwise, we would be trying to update a gateVcData
// that is gone and get errors.  The gatePvCallbackId's are stored in
// a list in the gatePvData since they must remain around until this
// callback runs.  If we did not need the vcID to check the vc, we
// could have avoided all this and just passed the pointer to the
// gatePvData as the args.usr.  Note that we can also get the
// gatePvData from ca_puser(args.chid), and it is perhaps not
// necessary to include the GatePvData this pointer in the
// gatePvConnectId.  We will leave it this way for now.
void gatePvData::putCB(EVENT_ARGS args)
{
	gateDebug1(5,"gatePvData::putCB(gatePvData=%p)\n",ca_puser(args.chid));

	// Get the callback id
	gatePvCallbackId* cbid=(gatePvCallbackId *)args.usr;
	if(!cbid) {
     // Unexpected error
		fprintf(stderr,"gatePvData::putCB: gatePvCallbackId pointer is NULL\n");
		return;
	}

	// Get the information from the callback id
	unsigned long vcid=cbid->getID();
	gatePvData *pv=cbid->getPV();
	if(!pv) {
     // Unexpected error
		fprintf(stderr,"gatePvData::putCB: gatePvData pointer is NULL\n");
		return;
	}

#if DEBUG_PUT
	printf("gatePvData::putCB: cbid=%p user=%p id=%ld pv=%p\n",
	  cbid,ca_puser(args.chid),cbid->getID(),cbid->getPV());
#endif

	// We are through with the callback id.  Remove it from the
	// callback_list and delete it.
	pv->callback_list.remove(*cbid);
	gateAsyncW * pWIO = reinterpret_cast < gateAsyncW * >
				( cbid->getPrivatePtr () );
	delete cbid;

	if (!pWIO) return;
	// Check if the originating vc is still around.
	if(!pv->vc || pv->vc->getVcID() != vcid) return;

#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif
    // The originating vc is still around.  Let it handle it.
	pv->vc->putCB(args.status,*pWIO);
}

// This is the callback registered with ca_create_subscription in the
// monitor routine.  If conditions are right, it calls the routines
// that copy the data into the GateVcData's event_data.
void gatePvData::eventCB(EVENT_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);
    gateDebug3(5,"gatePvData::eventCB(gatePvData=%p) type=%d, count=%lu\n",
      (void *)pv, (unsigned int)args.type, args.count);
	gdd* dd;
	readType read_type = timeType;
	int stat_sevr_changed = 1;

#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif

#if DEBUG_BEAM
	printf("gatePvData::eventCB(): status=%d %s\n",
	  args.status,
	  pv->name());
#endif

#if DEBUG_DELAY
	if(!strncmp("Xorbit",pv->name(),6)) {
		printf("%s gatePvData::eventCB: %s state=%d\n",timeStamp(),pv->name(),
		  pv->getState());
	}
#endif

	if(args.status==ECA_NORMAL)
	{
		// only sends event_data and does ADD transactions
		if(pv->active())
		{
			gateDebug1(5,"gatePvData::eventCB() %s PV\n",pv->getStateName());
            dd = pv->runEventCB(&args);
            if (dd)
			{
#if DEBUG_BEAM
				printf("  dd=%p needAddRemove=%d\n",
					   dd,
					   pv->needAddRemove());
#endif
#if DEBUG_GDD
                dumpdd(1, "gatePvData::eventCB setEventData", pv->name(), dd);
#endif
				stat_sevr_changed = pv->vc->setEventData(dd);

				if(pv->needAddRemove())
				{
					gateDebug0(5,"gatePvData::eventCB() need add/remove\n");
					pv->markAddRemoveNotNeeded();
					if(!global_resources->getCacheMode())
						pv->vc->vcAdd(read_type);
					else{
						read_type = ctrlType;
						pv->vc->vcAdd(read_type);
					}
				}
				else
				{

					if(global_resources->getArchiveMode()){
						// Post the event
						if(stat_sevr_changed)
							pv->vc->vcPostEvent(pv->value_alarm_mask);
						else
							pv->vc->vcPostEvent(pv->value_mask);
					}
					else{
						// Post the event
						if(stat_sevr_changed)
							pv->vc->vcPostEvent(pv->select_mask);
						else
							pv->vc->vcPostEvent(pv->value_log_mask);
					}
				}
			}
		}
		++(pv->event_count);
	}
}

void gatePvData::logEventCB(EVENT_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);
	gateDebug2(5,"gatePvData::LogEventCB(gatePvData=%p) type=%d\n",
	  (void *)pv, (unsigned int)args.type);
	gdd* dd;
	readType read_type = timeType;


#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif

#if DEBUG_BEAM
	printf("gatePvData::LogEventCB(): status=%d %s\n",
	  args.status,
	  pv->name());
#endif

#if DEBUG_DELAY
	if(!strncmp("Xorbit",pv->name(),6)) {
		printf("%s gatePvData::LogEventCB: %s state=%d\n",timeStamp(),pv->name(),
		  pv->getState());
	}
#endif

	if(args.status==ECA_NORMAL)
	{
		// only sends event_data and does ADD transactions
		if(pv->active())
		{
			gateDebug1(5,"gatePvData::LogEventCB() %s PV\n",pv->getStateName());
            dd = pv->runEventCB(&args);
            if (dd)
			{
#if DEBUG_BEAM
				printf("  dd=%p needAddRemove=%d\n",
					   dd,
					   pv->needAddRemove());
#endif
#if DEBUG_GDD
                dumpdd(1, "gatePvData::logEventCB setEventData", pv->name(), dd);
#endif
				pv->vc->setEventData(dd);

				if(pv->needAddRemove())
				{
					gateDebug0(5,"gatePvData::LogEventCB() need add/remove\n");
					pv->markAddRemoveNotNeeded();
					if(!global_resources->getCacheMode())
						pv->vc->vcAdd(read_type);
					else{
						read_type = ctrlType;
						pv->vc->vcAdd(read_type);
					}
				}
				else
				{
					// Post the event
					pv->vc->vcPostEvent(pv->mrg->logEventMask());
				}
			}
		}
		++(pv->event_count);
	}
}

void gatePvData::propEventCB(EVENT_ARGS args)
{
    gatePvData* pv=(gatePvData*)ca_puser(args.chid);
    gateDebug3(5,"gatePvData::propEventCB(gatePvData=%p)(gateVCData=%p) type=%d\n",
      (void *)pv, (void*)pv->vc, (unsigned int)args.type);
    gdd* dd;

#ifdef RATE_STATS
    ++pv->mrg->client_event_count;
#endif

#if DEBUG_BEAM
    printf("gatePvData::propEventCB(): status=%d %s\n",
      args.status,
      pv->name());
#endif

#if DEBUG_DELAY
    if(!strncmp("Xorbit",pv->name(),6)) {
        printf("%s gatePvData::propEventCB: %s state=%d\n",timeStamp(),pv->name(),
          pv->getState());
    }
#endif

    if(args.status==ECA_NORMAL)
    {
        // only sends event_data and does ADD transactions
        if(pv->active())
        {
            gateDebug3(5,"gatePvData::propEventCB() %s PV %s propGetPending %d\n",pv->getStateName(),pv->name(),pv->propGetPending());
            if(pv->propGetPending()) {
                gateDebug1(5,"gatePvData::propEventCB() Ignore first propEvent %s PV\n",pv->getStateName());
                pv->markPropNoGetPending();
                return;
            }

            gateDebug1(3,"gatePvData::propEventCB() %s PV runDataCB\n",pv->getStateName());
            if ((dd = pv->runDataCB(&args)))  // Create the attributes gdd
            {
#if DEBUG_BEAM
                printf("  dd=%p needAddRemove=%d\n", dd, pv->needAddRemove());
#endif
                // Update attribute cache
                gateDebug2(3,"gatePvData::propEventCB() %s PV %s setPvData\n",pv->getStateName(),pv->name());
#if DEBUG_ENUM
                dumpdd(1, "gatePvData::propEventCB setPvData", pv->name(), dd);
#endif
                pv->vc->setPvData(dd);
            }

            gateDebug2(4,"gatePvData::propEventCB() %s PV %s runValueDataCB\n",pv->getStateName(),pv->name());
            if ((dd = pv->runValueDataCB(&args)))  // Create the value gdd
            {
#if DEBUG_BEAM
                printf("  dd=%p needAddRemove=%d\n", dd, pv->needAddRemove());
#endif
                gateDebug2(3,"gatePvData::propEventCB() %s PV %s setEventData\n",pv->getStateName(),pv->name());
#if DEBUG_ENUM
                dumpdd(1, "gatePvData::propEventCB setEventData", pv->name(), dd);
#endif
                // This dd will have an undefined timeStamp as it comes
                // from a dbr_ctrl_* structure
                pv->vc->setEventData(dd);   // Create new setPropData()???

                if (pv->needAddRemove())
                {
                    gateDebug0(5,"gatePvData::propEventCB() need add/remove\n");
                    pv->markAddRemoveNotNeeded();
                    pv->vc->vcAdd(ctrlType);
                }
                else
                {
                    // Post the event
                    pv->vc->vcPostEvent(pv->mrg->propertyEventMask());
                }
            }
        }
        ++(pv->event_count);
    }
}


// This is the callback registered with ca_add_subscription in the
// alhMonitor routine.  If conditions are right, it calls the routines
// that copy the data into the GateVcData's event_data.
void gatePvData::alhCB(EVENT_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);
	gateDebug2(5,"gatePvData::alhCB(gatePvData=%p) type=%d\n",
	  (void *)pv, (unsigned int)args.type);
	gdd* dd;

#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif

#if DEBUG_BEAM
	printf("gatePvData::alhCB(): status=%d %s\n",
	  args.status,
	  pv->name());
#endif

	if(args.status==ECA_NORMAL)
	{
		// only sends event_data and does ADD transactions
		if(pv->active())
		{
			gateDebug1(5,"gatePvData::alhCB() %s PV\n",pv->getStateName());
			if((dd=pv->eventSTSAckStringCB((dbr_stsack_string*)args.dbr)))
			{
#if DEBUG_BEAM
				printf("  dd=%p needAddRemove=%d\n",
					   dd,
					   pv->needAddRemove());
#endif
				// Flush flushAsyncAlhReadQueue and vcPostEvent are
				// handled in setAlhData, unlike in the eventCB
				pv->vc->setAlhData(dd);

				if(pv->alhGetPending())
				{
					pv->markAlhNoGetPending();
					pv->vc->markAlhDataAvailable();
				}
			}
		}
		++(pv->event_count);
	}
}

void gatePvData::getCB(EVENT_ARGS args)
{
    gatePvData* pv=(gatePvData*)ca_puser(args.chid);
    gateDebug1(5,"gatePvData::getCB(gatePvData=%p)\n",(void *)pv);
    gdd* dd;
    readType read_type = ctrlType;


#ifdef RATE_STATS
    ++pv->mrg->client_event_count;
#endif

#if DEBUG_ENUM
    printf("gatePvData::getCB\n");
#endif

#if DEBUG_DELAY
    if(!strncmp("Xorbit",pv->name(),6)) {
        printf("%s gatePvData::getCB: %s state=%d\n",timeStamp(),pv->name(),
               pv->getState());
    }
#endif


    pv->markNoCtrlGetPending();
    if (pv->active()) {
        if (args.status == ECA_NORMAL) {
            gateDebug2(5,"gatePvData::getCB() %s PV %s runDataCB\n",pv->getStateName(),pv->name());
            // Update property cache with received property data
            dd = pv->runDataCB(&args);
            if (dd)
                pv->vc->setPvData(dd);
        }

        if (global_resources->getCacheMode()) {

            // CACHING mode

            gateDebug0(5,"gatePvData::getCB() [CACHE] Enable value monitor\n");
            pv->monitor();
            gateDebug0(5,"gatePvData::getCB() [CACHE] Enable property monitor\n");
            pv->propMonitor();

            if (pv->vc->needPosting() &&  // enable archive monitor only if in archive mode and requested
                    global_resources->getArchiveMode() &&
                    (pv->vc->client_mask == DBE_LOG)) {
                gateDebug0(5,"gatePvData::getCB() [CACHE] Enable log monitor\n");
                pv->logMonitor();
            }

        } else {

            // NO CACHING mode

            if (args.status == ECA_NORMAL) {
                // Update value cache with received data
                gateDebug2(3,"gatePvData::getCB() %s PV %s runValueDataCB\n",pv->getStateName(),pv->name());
                dd = pv->runValueDataCB(&args);
                if (dd) {
#if DEBUG_GDD
                    dumpdd(1, "gatePvData::getCB runValueDataCB setEventData", pv->name(), dd);
#endif
                    pv->vc->setEventData(dd);
                }

                if (pv->needAddRemove() && !pv->vc->needPosting()) {
                    gateDebug0(5, "gatePvData::getCB() need add/remove\n");
                    pv->markAddRemoveNotNeeded();
                    pv->vc->vcAdd(read_type);
                } else
                    pv->vc->vcData(read_type);
            }

            if (pv->vc->needPosting()) {  // enable monitor only if requested
                gateDebug0(5,"gatePvData::getCB() [NO_CACHE] Enable value monitor\n");
                pv->monitor();
            }

            if (pv->vc->needPosting() &&  // enable archive monitor only if in achive mode and requested
                    global_resources->getArchiveMode() &&
                    (pv->vc->client_mask == DBE_LOG)) {
                gateDebug0(5,"gatePvData::getCB() [NO_CACHE] Enable log monitor\n");
                pv->logMonitor();
            }

            if (pv->vc->needPosting() &&  // enable property monitor only if requested
                    (pv->vc->client_mask == DBE_PROPERTY)) {
                gateDebug0(5,"gatePvData::getCB() [NO_CACHE] Enable property monitor\n");
                pv->propMonitor();
            }
        }
    }
}

void gatePvData::getTimeCB(EVENT_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);
	gateDebug1(5,"gatePvData::getTimeCB(gatePvData=%p)\n",(void *)pv);
	gdd* dd;
	readType read_type = timeType;


#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif

#if DEBUG_ENUM
	printf("gatePvData::getTimeCB\n");
#endif

#if DEBUG_DELAY
	if(!strncmp("Xorbit",pv->name(),6)) {
		printf("%s gatePvData::getTimeCB: %s state=%d\n",timeStamp(),pv->name(),
		  pv->getState());
	}
#endif

	pv->markNoTimeGetPending();
	if(args.status==ECA_NORMAL)
	{


		if(pv->active())
		{
			gateDebug1(5,"gatePvData::getTimeCB() %s PV\n",pv->getStateName());
            dd = pv->runEventCB(&args);
            if (dd) {
#if DEBUG_GDD
                dumpdd(1, "gatePvData::getTimeCB setEventData", pv->name(), dd);
#endif
                pv->vc->setEventData(dd);
            }

			/* flush async get request */
			if(pv->needAddRemove() && !pv->vc->needPosting())
			{
				gateDebug0(5,"gatePvData::getTimeCB() need add/remove\n");
				pv->markAddRemoveNotNeeded();
				pv->vc->vcAdd(read_type);
			}
			else
				pv->vc->vcData(read_type);

			if(pv->vc->needPosting() && !pv->monitored()) // do monitor only if requested
			{
				pv->monitor();
			}

			if(pv->vc->needPosting() &&  // do archive monitor only if requested
			   global_resources->getArchiveMode() &&
			   !pv->logMonitored() &&
			    (pv->vc->client_mask == DBE_LOG)) {
			        gateDebug0(5,"gatePvData::getCB() Starting log monitor timecb\n");
                    pv->logMonitor();
                }

			if(pv->vc->needPosting() &&  // do property monitor only if requested
			   !pv->propMonitored() &&
			    (pv->vc->client_mask == DBE_PROPERTY)) {
			        gateDebug0(5,"gatePvData::getCB() Starting prop monitor timecb\n");
                    pv->propMonitor();
                }

		}
	}
}

void gatePvData::accessCB(ACCESS_ARGS args)
{
	gatePvData* pv=(gatePvData*)ca_puser(args.chid);
	gateVcData* vc=pv->VC();
    gateDebug1(5,"gatePvData::accessCB(gatePvData=%p)\n",(void *)pv);

#ifdef RATE_STATS
	++pv->mrg->client_event_count;
#endif

#if DEBUG_DELAY
	if(!strncmp("Xorbit",pv->name(),6)) {
		printf("%s gatePvData::accessCB: %s vc=%d state=%d\n",timeStamp(),
		  pv->name(),vc,pv->getState());
	}
#endif

	// sets general read/write permissions for the gateway itself
	if(vc)
	{
		vc->setReadAccess(ca_read_access(args.chid)?aitTrue:aitFalse);
		vc->setWriteAccess(ca_write_access(args.chid)?aitTrue:aitFalse);
	}

	gateDebug0(9,"accCB: -------------------------------\n");
	gateDebug1(9,"accCB: name=%s\n",ca_name(args.chid));
	gateDebug1(9,"accCB: type=%d\n",ca_field_type(args.chid));
	gateDebug1(9,"accCB: number of elements=%ld\n",
	  (long)ca_element_count(args.chid));
	gateDebug1(9,"accCB: host name=%s\n",ca_host_name(args.chid));
	gateDebug1(9,"accCB: read access=%d\n",ca_read_access(args.chid));
	gateDebug1(9,"accCB: write access=%d\n",ca_write_access(args.chid));
	gateDebug1(9,"accCB: state=%d\n",ca_state(args.chid));
}

// one function for each of the different data that come from gets:
//  DBR_STS_STRING
//  DBR_CTRL_ENUM
//  DBR_CTRL_CHAR
//  DBR_CTRL_DOUBLE
//  DBR_CTRL_FLOAT
//  DBR_CTRL_LONG
//  DBR_CTRL_SHORT (DBR_CTRL_INT)

gdd* gatePvData::dataStringCB(EVENT_ARGS * /*pArgs*/)
{
	gateDebug0(4,"gatePvData::dataStringCB\n");
	// no useful pv_data returned by this function
	return NULL;
}

gdd* gatePvData::dataEnumCB(EVENT_ARGS * pArgs)
{
	gateDebug0(4,"gatePvData::dataEnumCB\n");
	int i;
    dbr_ctrl_enum* ts = (dbr_ctrl_enum*)pArgs->dbr;
	aitFixedString* items = new aitFixedString[ts->no_str];
	gddAtomic* menu=new gddAtomic(GR->appEnum,aitEnumFixedString,1,ts->no_str);

#if DEBUG_ENUM
	printf("gatePvData::dataEnumCB: no_str=%d\n",ts->no_str);
	for(i=0; i<ts->no_str; i++) {
		printf("  %s\n",ts->strs[i]);
	}
#endif

	// DBR_CTRL_ENUM response
	for (i=0;i<ts->no_str;i++) {
		strncpy(items[i].fixed_string,&(ts->strs[i][0]),
		  sizeof(aitFixedString));
		items[i].fixed_string[sizeof(aitFixedString)-1u] = '\0';
	}

	menu->putRef(items,new gateFixedStringDestruct());
#if DEBUG_ENUM
#endif
	return menu;
}

gdd* gatePvData::dataDoubleCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::dataDoubleCB\n");
    dbr_ctrl_double* ts = (dbr_ctrl_double*)pArgs->dbr;
	gdd* attr = GETDD(appAttributes);

	// DBR_CTRL_DOUBLE response
	attr[gddAppTypeIndex_attributes_units].put(ts->units);
	attr[gddAppTypeIndex_attributes_maxElements]=maxElements();
	attr[gddAppTypeIndex_attributes_precision]=ts->precision;
	attr[gddAppTypeIndex_attributes_graphicLow]=ts->lower_disp_limit;
	attr[gddAppTypeIndex_attributes_graphicHigh]=ts->upper_disp_limit;
	attr[gddAppTypeIndex_attributes_controlLow]=ts->lower_ctrl_limit;
	attr[gddAppTypeIndex_attributes_controlHigh]=ts->upper_ctrl_limit;
	attr[gddAppTypeIndex_attributes_alarmLow]=ts->lower_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmHigh]=ts->upper_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmLowWarning]=ts->lower_warning_limit;
	attr[gddAppTypeIndex_attributes_alarmHighWarning]=ts->upper_warning_limit;
	return attr;
}

gdd* gatePvData::dataShortCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::dataShortCB\n");
    dbr_ctrl_short* ts = (dbr_ctrl_short*)pArgs->dbr;
	gdd* attr = GETDD(appAttributes);

	// DBR_CTRL_SHORT DBT_CTRL_INT response
	attr[gddAppTypeIndex_attributes_units].put(ts->units);
	attr[gddAppTypeIndex_attributes_maxElements]=maxElements();
	attr[gddAppTypeIndex_attributes_precision]=0;
	attr[gddAppTypeIndex_attributes_graphicLow]=ts->lower_disp_limit;
	attr[gddAppTypeIndex_attributes_graphicHigh]=ts->upper_disp_limit;
	attr[gddAppTypeIndex_attributes_controlLow]=ts->lower_ctrl_limit;
	attr[gddAppTypeIndex_attributes_controlHigh]=ts->upper_ctrl_limit;
	attr[gddAppTypeIndex_attributes_alarmLow]=ts->lower_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmHigh]=ts->upper_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmLowWarning]=ts->lower_warning_limit;
	attr[gddAppTypeIndex_attributes_alarmHighWarning]=ts->upper_warning_limit;
	return attr;
}

gdd* gatePvData::dataFloatCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::dataFloatCB\n");
    dbr_ctrl_float* ts = (dbr_ctrl_float*)pArgs->dbr;
	gdd* attr = GETDD(appAttributes);

	// DBR_CTRL_FLOAT response
	attr[gddAppTypeIndex_attributes_units].put(ts->units);
	attr[gddAppTypeIndex_attributes_maxElements]=maxElements();
	attr[gddAppTypeIndex_attributes_precision]=ts->precision;
	attr[gddAppTypeIndex_attributes_graphicLow]=ts->lower_disp_limit;
	attr[gddAppTypeIndex_attributes_graphicHigh]=ts->upper_disp_limit;
	attr[gddAppTypeIndex_attributes_controlLow]=ts->lower_ctrl_limit;
	attr[gddAppTypeIndex_attributes_controlHigh]=ts->upper_ctrl_limit;
	attr[gddAppTypeIndex_attributes_alarmLow]=ts->lower_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmHigh]=ts->upper_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmLowWarning]=ts->lower_warning_limit;
	attr[gddAppTypeIndex_attributes_alarmHighWarning]=ts->upper_warning_limit;
	return attr;
}

gdd* gatePvData::dataCharCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::dataCharCB\n");
    dbr_ctrl_char* ts = (dbr_ctrl_char*)pArgs->dbr;
	gdd* attr = GETDD(appAttributes);

	// DBR_CTRL_CHAR response
	attr[gddAppTypeIndex_attributes_units].put(ts->units);
	attr[gddAppTypeIndex_attributes_maxElements]=maxElements();
	attr[gddAppTypeIndex_attributes_precision]=0;
	attr[gddAppTypeIndex_attributes_graphicLow]=ts->lower_disp_limit;
	attr[gddAppTypeIndex_attributes_graphicHigh]=ts->upper_disp_limit;
	attr[gddAppTypeIndex_attributes_controlLow]=ts->lower_ctrl_limit;
	attr[gddAppTypeIndex_attributes_controlHigh]=ts->upper_ctrl_limit;
	attr[gddAppTypeIndex_attributes_alarmLow]=ts->lower_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmHigh]=ts->upper_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmLowWarning]=ts->lower_warning_limit;
	attr[gddAppTypeIndex_attributes_alarmHighWarning]=ts->upper_warning_limit;
	return attr;
}

gdd* gatePvData::dataLongCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::dataLongCB\n");
    dbr_ctrl_long* ts = (dbr_ctrl_long*)pArgs->dbr;
	gdd* attr = GETDD(appAttributes);

	// DBR_CTRL_LONG response
	attr[gddAppTypeIndex_attributes_units].put(ts->units);
	attr[gddAppTypeIndex_attributes_maxElements]=maxElements();
	attr[gddAppTypeIndex_attributes_precision]=0;
	attr[gddAppTypeIndex_attributes_graphicLow]=ts->lower_disp_limit;
	attr[gddAppTypeIndex_attributes_graphicHigh]=ts->upper_disp_limit;
	attr[gddAppTypeIndex_attributes_controlLow]=ts->lower_ctrl_limit;
	attr[gddAppTypeIndex_attributes_controlHigh]=ts->upper_ctrl_limit;
	attr[gddAppTypeIndex_attributes_alarmLow]=ts->lower_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmHigh]=ts->upper_alarm_limit;
	attr[gddAppTypeIndex_attributes_alarmLowWarning]=ts->lower_warning_limit;
	attr[gddAppTypeIndex_attributes_alarmHighWarning]=ts->upper_warning_limit;
	return attr;
}

// one function for each of the different events that come from monitors:
//  DBR_TIME_STRING
//  DBR_TIME_ENUM
//  DBR_TIME_CHAR
//  DBR_TIME_DOUBLE
//  DBR_TIME_FLOAT
//  DBR_TIME_LONG
//  DBR_TIME_SHORT (DBR_TIME_INT)
//  DBR_STSACK_STRING (alarm info)

gdd* gatePvData::eventStringCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventStringCB\n");
    dbr_time_string* ts = (dbr_time_string*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;


	// DBR_TIME_STRING response
    if (maxCount > 1)
	{
		// KE: For arrays of strings.  This case was not originally
		// included and was added 11-2004.  It uses aitFixedString
		// whereas the count=1 case uses aitString, which is a class.
		// It is not commonly used, so if this implementation is
		// wrong, it may be awhile before it is discovered.
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitFixedString *d, *nd;
        nd = new aitFixedString[maxCount];
		d=(aitFixedString*)&ts->value;
        memcpy(nd, d, count * sizeof(aitFixedString));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitFixedString));
        value = new gddAtomic(GR->appValue, aitEnumFixedString, 1, &count);
		value->putRef(nd,new gateFixedStringDestruct());
	}
	else
	{
		value=new gddScalar(GR->appValue, aitEnumString);
		aitString* str = (aitString*)value->dataAddress();
		str->copy(ts->value);
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventEnumCB(EVENT_ARGS * pArgs)
{
    gateDebug1(10,"gatePvData::eventEnumCB PV %s\n", name());
    dbr_time_enum* ts = (dbr_time_enum*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

#if DEBUG_ENUM
	printf("gatePvData::eventEnumCB\n");
#endif

	// DBR_TIME_ENUM response
    if (maxCount > 1)
	{
		// KE: For arrays of enums.  This case was not originally
		// included and was added 11-2004.  It is not commonly, if
		// ever, used, so if this implementation is wrong, it may be
		// awhile before it is discovered.  The waveform can be an
		// array of enums (FTVL="ENUM"), but there is no support for
		// the menu strings, so this is an unwise thing to do.  Note
		// that the menu strings (which don't exist for the waveform)
		// are added in dataEnumCB.
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitEnum16 *d, *nd;
        nd = new aitEnum16[maxCount];
        d = (aitEnum16*)&ts->value;
        memcpy(nd, d, count * sizeof(aitEnum16));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitEnum16));
        value = new gddAtomic(GR->appValue, aitEnumInt16, 1, &count);
		value->putRef(nd,new gateEnumDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumEnum16);
		value->putConvert(ts->value);
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
#if DEBUG_ENUM
//    printf("gatePvData::eventEnumCB\n");
    dumpdd(1, "gatePvData::eventEnumCB", name(), value);
#endif
    gateDebug3(12,"gatePvData::eventEnumCB PV %s secPastEpoch=%u, nsec=%u \n", name(), ts->stamp.secPastEpoch, ts->stamp.nsec);
	return value;
}

gdd* gatePvData::eventLongCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventLongCB\n");
    dbr_time_long* ts = (dbr_time_long*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_TIME_LONG response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt32 *d, *nd;
        nd = new aitInt32[maxCount];
        d = (aitInt32*)&ts->value;
        memcpy(nd, d, count * sizeof(aitInt32));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt32));
        value = new gddAtomic(GR->appValue, aitEnumInt32, 1, &count);
		value->putRef(nd,new gateIntDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt32);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventCharCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventCharCB\n");
    dbr_time_char* ts = (dbr_time_char*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_TIME_CHAR response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt8 *d, *nd;
        nd = new aitInt8[maxCount];
        d = (aitInt8*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitInt8));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt8));
        value = new gddAtomic(GR->appValue, aitEnumInt8, 1, &count);
		value->putRef(nd,new gateCharDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt8);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventFloatCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventFloatCB\n");
    dbr_time_float* ts = (dbr_time_float*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_TIME_FLOAT response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitFloat32 *d, *nd;
        nd = new aitFloat32[maxCount];
        d = (aitFloat32*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitFloat32));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitFloat32));
        value = new gddAtomic(GR->appValue, aitEnumFloat32, 1, &count);
		value->putRef(nd,new gateFloatDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumFloat32);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventDoubleCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventDoubleCB\n");
    dbr_time_double* ts = (dbr_time_double*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

    // DBR_TIME_DOUBLE response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitFloat64 *d, *nd;
        nd = new aitFloat64[maxCount];
        d = (aitFloat64*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitFloat64));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitFloat64));
        value = new gddAtomic(GR->appValue, aitEnumFloat64, 1, &count);
		value->putRef(nd,new gateDoubleDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumFloat64);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventShortCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::eventShortCB\n");
    dbr_time_short* ts = (dbr_time_short*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_TIME_FLOAT response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt16 *d, *nd;
        nd = new aitInt16[maxCount];
        d = (aitInt16*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitInt16));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt16));
        value = new gddAtomic(GR->appValue, aitEnumInt16, 1, &count);
		value->putRef(nd,new gateShortDestruct);
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt16);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	value->setTimeStamp(&ts->stamp);
	return value;
}

gdd* gatePvData::eventSTSAckStringCB(dbr_stsack_string *ts)
{
	gateDebug0(10,"gatePvData::eventSTSAckStringCB\n");
	gdd* dd = GETDD(appSTSAckString);
	gdd& vdd = dd[gddAppTypeIndex_dbr_stsack_string_value];

	// DBR_STSACK_STRING response
	// (the value gdd carries the severity and status information)

	// change type of value gdd to native type of pv
	dd[gddAppTypeIndex_dbr_stsack_string_value].setPrimType(nativeType());

	dd[gddAppTypeIndex_dbr_stsack_string_ackt] = ts->ackt;
	dd[gddAppTypeIndex_dbr_stsack_string_acks] = ts->acks;

	vdd.setStatSevr(ts->status,ts->severity);

	return dd;
}

// one function for each of the different value type :

gdd* gatePvData::valueDataEnumCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataEnumCB\n");
    dbr_ctrl_enum* ts = (dbr_ctrl_enum*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

#if DEBUG_ENUM
	printf("gatePvData::valueDataEnumCB\n");
#endif

	// DBR_CTRL_ENUM response
    if (maxCount > 1)
	{
		// KE: For arrays of enums.  This case was not originally
		// included and was added 11-2004.  It is not commonly, if
		// ever, used, so if this implementation is wrong, it may be
		// awhile before it is discovered.  The waveform can be an
		// array of enums (FTVL="ENUM"), but there is no support for
		// the menu strings, so this is an unwise thing to do.  Note
		// that the menu strings (which don't exist for the waveform)
		// are added in dataEnumCB.
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitEnum16 *d, *nd;
        nd = new aitEnum16[maxCount];
        d = (aitEnum16*)&ts->value;
        memcpy(nd, d, count * sizeof(aitEnum16));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitEnum16));
        value = new gddAtomic(GR->appValue, aitEnumInt16, 1, &count);
		value->putRef(nd,new gateEnumDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumEnum16);
		value->putConvert(ts->value);
	}
#if DEBUG_ENUM
//    printf("gatePvData::valueDataEnumCB\n");
    dumpdd(1, "gatePvData::valueDataEnumCB", name(), value);
#endif
	value->setStatSevr(ts->status,ts->severity);
	return value;
}

gdd* gatePvData::valueDataLongCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataLongCB\n");
    dbr_ctrl_long* ts = (dbr_ctrl_long*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_CTRL_LONG response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt32 *d, *nd;
        nd = new aitInt32[maxCount];
        d = (aitInt32*)&ts->value;
        memcpy(nd, d, count * sizeof(aitInt32));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt32));
        value = new gddAtomic(GR->appValue, aitEnumInt32, 1, &count);
		value->putRef(nd,new gateIntDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt32);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	return value;
}

gdd* gatePvData::valueDataCharCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataCharCB\n");
    dbr_ctrl_char* ts = (dbr_ctrl_char*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_CTRL_CHAR response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt8 *d, *nd;
        nd = new aitInt8[maxCount];
        d = (aitInt8*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitInt8));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt8));
        value = new gddAtomic(GR->appValue, aitEnumInt8, 1, &count);
		value->putRef(nd,new gateCharDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt8);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	return value;
}

gdd* gatePvData::valueDataFloatCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataFloatCB\n");
    dbr_ctrl_float* ts = (dbr_ctrl_float*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_TIME_FLOAT response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitFloat32 *d, *nd;
        nd = new aitFloat32[maxCount];
        d = (aitFloat32*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitFloat32));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitFloat32));
        value = new gddAtomic(GR->appValue, aitEnumFloat32, 1, &count);
		value->putRef(nd,new gateFloatDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumFloat32);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	return value;
}

gdd* gatePvData::valueDataDoubleCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataDoubleCB\n");
    dbr_ctrl_double* ts = (dbr_ctrl_double*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_CTRL_DOUBLE response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitFloat64 *d,*nd;
        nd = new aitFloat64[maxCount];
        d = (aitFloat64*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitFloat64));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitFloat64));
        value = new gddAtomic(GR->appValue, aitEnumFloat64, 1, &count);
		value->putRef(nd,new gateDoubleDestruct());
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumFloat64);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);

#if DEBUG_GDD
    // Note: This gdd value will have undefined timeStamp (0,0)
    // as dbr_ctrl_double doesn't contain a timeStamp.
    dumpdd(1, "gatePvData::valueDataDoubleCB", name(), value);
#endif
	return value;
}

gdd* gatePvData::valueDataShortCB(EVENT_ARGS * pArgs)
{
	gateDebug0(10,"gatePvData::valueDataShortCB\n");
    dbr_ctrl_short* ts = (dbr_ctrl_short*)pArgs->dbr;
    aitIndex maxCount = totalElements();
	gdd* value;

	// DBR_CTRL_FLOAT response
	// set up the value
    if (maxCount > 1)
	{
        aitIndex count = ((aitUint32)pArgs->count > maxCount ? maxCount : pArgs->count);
        aitInt16 *d, *nd;
        nd = new aitInt16[maxCount];
        d = (aitInt16*)&(ts->value);
        memcpy(nd, d, count * sizeof(aitInt16));
        memset(nd + count, 0, (maxCount - count) * sizeof(aitInt16));
        value = new gddAtomic(GR->appValue, aitEnumInt16, 1, &count);
		value->putRef(nd,new gateShortDestruct);
	}
	else
	{
		value = new gddScalar(GR->appValue,aitEnumInt16);
		*value=ts->value;
	}
	value->setStatSevr(ts->status,ts->severity);
	return value;
}

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
