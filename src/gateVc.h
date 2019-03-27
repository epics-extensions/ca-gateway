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
#ifndef _GATEVC_H_
#define _GATEVC_H_

/*+*********************************************************************
 *
 * File:       gateVc.h
 * Project:    CA Proxy Gateway
 *
 * Descr.:     VC = Server tool side (upper half) of Proxy Gateway
 *             Variable. Handles all CAS related stuff:
 *             - Satisfies CA Server API
 *             - Keeps graphic enum, value and ALH data
 *             - Keeps queues for async read and write operations
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 *********************************************************************-*/

#include <sys/types.h>

#ifdef _WIN32
#else
# include <sys/time.h>
#endif

#include <caProto.h>

#include "casdef.h"
#include "aitTypes.h"

#include "gateAsyncIO.h"

typedef enum {
	gateVcClear,
	gateVcConnect,
	gateVcReady
} gateVcState;

typedef enum {
	gateFalse=0,
	gateTrue
} gateBool;

typedef enum {
	ctrlType=0,
	timeType,
	noCacheType
} readType;



class gdd;
class gatePvData;
class gateVcData;
class gateServer;
class gateAs;
class gateAsClient;
class gateAsEntry;

// ----------------------- vc channel stuff -------------------------------

class gateChan : public casChannel
{
public:
	gateChan(const casCtx &ctx, casPV *pvIn, gateAsEntry *asentryIn,
	  const char * const user, const char * const host);
	~gateChan(void);

#ifdef SUPPORT_OWNER_CHANGE
    // Virtual function from casChannel, not called from Gateway.  It
    // is a security hole to support this, and it is no longer
    // implemented in base.
	virtual void setOwner(const char * const user, const char * const host);
#endif
	
	void report(FILE *fp);

	const char *getUser(void) const { return user; };
	const char *getHost(void) const { return host; }
	casPV *getCasPv(void) const { return casPv; }

	gateAsClient *getAsClient(void) const { return asclient; }
	void deleteAsClient(void);
	void resetAsClient(gateAsEntry *asentry);
	
	void setCasPv(casPV *casPvIn) { casPv=casPvIn; }

	static void post_rights(void *userPvt);

protected:	
	casPV *casPv;
	gateAsClient *asclient; // Must be deleted when done using it
	const char *user;
	const char *host;
};

class gateVcChan : public gateChan, public tsDLNode<gateVcChan>
{
public:
	gateVcChan(const casCtx &ctx, casPV *pvIn, gateAsEntry *asentryIn,
	  const char * const user, const char * const host);
	~gateVcChan(void);

    virtual caStatus write(const casCtx &ctx, const gdd &value);
    virtual caStatus writeNotify(const casCtx &ctx, const gdd &value);
	virtual bool readAccess(void) const;
	virtual bool writeAccess(void) const;
};


// ----------------------- vc data stuff -------------------------------

class gateVcData : public casPV, public tsDLHashNode<gateVcData>
{
public:
	gateVcData(gateServer*,const char* pv_name);
	virtual ~gateVcData(void);

	// CA server interface functions
	virtual caStatus interestRegister(void);
	virtual void interestDelete(void);
	virtual aitEnum bestExternalType(void) const;
	virtual caStatus read(const casCtx &ctx, gdd &prototype);
	virtual caStatus write(const casCtx &ctx, const gdd &value);
	virtual caStatus writeNotify(const casCtx &ctx, const gdd &value);
	virtual void destroy(void);
	virtual unsigned maxDimension(void) const;
	virtual aitIndex maxBound(unsigned dim) const;
	virtual casChannel *createChannel (const casCtx &ctx,
		const char* const pUserName, const char* const pHostName);
	virtual const char *getName() const;

	caStatus write(const casCtx &ctx, const gdd &value, gateChan &chan);
	caStatus writeNotify(const casCtx &ctx, const gdd &value, gateChan &chan);

	int pending(void);
	int pendingConnect(void)	{ return (pv_state==gateVcConnect)?1:0; }
	int ready(void)				{ return (pv_state==gateVcReady)?1:0; }

	void report(FILE *fp);
    const char* name(void) const { return pv_name; }  // KE: Duplicates getName
    void* PV(void) const { return pv; }
    void setPV(gatePvData* pvIn)  { pv=pvIn; }
    gateVcState getState(void) const { return pv_state; }
    gdd* pvData(void) const { return pv_data; }
    gdd* eventData(void) const { return event_data; }
    gdd* attribute(int) const { return NULL; } // not done
    aitIndex maximumElements(void) const;
    gateAsEntry* getEntry(void) const { return asentry; }
	void removeEntry(void);
	void resetEntry(gateAsEntry *asentryIn);
    void addChan(gateVcChan*);
    void removeChan(gateVcChan*);

	void postAccessRights(void);
	void setReadAccess(aitBool b);
	void setWriteAccess(aitBool b);
	aitBool readAccess(void) const  { return read_access; }
	aitBool writeAccess(void) const { return write_access; }

	// Pv notification interface
	virtual void vcNew(readType read_type);
	virtual void vcDelete(void);
	virtual void vcPostEvent(casEventMask event_mask);
	virtual void vcData(readType read_type);

	void vcRemove(void);
	void vcAdd(readType read_type);
	int setEventData(gdd *dd); // Sets event_data from GatePvData::eventCB
	void setPvData(gdd *dd);    // Sets pv_data from GatePvData::getCB
	void setAlhData(gdd *dd);   // Sets event_data from GatePvData::alhCB
	void copyState(gdd &dd);    // Copies pv_data and event_data to dd

	// Asynchronous IO
	void putCB(int status,gateAsyncW &);
	unsigned long getVcID(void) const { return vcID; }
	void flushAsyncReadQueue(readType read_type);
	void flushAsyncWriteQueue();
	void flushAsyncAlhReadQueue(void);

	void markNoList(void) { in_list_flag=0; }
	void markInList(void) { in_list_flag=1; }

#if 0
	// KE: Unused
	void setTransTime(void);
	time_t timeSinceLastTrans(void) const;
	void setAlhTransTime(void);
	time_t timeSinceLastAlhTrans(void) const;
#endif
	
	int needPosting(void);
	int needInitialPosting(void);
	void markInterested(void);
	void markNotInterested(void);
	void markAlhDataAvailable(void);
	int getStatus(void) { return status; }

	casEventMask select_mask;
	casEventMask alh_mask;
	
	ca_uint16_t client_mask;
	
protected:
	void setState(gateVcState s) { pv_state=s; }
	gatePvData* pv;     // Pointer to the associated gatePvData

private:
	static unsigned long nextID;
	unsigned long vcID;
	aitBool read_access,write_access;
#if 0
	// KE: Unused
	time_t time_last_trans;
	time_t time_last_alh_trans;
#endif
	int status;
	gateAsEntry* asentry;
	gateVcState pv_state;
	gateServer* mrg;     // The gateServer that manages this gateVcData
	const char* pv_name;     // The name of the process variable
	int in_list_flag;
	int prev_post_value_changes;
	int post_value_changes;
	tsDLList<gateVcChan> chan_list;
	tsDLList<gateAsyncR> ctrl_rio;	 // Queue for ctrl read's received when not ready
	tsDLList<gateAsyncR> time_rio;	 // Queue for time read's received when not ready
	tsDLList<gateAsyncR> alhRio; // Queue for alh read's received when not ready
	tsDLList<gateAsyncW> nrWio;
	tsDLList<gateAsyncW> pendWio;
	// The state of the process variable is kept in these two gdd's
	gdd* pv_data;     // Filled in by gatePvData::getCB on activation
	gdd* event_data;  // Filled in by gatePvData::eventCB on channel change
	unsigned highestGddAppType;
	caStatus writeSpecifyingCBMechanism(const casCtx &ctx, 
		const gdd &value, bool isPutNotify );
	static const unsigned maxSimlWriteIO = 100u;
};

inline int gateVcData::pending(void) { return (pv_state==gateVcConnect)?1:0; }

inline int gateVcData::needPosting(void)
	{ return (post_value_changes)?1:0; }
inline int gateVcData::needInitialPosting(void)
	{ return (post_value_changes && !prev_post_value_changes)?1:0; }
inline void gateVcData::markInterested(void)
	{ prev_post_value_changes=post_value_changes; post_value_changes=1; }
inline void gateVcData::markNotInterested(void)
	{ prev_post_value_changes=post_value_changes; post_value_changes=0; }

#if 0
// KE: Unused
inline void gateVcData::setTransTime(void) { time(&time_last_trans); }
inline time_t gateVcData::timeSinceLastTrans(void) const
	{ return time(NULL)-time_last_trans; }
inline void gateVcData::setAlhTransTime(void) { time(&time_last_alh_trans); }
inline time_t gateVcData::timeSinceLastAlhTrans(void) const
	{ return time(NULL)-time_last_alh_trans; }
#endif

inline void gateVcData::addChan(gateVcChan* chan) { chan_list.add(*chan); }
inline void gateVcData::removeChan(gateVcChan* chan) {
	chan_list.remove(*chan); chan->setCasPv(NULL); }

#endif

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
