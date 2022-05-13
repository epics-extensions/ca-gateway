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
#ifndef _GATEPV_H_
#define _GATEPV_H_

/*+*********************************************************************
 *
 * File:       gatePv.h
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

// Used in put() to specify callback or not
#define GATE_NOCALLBACK 0
#define GATE_DOCALLBACK 1

#include <sys/types.h>

#ifdef _WIN32
#else
# include <sys/time.h>
#endif

#include <aitTypes.h>
#include <gddAppTable.h>
#include "tsDLList.h"

#include "gateAsyncIO.h"

extern "C" {
#include "cadef.h"
#include "db_access.h"
}

typedef evargs EVENT_ARGS;
typedef struct access_rights_handler_args	ACCESS_ARGS;
typedef struct connection_handler_args		CONNECT_ARGS;

typedef enum {
	gatePvDead,
	gatePvInactive,
	gatePvActive,
	gatePvConnect,
	gatePvDisconnect
} gatePvState;




// Other state information is boolean:
//	monitored state: 0=false or not monitored, 1=true or is monitored
//	get state: 0=false or no get pending, 1=true or get pending
//	test flag : true if NAK/ACK response required after completion
//	complete flag: true if ADD/REMOVE response required after completion

class gdd;
class gateVcData;
class gatePvData;
class gateServer;
class gateAsEntry;

// This class is used by gatePvData to keep track of which associated
// gateVcData initiated a put.  It stores the vcID of the gateVcData
// and the gatePvData's this pointer in a list for use by the
// gatePvData's static putCB.
class gatePvCallbackId : public tsDLNode<gatePvCallbackId>
{
public:
	gatePvCallbackId(unsigned long idIn,
	    gatePvData *pvIn, void * pPrivateIn=0 ) :
	id(idIn),pv(pvIn),pPrivate(pPrivateIn) {};
	unsigned long getID(void) const { return id; }
	gatePvData *getPV(void) const { return pv; }
	void * getPrivatePtr () const { return pPrivate; }
private:
	unsigned long id;
	gatePvData *pv;
	void * pPrivate;
};

// This class is used to manage one process variable (channel) on the
// client side of the Gateway.  It typically stays around a long time,
// whereas the associated gateVcData comes and goes as clients connect
// to the server side of the Gateway.
class gatePvData
{
public:
	gatePvData(gateServer*,gateAsEntry*,const char* name);
	~gatePvData(void);

    typedef gdd* (gatePvData::*gateCallback)(EVENT_ARGS *);

	int active(void) const { return (pv_state==gatePvActive)?1:0; }
	int inactive(void) const { return (pv_state==gatePvInactive)?1:0; }
	int disconnected(void) const { return (pv_state==gatePvDisconnect)?1:0; }
	int dead(void) const { return (pv_state==gatePvDead)?1:0; }
	int pendingConnect(void) const { return (pv_state==gatePvConnect)?1:0; }

	int pendingCtrlGet(void) const { return (ctrl_get_state)?1:0; }
	int pendingTimeGet(void) const { return (time_get_state)?1:0; }
	int monitored(void) const { return (mon_state)?1:0; }
	int logMonitored(void) const { return (log_mon_state)?1:0; }
	int propMonitored(void) const { return (prop_mon_state)?1:0; }
	int alhMonitored(void) const { return (alh_mon_state)?1:0; }
	int alhGetPending(void) const { return (alh_get_state)?1:0; }
	int propGetPending(void) const { return (prop_get_state)?1:0; }
	int logGetPending(void) const { return (log_get_state)?1:0; }
	int needAddRemove(void) const { return (complete_flag)?1:0; }
	int abort(void) const { return (abort_flag)?1:0; }

	const char* name(void) const { return pv_name; }
	gateVcData* VC(void) const { return vc; }
	gateAsEntry* getEntry(void) const { return asentry; }
	void removeEntry(void) { asentry = NULL; }
	void resetEntry(gateAsEntry *asentryIn) { asentry = asentryIn; }
	gatePvState getState(void) const { return pv_state; }
	const char* getStateName(void) const { return pv_state_names[pv_state]; }
	int getStatus(void) const { return status; }
	chid getChannel(void) const { return chID; }
	evid getEvent(void) const { return evID; }
	int getCaState(void) const { return ca_state(chID); }
	aitInt32 totalElements(void) const { return ca_element_count(chID); }
	aitUint32 maxElements(void) const { return max_elements; }
	chtype fieldType(void) const { return ca_field_type(chID); }
	aitEnum nativeType(void) const;
	chtype dataType(void) const { return data_type; }
	chtype eventType(void) const { return event_type; }
	void checkEvent(void) { ca_poll(); }
	double eventRate(void);

	int activate(gateVcData* from); // set to active (CAS connect)
	int deactivate(void);           // set to inactive (CAS disconnect)
	int death(void);                // set to not connected (CAC disconnect)
	int life(void);                 // set to connected (CAC connect)
	int monitor(void);              // add monitor
	int logMonitor(void);           // add log monitor
	int propMonitor(void);           // add prop monitor
	int unmonitor(void);            // delete monitor
	int logUnmonitor(void);         // delete log monitor
	int propUnmonitor(void);         // delete prop monitor
	int alhMonitor(void);           // add alh info monitor
	int alhUnmonitor(void);         // delete alh info monitor
	int get(readType read_type);                  // get callback
	int put(const gdd &, class gateAsyncW * );  // put

	time_t timeAlive(void) const;
	time_t timeActive(void) const;
	time_t timeInactive(void) const;
	time_t timeConnecting(void) const;
	time_t timeDead(void) const;
	time_t timeDisconnected(void) const;

#if 0
	// KE: Unused
	time_t timeSinceLastTrans(void) const;
#endif

	void setVC(gateVcData* t) { vc=t; }
	void setTransTime(void);
	void addET(const casCtx&);
	void flushAsyncETQueue(pvExistReturnEnum);

	void markAlhGetPending(void) { alh_get_state=1; }
	void markAlhNoGetPending(void) { alh_get_state=0; }
	void markPropGetPending(void) { prop_get_state=1; }
	void markPropNoGetPending(void) { prop_get_state=0; }
    void markLogGetPending(void) { log_get_state=1; }
    void markLogNoGetPending(void) { log_get_state=0; }


protected:
	void init(gateServer*,gateAsEntry *pase, const char* name);
	void initClear(void);

	void setInactiveTime(void);
	void setActiveTime(void);
	void setDeathTime(void);
	void setAliveTime(void);
	void setReconnectTime(void);
	void setTimes(void);

private:
	void markMonitored(void) { mon_state=1; }
	void markNotMonitored(void) { mon_state=0; }
	void markLogMonitored(void) { log_mon_state=1; }
	void markPropMonitored(void) { prop_mon_state=1; }
	void markLogNotMonitored(void) { log_mon_state=0; }
	void markPropNotMonitored(void) { prop_mon_state=0; }
	void markCtrlGetPending(void) { ctrl_get_state=1; }
	void markNoCtrlGetPending(void) { ctrl_get_state=0; }
	void markTimeGetPending(void) { time_get_state=1; }
	void markNoTimeGetPending(void) { time_get_state=0; }
	void markAlhMonitored(void) { alh_mon_state=1; }
	void markAlhNotMonitored(void) { alh_mon_state=0; }
	void markAddRemoveNeeded(void) { complete_flag=1; }
	void markAddRemoveNotNeeded(void) { complete_flag=0; }
	void markAbort(void) { abort_flag=1; }
	void markNoAbort(void) { abort_flag=0; }

    void setState(gatePvState s) {
        pv_state=s;
        gateDebug2(3,"gatePvData::setState PV %s: %s\n", name(), getStateName());
    }

    gdd* runEventCB(EVENT_ARGS *pArgs) { return (this->*event_func)(pArgs); }
    gdd* runDataCB(EVENT_ARGS *pArgs) { return (this->*data_func)(pArgs); }
    gdd* runValueDataCB(EVENT_ARGS *pArgs) { return (this->*value_data_func)(pArgs); }

	tsDLList<gateAsyncE> eio;  // pending exist test list
	tsDLList<gatePvCallbackId> callback_list;  // callback list for puts

	gateServer* mrg;    // The gateServer that manages this gatePvData
	gateVcData* vc;     // Pointer to the associated gateVcData, NULL if none
	gateAsEntry* asentry;
	aitUint32 max_elements;
	int status;
	char* pv_name;             // Name of the process variable
	chid chID;                 // Channel access ID
	evid evID;                 // Channel access event id
	evid logID;                 // Channel access event id
	evid propID;                 // Channel access event id
	evid alhID;                // Channel access alh info event id
	chtype event_type;         // DBR type associated with eventCB (event_data)
	chtype data_type;          // DBR type associated with getCB (pv_data)
	gatePvState pv_state;      // The state of the connection
	unsigned long event_count; // Counter for events received
	static const char* const pv_state_names[]; // State strings

	gateCallback event_func;   // Function called in eventCB for event_data
	gateCallback data_func;    // Function called in getCB for pv_data
	gateCallback value_data_func;    // Function called in getCB for pv_data

	int mon_state;     // 0=not monitored, 1=is monitored
	int log_mon_state;     // 0=not log monitored, 1=is log monitored
	int prop_mon_state;     // 0=not prop monitored, 1=is prop monitored
	int ctrl_get_state;     // 0=no ctrl get pending, 1=ctrl get pending
	int time_get_state;     // 0=no time get pending, 1=time get pending
	int alh_mon_state; // 0=alh info not monitored, 1=alh info is monitored
	int alh_get_state; // 0=no alh info get pending, 1=alh info get pending
	int prop_get_state; // 0=no prop info get pending, 1=prop info get pending
	int log_get_state; // 0=no log info get pending, 1=log info get pending
	int abort_flag;	   // true if activate-connect sequence should be aborted
	int complete_flag; // true if ADD/REMOVE required after completion

	time_t no_connect_time; // when no one connected to held PV
	time_t dead_alive_time; // when PV went from dead to alive
	time_t last_trans_time; // last transaction (put or get) occurred at this time

	//gjansa: until something better is found out
	unsigned int bytes;

	casEventMask select_mask;
	casEventMask alh_mask;
	casEventMask value_mask;
	casEventMask value_alarm_mask;
	casEventMask value_log_mask;

	// Callback functions used in eventCB
    gdd* eventStringCB(EVENT_ARGS *pArgs);
    gdd* eventEnumCB(EVENT_ARGS *pArgs);
    gdd* eventShortCB(EVENT_ARGS *pArgs);
    gdd* eventFloatCB(EVENT_ARGS *pArgs);
    gdd* eventDoubleCB(EVENT_ARGS *pArgs);
    gdd* eventCharCB(EVENT_ARGS *pArgs);
    gdd* eventLongCB(EVENT_ARGS *pArgs);
	gdd* eventSTSAckStringCB(dbr_stsack_string*);

	// Callback functions used in getCB
    gdd* dataStringCB(EVENT_ARGS *pArgs);
    gdd* dataEnumCB(EVENT_ARGS *pArgs);
    gdd* dataShortCB(EVENT_ARGS *pArgs);
    gdd* dataFloatCB(EVENT_ARGS *pArgs);
    gdd* dataDoubleCB(EVENT_ARGS *pArgs);
    gdd* dataCharCB(EVENT_ARGS *pArgs);
    gdd* dataLongCB(EVENT_ARGS *pArgs);

	// Callback functions used in getCB for value
    gdd* valueDataStringCB(EVENT_ARGS *pArgs);
    gdd* valueDataEnumCB(EVENT_ARGS *pArgs);
    gdd* valueDataShortCB(EVENT_ARGS *pArgs);
    gdd* valueDataFloatCB(EVENT_ARGS *pArgs);
    gdd* valueDataDoubleCB(EVENT_ARGS *pArgs);
    gdd* valueDataCharCB(EVENT_ARGS *pArgs);
    gdd* valueDataLongCB(EVENT_ARGS *pArgs);

public:
    static void connectCB(CONNECT_ARGS args);   // connection callback
    static void accessCB(ACCESS_ARGS args);     // access security callback
    static void eventCB(EVENT_ARGS args);       // value-changed callback
    static void logEventCB(EVENT_ARGS args);    // value-changed callback
    static void propDataCB(EVENT_ARGS args);    // DBE_PROPERTY data-changed callback
    static void propEventCB(EVENT_ARGS args);   // DBE_PROPERTY value-changed callback
    static void alhCB(EVENT_ARGS args);         // alh info value-changed callback
    static void putCB(EVENT_ARGS args);         // put callback
    static void getCB(EVENT_ARGS args);         // get callback
    static void getTimeCB(EVENT_ARGS args);     // get time callback
};

inline void gatePvData::addET(const casCtx& c)
	{ eio.add(*(new gateAsyncE(c,&eio))); }

inline time_t gatePvData::timeDisconnected(void) const
	{ return disconnected()?(time(NULL)-no_connect_time):0; }
inline time_t gatePvData::timeInactive(void) const
	{ return inactive()?(time(NULL)-no_connect_time):0; }
inline time_t gatePvData::timeActive(void) const
	{ return active()?(time(NULL)-no_connect_time):0; }
#if 0
	// KE: Unused
inline time_t gatePvData::timeSinceLastTrans(void) const
	{ return time(NULL)-last_trans_time; }
#endif

inline time_t gatePvData::timeDead(void) const
{
	time_t now=time(NULL);
	time_t x=now-dead_alive_time;
	time_t y=now-last_trans_time;
	if(dead())
		return (x<y)?x:y;
	else
		return 0;
}

inline time_t gatePvData::timeAlive(void) const
	{ return (!dead())?(time(NULL)-dead_alive_time):0; }
inline time_t gatePvData::timeConnecting(void) const
	{ return (time(NULL)-dead_alive_time); }

inline void gatePvData::setReconnectTime(void) { time(&dead_alive_time); }
inline void gatePvData::setInactiveTime(void)  { time(&no_connect_time); }
inline void gatePvData::setActiveTime(void)    { time(&no_connect_time); }
inline void gatePvData::setAliveTime(void)     { time(&dead_alive_time); }
inline void gatePvData::setTransTime(void)     { time(&last_trans_time); }

inline void gatePvData::setTimes(void)
{
	time(&dead_alive_time);
	no_connect_time=last_trans_time=dead_alive_time;
}

inline void gatePvData::setDeathTime(void)
{
	time(&dead_alive_time);
	no_connect_time=dead_alive_time;
}

#endif

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */


