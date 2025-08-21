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
 * File:       gateVc.cc
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

#define DEBUG_STATE 0
#define DEBUG_VC_DELETE 0
#define DEBUG_GDD 0
#define DEBUG_EVENT_DATA 0
#define DEBUG_ENUM 0
#define DEBUG_TIMESTAMP 0
#define DEBUG_RTYP 0
#define DEBUG_DELAY 0
#define DEBUG_ACCESS 0
#define DEBUG_SLIDER 0

#include <stdio.h>
#include <string.h>
#include <time.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef _WIN32
#else
# include <unistd.h>
#endif

#include <gdd.h>
#include <gddApps.h>
#include <dbMapper.h>
#include <osiWireFormat.h>
#include <casCtx.h>

#include "gateResources.h"
#include "gateServer.h"
#include "gateVc.h"
#include "gatePv.h"
#include "gateAs.h"

// Static initializations
unsigned long gateVcData::nextID=0;

// ---------------------------- utilities ------------------------------------

void heading(const char *funcname, const char *pvname)
{
//	if(strcmp(pvname,"evans:perf:c3.SCAN")) return;
	fflush(stderr);
	printf("\n[%s] %s %s\n",timeStamp(),funcname,pvname);
	fflush(stdout);
}

void dumpdd(int step, const char *desc, const char * pvname, const gdd *dd)
{
//	if(strcmp(name,"evans:perf:c3.SCAN")) return;
	fflush(stderr);
    printf("(%d) PV %s *******************************\n", step, pvname);
	if (!dd) {
		printf("%-25s ===== no gdd here (NULL pointer) =====\n",
			   desc);
	} else {
		printf("%-25s at=%d[%s] pt=%d[%s]\n",
			   desc,
			   dd->applicationType(),
			   gddApplicationTypeTable::AppTable().getName(dd->applicationType()),
			   dd->primitiveType(),
			   aitName[dd->primitiveType()]
			);
		fflush(stdout);
		dd->dump(); fflush(stderr);
	}
	fflush(stdout);
}

// ------------------------gateChan

// gateChan has the common part of a casChannel, primarily access
// security.  gateVcChan is derived for specific use with gateVcData
// and gateStatChan is derived for specific use with gateStat.

// casPv is set in the constructor to the incoming casPv.  The
// derived-class constructor adds the derived gateChan to the
// chan_list in the casPv.  casPv is set to NULL when the casPv
// removes it from its chan_list in the casPv destructor.  This
// prevents anyone, including the server, from calling removeChan when
// the casPv is gone.

// The caller must provide storage for user and host in asAddClient in
// gateAsClient::gateAsClient.  We assume this storage is in the
// server and just keep pointers to it.
gateChan::gateChan(const casCtx &ctx, casPV *casPvIn, gateAsEntry *asentry,
  const char * const userIn, const char * const hostIn) :
	casChannel(ctx),
	casPv(casPvIn),
	user(userIn),
	host(hostIn)
{

	asclient=new gateAsClient(asentry,user,host);
	if(asclient) asclient->setUserFunction(post_rights,this);
}

gateChan::~gateChan(void)
{
	deleteAsClient();
}

#ifdef SUPPORT_OWNER_CHANGE
// Virtual function from casChannel, not called from Gateway.  It is a
// security hole to support this, and it is no longer implemented in
// base.
void gateChan::setOwner(const char* const u, const char* const h)
{
	asclient->changeInfo(u,h);
}
#endif

// Access security userFunc that calls postAccessRightsEvent in the
// server.  The server then notifies its clients.  With MEDM, for
// example, if there is no write access, it would then display a
// circle with a bar.  The values of read and write access are not
// stored in the server, but rather readAcess() or writeAcccess() is
// called before each operation.
void gateChan::post_rights(void* userPvt)
{
#if DEBUG_ACCESS
	printf("gateChan::post_rights:\n");
#endif
	gateChan *pChan = (gateChan *)userPvt;
	// Do not post the event if the casPv is NULL, which means the
	// gateChan has been removed from the gateVcData::chan_list which
	// means the gateChan is not relevant anymore, even though it may
	// not yet have been destroyed.  Actually, post_rights should not
	// be called if casPv is NULL since the asclient should also have
	// been destroyed in that event, but it is a safety feature.
	if(pChan && pChan->getCasPv()) {
		pChan->postAccessRightsEvent();
	}
}

void gateChan::report(FILE *fp)
{
	fprintf(fp,"  %s@%s (%s access)\n",getUser(),getHost(),
	  readAccess()?(writeAccess()?"read/write":"read only"):"no");
}

void gateChan::resetAsClient(gateAsEntry *asentry)
{
#if DEBUG_ACCESS
	printf("gateChan::addAsClient asentry=%p asclient=%p\n",
	  asentry,asclient);
#endif

	if(asclient) {
		delete asclient;
		asclient=NULL;
	}

    // Don't do anything else if the casPv is NULL, indicating it is
	// being destroyed or if the asentry is NULL
	if(!casPv || !asentry) return;

	asclient=new gateAsClient(asentry,user,host);
	if(asclient) {
		asclient->setUserFunction(post_rights,this);
	}
#if DEBUG_ACCESS
	printf("user=%s host=%s asentry=%p asclient=%p\n",
	  user,host,asentry,asclient);
	report(stdout);
#endif
	// Notify the server.  postAccessRightsEvent() just causes the
	// server to notify its clients.  The access is not stored in the
	// server, but rather readAcess() or writeAcccess() is called
	// before each operation.
	postAccessRightsEvent();
}

void gateChan::deleteAsClient(void)
{
	if(asclient) {
		delete asclient;
		asclient = NULL;
	}
}

// ------------------------gateVcChan

gateVcChan::gateVcChan(const casCtx &ctx, casPV *casPvIn, gateAsEntry *asentry,
  const char * const user, const char * const host) :
	gateChan(ctx,casPvIn,asentry,user,host)
{
	gateVcData *vc=(gateVcData *)casPv;
	if(vc) vc->addChan(this);
}

gateVcChan::~gateVcChan(void)
{
	gateVcData *vc=(gateVcData *)casPv;
	if(vc) vc->removeChan(this);
}

// This is a new virtual write in CAS that knows the casChannel.  The
// casPV::write() is not called if this one is implemented.  This
// write calls a new, overloaded gateVcData::write() that has the
// gateChan as an argument to do what used to be done in the virtual
// gateVcData::write().
caStatus gateVcChan::write(const casCtx &ctx, const gdd &value)
{
	gateVcData *vc=(gateVcData *)casPv;

	// Trap writes
	if(asclient && asclient->clientPvt()->trapMask) {
		FILE *fp=global_resources->getPutlogFp();
#ifndef WITH_CAPUTLOG
		if(fp) {
			fprintf(fp,"%s %s@%s %s\n",
			  timeStamp(),
			  user?user:"Unknown",
			  host?host:"Unknown",
			  vc && vc->getName()?vc->getName():"Unknown");
			fflush(fp);
		}
#else	/* WITH_CAPUTLOG */
		if(fp) {
          global_resources->putLog( fp,	  user ? user : "Unknown",
                                          host ? host : "Unknown",
		  								  (vc && vc->getName()) ? vc->getName() : "Unknown",
                                          (vc) ? vc->eventData() : NULL,
                                          &value);
		}
        if (global_resources->hasCaPutlogAddress()) {
          global_resources->caPutLog_Send(user ? user : "Unknown",
                                          host ? host : "Unknown",
                                          (vc && vc->getName()) ? vc->getName() : "Unknown",
                                          (vc) ? vc->eventData() : NULL,
                                          &value);
        }
#endif
	}

	// Call the non-virtual-function write() in the gateVcData
	if(vc) return vc->write(ctx,value,*this);
	else return S_casApp_noSupport;
}

// This is a new virtual write in CAS that knows the casChannel.  The
// casPV::write() is not called if this one is implemented.  This
// write calls a new, overloaded gateVcData::write() that has the
// gateChan as an argument to do what used to be done in the virtual
// gateVcData::writeNotify().
caStatus gateVcChan::writeNotify(const casCtx &ctx, const gdd &value)
{
	gateVcData *vc=(gateVcData *)casPv;

	// Trap writes
	if(asclient && asclient->clientPvt()->trapMask) {
		FILE *fp=global_resources->getPutlogFp();
#ifndef WITH_CAPUTLOG
		if(fp) {
			fprintf(fp,"%s %s@%s %s\n",
			  timeStamp(),
			  user?user:"Unknown",
			  host?host:"Unknown",
			  vc && vc->getName()?vc->getName():"Unknown" );
			fflush(fp);
		}
#else	/* WITH_CAPUTLOG */
		if(fp) {
          global_resources->putLog( fp,	  user ? user : "Unknown",
                                          host ? host : "Unknown",
		  								  (vc && vc->getName()) ? vc->getName() : "Unknown",
                                          (vc) ? vc->eventData() : NULL,
                                          &value);
		}
        if (global_resources->hasCaPutlogAddress()) {
          global_resources->caPutLog_Send(user ? user : "Unknown",
                                          host ? host : "Unknown",
                                          (vc && vc->getName()) ? vc->getName() : "Unknown",
                                          (vc) ? vc->eventData() : NULL,
                                          &value);
        }
#endif
	}

	// Call the non-virtual-function write() in the gateVcData
	if(vc) return vc->writeNotify(ctx,value,*this);
	else return S_casApp_noSupport;
}

// Called from the server to determine access before each read
// Called from the server to determine access before each read
bool gateVcChan::readAccess(void) const
{
	gateVcData *vc=(gateVcData *)casPv;

#if DEBUG_ACCESS && 0
	printf("gateVcChan::readAccess: %s asclient=%p ret=%d\n",
	  vc?vc->getName():"NULL VC",asclient,
	  (asclient && asclient->readAccess() && vc && vc->readAccess())?1:0);
#endif

	return (asclient && asclient->readAccess() && vc && vc->readAccess())?
	  true:false;
}

// Called from the server to determine access before each write
bool gateVcChan::writeAccess(void) const
{
	gateVcData *vc=(gateVcData *)casPv;

#if DEBUG_ACCESS && 0
	printf("gateVcChan::writeAccess: %s asclient=%p ret=%d\n",
	  vc?vc->getName():"NULL VC",asclient,
	  (asclient && asclient->writeAccess() && vc && vc->writeAccess())?1:0);
#endif

	return (asclient && asclient->writeAccess() && vc && vc->writeAccess())?
	  true:false;
}

// ------------------------gateVcData

gateVcData::gateVcData(gateServer* m,const char* name) :
	casPV(*m),
	pv(NULL),
	vcID(nextID++),
#if 0
	event_count(0),
#endif
	read_access(aitTrue),
#if 0
	//KE: Unused
	time_last_trans(0),
	time_last_alh_trans(0),
#endif
	status(0),
	asentry(NULL),
	pv_state(gateVcClear),
	mrg(m),
	pv_name(strDup(name)),
	in_list_flag(0),
	prev_post_value_changes(0),
	post_value_changes(0),
	pv_data(NULL),
	event_data(NULL),
	highestGddAppType(0)
{
	gateDebug2(5,"gateVcData(gateServer=%p,name=%s)\n",(void *)m,name);

	select_mask|=(mrg->alarmEventMask()|
	  mrg->valueEventMask()|
	  mrg->logEventMask()|
      mrg->propertyEventMask());
	alh_mask|=mrg->alarmEventMask();
	client_mask=0u;

	// Important Note: The exist test should have been performed for this
	// PV already, which means that the gatePvData exists and is connected
	// at this point, so it should be present on the pv list

	if(mrg->pvFind(pv_name,pv)==0)
	{
		// Activate could possibly perform get for pv_data and event_data
		// before returning here.  Be sure to mark this state connecting
		// so that everything works out OK in this situation.
		setState(gateVcConnect);
		asentry=pv->getEntry();

		if(pv->activate(this)==0)
		{
			mrg->vcAdd(pv_name,*this);
			markInList();
			// set state to gateVcConnect used to be here
		}
		else
			status=1;
	}
	else
		status=1;

#ifdef STAT_PVS
	mrg->setStat(statVcTotal,++mrg->total_vc);
#endif
}

gateVcData::~gateVcData(void)
{
#if DEBUG_VC_DELETE
    printf("gateVcData::~gateVcData %s\n",pv?pv->name():"NULL");
#endif
	gateDebug0(5,"~gateVcData()\n");
	gateVcData* x;
	if(in_list_flag) mrg->vcDelete(pv_name,x);

	// Clean up the pending write. The server library destroys the
	// asynchronous io before destroying the casPV. When the casPv
	// is destroyed this destructor is called. However, when the
	// user types control C I notice that we end up here with some
	// IO requests that are still pending. Our obligation is
	// to track all of the IO requests until we post competion to
	// the server. The server always destroys the IO request object.
	while ( gateAsyncW * pWIO = nrWio.first() ) {
	    pWIO->listRemove ();
	    pWIO->postIOCompletion ( S_casApp_canceledAsyncIO );
	}
	while ( gateAsyncW * pWIO = pendWio.first() ) {
	    pWIO->listRemove ();
	    pWIO->postIOCompletion ( S_casApp_canceledAsyncIO );
	}
	if(pv_data) {
		pv_data->unreference();
		pv_data=NULL;
	}
	if(event_data) {
		event_data->unreference();
		event_data=NULL;
	}
	delete [] pv_name;
	pv_name="Error";
	if (pv) pv->setVC(NULL);

	// Clear the chan_list to insure the gateChan's do not call the
	// gateVcData to remove them after the gateVcData is
	// gone. removeChan also sets the pChan->vc to NULL, the only
	// really necessary thing to do.
	gateVcChan *pChan;
	while((pChan=chan_list.first()))	{
		removeChan(pChan);
		pChan->deleteAsClient();
	}

	// Clear the async io lists to insure the gateAsyncX's do not try
	// to remove themselves from the lists after the gateVcData and
	// the lists are gone. removeFromQueue sets the pointer to the
	// list in the gateAsyncX to NULL.
	gateAsyncR* asyncr;
	while((asyncr=ctrl_rio.first()))	{
		asyncr->removeFromQueue();
	}
	while((asyncr=time_rio.first()))	{
		asyncr->removeFromQueue();
	}
	while((asyncr=alhRio.first()))	{
		asyncr->removeFromQueue();
	}

#ifdef STAT_PVS
	mrg->setStat(statVcTotal,--mrg->total_vc);
#endif
}

const char* gateVcData::getName() const
{
	return name();
}

// This is called whenever CAS want to destroy your casPV, usually
// because all clients have disconnected
void gateVcData::destroy(void)
{
	gateDebug0(1,"gateVcData::destroy()\n");
#if DEBUG_VC_DELETE
	printf("gateVcData::destroy %s\n",pv?pv->name():"NULL");
#endif
	vcRemove();
	casPV::destroy();
}

casChannel* gateVcData::createChannel(const casCtx &ctx,
  const char * const user, const char * const host)
{
	gateDebug0(5,"gateVcData::createChannel()\n");
	gateVcChan* chan =  new gateVcChan(ctx,this,asentry,user,host);
	return chan;
}

void gateVcData::report(FILE *fp)
{
	fprintf(fp,"%-30s event rate = %5.2f\n",pv_name,pv->eventRate());

	tsDLIter<gateVcChan> iter=chan_list.firstIter();
	while(iter.valid()) {
		iter->report(fp);
		iter++;
	}
}

void gateVcData::vcRemove(void)
{
	gateDebug1(1,"gateVcData::vcRemove() name=%s\n",name());

	switch(getState())
	{
	case gateVcClear:
		gateDebug0(1,"gateVcData::vcRemove() clear\n");
		break;
	case gateVcConnect:
	case gateVcReady:
		gateDebug0(1,"gateVcData::vcRemove() connect/ready -> clear\n");
		setState(gateVcClear);
#if DEBUG_VC_DELETE
		printf("gateVcData::vcRemove %s\n",pv?pv->name():"NULL");
#endif
		pv->deactivate();
		break;
	default:
		gateDebug0(1,"gateVcData::vcRemove() default state\n");
		break;
	}
}

// This function is called by the gatePvData::eventCB to copy the gdd
// generated there into the event_data when needAddRemove is True,
// otherwise setEventData is called.  For ENUM's the event_data's
// related gdd is set to the pv_data, which holds the enum strings.
void gateVcData::vcAdd(readType read_type)
{
	// an add indicates that the pv_data and event_data are ready
	gateDebug1(1,"gateVcData::vcAdd() name=%s\n",name());

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gateVcData::vcAdd: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	switch(getState())
	{
	case gateVcConnect:
		gateDebug0(1,"gateVcData::vcAdd() connecting -> ready\n");
		setState(gateVcReady);
		vcNew(read_type);
		break;
	case gateVcReady:
		gateDebug0(1,"gateVcData::vcAdd() ready ?\n");
	case gateVcClear:
		gateDebug0(1,"gateVcData::vcAdd() clear ?\n");
	default:
		gateDebug0(1,"gateVcData::vcAdd() default state ?\n");
	}
}

// This function is called by the gatePvData::eventCB to copy the gdd
// generated there into the event_data when needAddRemove is False,
// otherwise vcAdd is called.  For ENUM's the event_data's related gdd
// is set to the pv_data, which holds the enum strings.
int gateVcData::setEventData(gdd* dd)
{
	gddApplicationTypeTable& app_table=gddApplicationTypeTable::AppTable();
	gdd* ndd=event_data;
	int stat_sevr_changed = 1;

	gateDebug2(10,"gateVcData::setEventData(dd=%p) name=%s\n",(void *)dd,name());

#if DEBUG_GDD
    heading("gateVcData::setEventData",name());
    dumpdd(1,"event_data (old value data)",name(),event_data);
    dumpdd(1,"dd (new value data)",name(),dd);
#endif

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gateVcData::setEventData: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	if(event_data)
	{
		dbr_short_t oldSevr;
		dbr_short_t newSevr;
		dbr_short_t oldStat;
		dbr_short_t newStat;

		// Containers get special treatment (for performance reasons)
		if(event_data->isContainer())
		{
			// If the gdd has already been posted, clone a new one
			if(event_data->isConstant())
			{
				ndd = app_table.getDD(event_data->applicationType());
				app_table.smartCopy(ndd,event_data);
				event_data->unreference();
			}

			ndd[gddAppTypeIndex_dbr_stsack_string_value].getStatSevr(oldStat,oldSevr);
			dd->getStatSevr(newStat,newSevr);
			if(oldSevr == newSevr && oldStat == newStat) stat_sevr_changed=0;

			// Fill in the new value
			app_table.smartCopy(ndd,dd);
			event_data = ndd;
			dd->unreference();
		}
		// Scalar and atomic data: Just replace the event_data
		else
		{
			event_data->getStatSevr(oldStat,oldSevr);
			dd->getStatSevr(newStat,newSevr);
			if(oldSevr == newSevr && oldStat == newStat) stat_sevr_changed=0;

			event_data = dd;
			ndd->unreference();
		}
	}
	// No event_data present: just set it to the incoming gdd
	else
	{
		event_data = dd;
	}

#if DEBUG_GDD
	dumpdd(4,"event_data(after)",name(),event_data);
#endif

#if DEBUG_STATE
	switch(getState())
	{
	case gateVcConnect:
		gateDebug0(2,"gateVcData::setEventData() connecting\n");
		break;
	case gateVcClear:
		gateDebug0(2,"gateVcData::setEventData() clear\n");
		break;
	case gateVcReady:
		gateDebug0(2,"gateVcData::setEventData() ready\n");
		break;
	default:
		gateDebug0(2,"gateVcData::setEventData() default state\n");
		break;
	}
#endif

#if DEBUG_EVENT_DATA
	if(pv->fieldType() == DBF_ENUM) {
		dumpdd(99,"event_data",name(),event_data);
		heading("*** gateVcData::setEventData: end",name());
	}
#endif

	return stat_sevr_changed;
}

// This function is called by the gatePvData::alhCB to copy the ackt and
// acks fields of the gdd generated there into the event_data. If ackt
// or acks are changed, an event is generated
void gateVcData::setAlhData(gdd* dd)
{
	gddApplicationTypeTable& app_table=gddApplicationTypeTable::AppTable();
	gdd* ndd=event_data;
	int ackt_acks_changed=1;

	gateDebug2(10,"gateVcData::setAlhData(dd=%p) name=%s\n",(void *)dd,name());

#if DEBUG_GDD
	heading("gateVcData::setAlhData",name());
	dumpdd(1,"dd (incoming)",name(),dd);
	dumpdd(2,"event_data(before)",name(),event_data);
#endif

	if(event_data)
	{

		// If the event_data is already an ALH Container, ackt/acks are adjusted
		if(event_data->applicationType() == gddAppType_dbr_stsack_string)
		{
			// If the gdd has already been posted, clone a new one
			if(event_data->isConstant())
			{
				ndd = app_table.getDD(event_data->applicationType());
				app_table.smartCopy(ndd,event_data);
				event_data->unreference();
			}
			// Check for acks and ackt changes and fill in the new values
			unsigned short oldacks = ndd[gddAppTypeIndex_dbr_stsack_string_acks];
			unsigned short oldackt = ndd[gddAppTypeIndex_dbr_stsack_string_ackt];
			unsigned short newacks = dd[gddAppTypeIndex_dbr_stsack_string_acks];
			unsigned short newackt = dd[gddAppTypeIndex_dbr_stsack_string_ackt];

			ndd[gddAppTypeIndex_dbr_stsack_string_ackt].
			  put(&dd[gddAppTypeIndex_dbr_stsack_string_ackt]);
			ndd[gddAppTypeIndex_dbr_stsack_string_acks].
			  put(&dd[gddAppTypeIndex_dbr_stsack_string_acks]);

			if(oldacks == newacks && oldackt == newackt) ackt_acks_changed=0;
			event_data= ndd;
			dd->unreference();
		}
		// If event_data is a value: use the incoming gdd and adjust the value
		else
		{
			event_data = dd;

#if DEBUG_GDD
			dumpdd(31,"event_data(before fill in)",name(),event_data);
#endif
			// But replace the (string) value with the old value
			app_table.smartCopy(event_data,ndd);
			ndd->unreference();

#if DEBUG_GDD
			dumpdd(32,"event_data(old value filled in)",name(),event_data);
#endif
		}
	}
	// No event_data present: just set it to the incoming gdd
	else
	{
		event_data = dd;
	}

#if DEBUG_GDD
	dumpdd(4,"event_data(after)",name(),event_data);
#endif

	// Post the extra alarm data event if there is a change
	if(ackt_acks_changed) vcPostEvent(mrg->alarmEventMask());

#if DEBUG_STATE
	switch(getState())
	{
	case gateVcConnect:
		gateDebug0(2,"gateVcData::setAlhData() connecting\n");
		break;
	case gateVcClear:
		gateDebug0(2,"gateVcData::setAlhData() clear\n");
		break;
	case gateVcReady:
		gateDebug0(2,"gateVcData::setAlhData() ready\n");
		break;
	default:
		gateDebug0(2,"gateVcData::setAlhData() default state\n");
		break;
	}
#endif
}

// This function is called by the gatePvData::getCB to copy the gdd
// generated there into the pv_data
void gateVcData::setPvData(gdd* dd)
{
	// always accept the data transaction - no matter what state
	// this is the PV atttributes, which come in during the connect state
	// currently
	gateDebug2(2,"gateVcData::setPvData(gdd=%p) name=%s\n",(void *)dd,name());

#if DEBUG_GDD
    heading("gateVcData::setPvData",name());
    dumpdd(1,"pv_data (old property data)",name(),pv_data);
    dumpdd(1,"dd (new property data)",name(),dd);
#endif

	if(pv_data) pv_data->unreference();
	pv_data=dd;

	switch(getState())
	{
	case gateVcClear:
		gateDebug0(2,"gateVcData::setPvData() clear\n");
		break;
	default:
		gateDebug0(2,"gateVcData::setPvData() default state\n");
		break;
	}
	//don't call vcData if caching disabled
	//if(global_resources->getCacheMode())
	//	vcData();
}

// This function is an adapted version of the function with the same
// name from casStrmClient.cc. We use it to ensure that a GDD container
// member has enough space to accept all the elements we want to copy
// to it.
static void convertContainerMemberToAtomic(
	gdd &dd, aitUint32 appType, aitUint32 count)
{
	gdd * pVal;
	if (dd.isContainer()) {
		// All DBR types have a value member
		aitUint32 index;
		int gdds = gddApplicationTypeTable::AppTable().mapAppToIndex(
			dd.applicationType(), appType, index);
		if (gdds) {
			gateDebug0(
				1,
				"convertContainerMemberToAtomic: got container type "
				"without a value member");
			return;
		}

		pVal = dd.getDD(index);
		if (!pVal) {
			gateDebug0(
				1,
				"convertContainerMemberToAtomic: value member is null");
			return;
		}
	} else {
		pVal = &dd;
	}

	// The prototype pVal for a DBR_CTRL_* contains a "scalar" valued zero.
	// The original code left that untouched unless the current value
	// is an array with more than one element:
	//    if (count <= 1) {
	//        return;
	//    }
	// This overlooked the option where count = 0 and turned empty arrays
	// into a scalar with value 0.
	// Now we always update the pVal gddBounds to 0 ... count,
	// regardless of count = 0, 1, 2, ...

	// We can't changed a managed type that is already atomic (array).
	if (!pVal->isScalar()) {
		gateDebug0(
			1,
			"convertContainerMemberToAtomic: Cannot resize value member "
			"that already is an array.");
		return;
	}

	// convert to atomic
	gddBounds bds;
	bds.setSize(count);
	bds.setFirst(0u);
	pVal->setDimension(1u, &bds);
}

// The state of a process variable in the gateway is maintained in two
// gdd's, the pv_data and the event_data.  The pv_data is filled in
// from the gatePvData's getCB.  For most native types, its
// application type is attributes.  (See the gatePvData:: dataXxxCB
// routines.)  The event_data is filled in by the gatePvData's
// eventCB.  It gets changed whenever a significant change occurs to
// the process variable.  When a read (get) is requested, this
// function copies the pv_data and event_data into the gdd that comes
// with the read.  This dd has the appropriate application type but
// its primitive type is 0 (aitEnumInvalid).
void gateVcData::copyState(gdd &dd)
{
	gddApplicationTypeTable& table=gddApplicationTypeTable::AppTable();

#if DEBUG_GDD || DEBUG_ENUM
	heading("gateVcData::copyState",name());
	dumpdd(1,"dd(incoming)",name(),&dd);
#endif

	// The pv_data gdd for all DBF types except DBF_STRING and
	// DBF_ENUM has an application type of attributes.  For DBF_STRING
	// pv_data is NULL. For DBF_STRING pv_data has application type
	// enums, and is the list of strings.  See the dataXxxCB
	// gatePvData routines.
	if ( pv_data ) {
		// If the target gdd stores enum labels, we have to ensure that
		// there is enough space for these labels. The code in
		// casStrmClient.cc does the same thing.
		unsigned at = dd.applicationType();
		if (at == gddAppType_dbr_gr_enum || at == gddAppType_dbr_ctrl_enum) {
			convertContainerMemberToAtomic(
				dd, gddAppType_enums, MAX_ENUM_STATES);
		}
		table.smartCopy(&dd,pv_data);
#if DEBUG_GDD || DEBUG_ENUM
		dumpdd(2,"pv_data",name(),pv_data);
		dumpdd(3,"dd(after pv_data)",name(),&dd);
#endif
	}

	// The event_data gdd has an application type of value for all DBF
	// types.  The primitive type and whether it is scalar or atomic
	// varies with the DBR type.  See the eventXxxCB gatePvData
	// routines.  If the pv is alh monitored, the event_data is a
	// container type (gddAppType_dbr_stsack_string)
	if ( event_data ) {
		// We might have to resize the target gdd if it does not have
		// enough space to store all the value elements. This only
		// happens when the target gdd is using one of the container
		// types.
		unsigned at = dd.applicationType();
		if (at >= gddAppType_dbr_gr_short
			&& at <= gddAppType_dbr_stsack_string) {
			aitUint32 count;
			if (event_data->isContainer()) {
				aitUint32 index;
				int gdds = table.mapAppToIndex(
					event_data->applicationType(), gddAppType_value, index);
				if (gdds) {
					gateDebug0(
						1,
						"gateVcData::copyState: could not find value member");
					count = 1;
				} else {
					count = event_data->getDD(index)->getDataSizeElements();
				}
			} else {
				count = event_data->getDataSizeElements();
			}
			convertContainerMemberToAtomic(dd, gddAppType_value, count);
		}
#if DEBUG_GDD || DEBUG_ENUM
        dumpdd(5,"dd(before event_data)",name(),&dd);
        dumpdd(4,"copyState (new event_data)",name(),event_data);
#endif
		table.smartCopy(&dd,event_data);
#if DEBUG_GDD || DEBUG_ENUM
		dumpdd(5,"dd(after event_data)",name(),&dd);
#endif
#if DEBUG_EVENT_DATA
		if(pv->fieldType() == DBF_ENUM) {
			dumpdd(99,"event_data",name(),event_data);
		}
#endif
	}
#if DEBUG_GDD || DEBUG_ENUM
	heading("*** gateVcData::copyState: end",name());
#endif
}

void gateVcData::vcNew(readType read_type)
{
	gateDebug1(10,"gateVcData::vcNew() name=%s\n",name());

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gateVcData::vcNew: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	// Flush any accumulated reads and writes
	flushAsyncWriteQueue();
	if(ctrl_rio.count() || time_rio.count())
		flushAsyncReadQueue(read_type);
	if(!pv->alhGetPending()) {
		if(alhRio.count())
			flushAsyncAlhReadQueue();
	}

#if DEBUG_EVENT_DATA
		if(pv->fieldType() == DBF_ENUM) {
			heading("gateVcData::vcNew",name());
			dumpdd(99,"event_data",name(),event_data);
		}
#endif
}

void gateVcData::markAlhDataAvailable(void)
{
	gateDebug1(10,"gateVcData::markAlhDataAvailable() name=%s\n",name());

	// Flush any accumulated reads
	if(ready()) {
		if(alhRio.count()) flushAsyncAlhReadQueue();
	}
}

// The asynchronous io queues are filled when the vc is not ready.
// This routine, called from vcNew, flushes the write queue.
void gateVcData::flushAsyncWriteQueue()
{
    // gateAsyncW object will be destroyed by PCAS library
    // so there is no leak here
    while ( gateAsyncW * pWIO = nrWio.first () ) {
	gateDebug1(10,"gateVcData::flushAsyncWriteQueue() name=%s\n",name());
	pWIO->listRemove ();
	pWIO->listAdd ( pendWio );
	if ( pv ) {
	    assert ( ready() );
	    smartConstGDDPointer pDD = pWIO->extractDD ();
	    assert ( pDD.valid () );
	    caStatus stat =
		pv->put( *pDD, pWIO->isPutNotify() ? pWIO : 0 );
	    pDD.set ( 0 );
	    if ( pWIO->isPutNotify() ) {
		if ( stat != S_casApp_success ) {
		    pWIO->listRemove ();
		    pWIO->postIOCompletion ( stat );
		}
	    }
	    else {
		pWIO->listRemove ();
		pWIO->postIOCompletion ( stat );
	    }
	}
	else {
	    pWIO->listRemove ();
	    pWIO->postIOCompletion ( S_casApp_canceledAsyncIO );
	}
    }
}

// The asynchronous io queues are filled when the vc is not ready.
// This routine, called from vcNew, flushes the read queue.
void gateVcData::flushAsyncReadQueue(readType read_type)
{
	gateDebug1(10,"gateVcData::flushAsyncReadQueue() name=%s\n",name());
	gateAsyncR* asyncr;

	if(read_type == ctrlType)
	{
		while((asyncr=ctrl_rio.first()))	{
			gateDebug2(5,"gateVcData::flushAsyncReadQueue() (ctrl read) "
			  "posting asyncr %p (DD at %p)\n",
			  (void *)asyncr,(void *)&asyncr->DD());
			asyncr->removeFromQueue();

#if DEBUG_GDD
			heading("gateVcData::flushAsyncReadQueue",name());
			dumpdd(1,"asyncr->DD()(before)",name(),&asyncr->DD());
#endif
			// Copy the current state into the asyncr->DD()
			copyState(asyncr->DD());
#if DEBUG_DELAY
			if(!strncmp("Xorbit",name(),6)) {
				printf("%s gateVcData::flushAsyncReadQueue: %s state=%d\n",
				  timeStamp(),name(),getState());
				printf("  S_casApp_success\n");
			}
#endif
			asyncr->postIOCompletion(S_casApp_success,asyncr->DD());
		}
	}
	else{
		while((asyncr=time_rio.first()))	{
			gateDebug2(5,"gateVcData::flushAsyncReadQueue() (time read) "
			  "posting asyncr %p (DD at %p)\n",
			  (void *)asyncr,(void *)&asyncr->DD());
			asyncr->removeFromQueue();

#if DEBUG_GDD
			heading("gateVcData::flushAsyncReadQueue",name());
			dumpdd(1,"asyncr->DD()(before)",name(),&asyncr->DD());
#endif
			// Copy the current state into the asyncr->DD()
			copyState(asyncr->DD());
#if DEBUG_DELAY
			if(!strncmp("Xorbit",name(),6)) {
				printf("%s gateVcData::flushAsyncReadQueue: %s state=%d\n",
				  timeStamp(),name(),getState());
				printf("  S_casApp_success\n");
			}
#endif
			asyncr->postIOCompletion(S_casApp_success,asyncr->DD());
		}
	}
}

// The alh asynchronous read queue is filled when the alh data is not
// ready.  This routine, called from vcNew, flushes the alh read
// queue.  There is no separate alh write queue.
void gateVcData::flushAsyncAlhReadQueue(void)
{
	gateDebug1(10,"gateVcData::flushAsyncAlhAlhReadQueue() name=%s\n",name());
	gateAsyncR* asyncr;

	while((asyncr=alhRio.first()))	{
		gateDebug2(5,"gateVcData::flushAsyncAlhReadQueue() posting asyncr %p (DD at %p)\n",
		  (void *)asyncr,(void *)&asyncr->DD());
		asyncr->removeFromQueue();

#if DEBUG_GDD
		heading("gateVcData::flushAsyncAlhReadQueue",name());
		dumpdd(1,"asyncr->DD()(before)",name(),&asyncr->DD());
#endif
		// Copy the current state into the asyncr->DD()
		copyState(asyncr->DD());
#if DEBUG_DELAY
		if(!strncmp("Xorbit",name(),6)) {
			printf("%s gateVcData::flushAsyncAlhReadQueue: %s state=%d\n",
			  timeStamp(),name(),getState());
			printf("  S_casApp_success\n");
		}
#endif
		asyncr->postIOCompletion(S_casApp_success,asyncr->DD());
	}
}

// Called from setEventData, which is called from the gatePvData's
// eventCB.  Posts an event to the server library owing to changes in
// the process variable.
void gateVcData::vcPostEvent(casEventMask event_mask)
{
	gateDebug1(10,"gateVcData::vcPostEvent() name=%s\n",name());
	gdd *local_event_data = event_data;

	if (highestGddAppType) {
		gateDebug1(10, "gateVcData::vcPostEvent() creating new %s DD\n", gddApplicationTypeTable::AppTable().getName(highestGddAppType));
		local_event_data = gddApplicationTypeTable::AppTable().getDD(highestGddAppType);
		copyState(*local_event_data);
	}

//	time_t t;

#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gateVcData::vcPostEvent: %s state=%d\n",timeStamp(),name(),
		  getState());
	}
#endif

	if(needPosting())
	{
		gateDebug1(2,"gateVcData::vcPostEvent() posting event (event_data at %p)\n",
			(void *)local_event_data);

		if(local_event_data->isAtomic())
		{
#if DEBUG_EVENT_DATA
			if(pv->fieldType() == DBF_ENUM) {
				heading("gateVcData::vcPostEvent",name());
				dumpdd(99,"event_data",name(),local_event_data);
			}
#elif DEBUG_GDD
			heading("gateVcData::vcPostEvent(1)",name());
			dumpdd(1,"event_data",name(),local_event_data);
#endif

				postEvent(event_mask,*local_event_data);

#ifdef RATE_STATS
			mrg->post_event_count++;
#endif
		}
		else
		{
			// no more than 4 events per second
			// if(++event_count<4)
#if DEBUG_EVENT_DATA && 0
				if(pv->fieldType() == DBF_ENUM) {
					heading("gateVcData::vcPostEvent",name());
					dumpdd(99,"event_data",name(),local_event_data);
				}
#endif
#if DEBUG_GDD
				heading("gateVcData::vcPostEvent(2)",name());
				dumpdd(1,"event_data",name(),local_event_data);
#endif

				postEvent(event_mask,*local_event_data);

#ifdef RATE_STATS
				mrg->post_event_count++;
#endif
		}
	}

	if (local_event_data != event_data)
		local_event_data->unreference();
}

void gateVcData::vcData(readType read_type)
{
	// pv_data just appeared - don't really care
	gateDebug1(10,"gateVcData::vcData() name=%s\n",name());

	//flush any accumulated reads if caching disabled
	if(!global_resources->getCacheMode()){
		if(ctrl_rio.count() || time_rio.count()) flushAsyncReadQueue(read_type);
	}
}

void gateVcData::vcDelete(void)
{
	gateDebug1(10,"gateVcData::vcDelete() name=%s\n",name());
}

caStatus gateVcData::interestRegister(void)
{
	gateDebug1(10,"gateVcData::interestRegister() name=%s\n",name());
	// supposed to post the value shortly after this is called?
	markInterested();
	return S_casApp_success;
}

void gateVcData::interestDelete(void)
{
	gateDebug1(10,"gateVcData::interestDelete() name=%s\n",name());
	markNotInterested();
}

caStatus gateVcData::read( const casCtx& ctx, gdd& dd )
{
	gateDebug1(10,"gateVcData::read() name=%s\n",name());
	static const aitString str = "Not Supported by Gateway";
	readType read_type = ctrlType;

	/* This is to obtain mask from client */
	if(ctx.getMsg()->m_cmmd == CA_PROTO_EVENT_ADD){
		struct mon_info *pMonInfo = (struct mon_info *) ctx.getData();
	//If build with R3.14.9 next two lines must be switched
     	ca_uint16_t caProtoMask = AlignedWireRef < epicsUInt16 >(pMonInfo->m_mask);
		//ca_uint16_t caProtoMask = epicsNTOH16 (pMonInfo->m_mask);

		if (caProtoMask & DBE_LOG) {
			client_mask = DBE_LOG;
		}
		if (caProtoMask & DBE_PROPERTY) {
			client_mask = DBE_PROPERTY;
		}

		unsigned at=dd.applicationType();
		if (highestGddAppType < at) {
			if (at >= gddAppType_dbr_gr_short && at <= gddAppType_dbr_ctrl_double) {
				highestGddAppType = at;
				gateDebug1(10, "gateVcData::read() increasing highestGddAppType to %u\n", highestGddAppType);
			}
		}
	}

#if DEBUG_GDD || DEBUG_ENUM
	heading("gateVcData::read",name());
	dumpdd(1,"dd(incoming)",name(),&dd);
#endif
#if DEBUG_RTYP
	heading("gateVcData::read",name());
	fflush(stderr);
	printf(" gddAppType: %d \n",(int)dd.applicationType());
	fflush(stdout);
#endif
#if DEBUG_TIMESTAMP
	heading("gateVcData::read",name());
#endif
#if DEBUG_DELAY
	if(!strncmp("Xorbit",name(),6)) {
		printf("%s gateVcData::read: %s state=%d appType=%d\n",
		  timeStamp(),name(), getState(),dd.applicationType());
	}
#endif

	// Branch on application type
	unsigned at=dd.applicationType();
	switch(at) {
	case gddAppType_ackt:
	case gddAppType_acks:
		// Not useful and not monitorable so data would be out-of-date
#if 0
		fprintf(stderr,"%s gateVcData::read(): "
		  "Got unsupported app type %d for %s\n",
		  timeStamp(),at,name());
		fflush(stderr);
#endif
#if DEBUG_DELAY
		if(!strncmp("Xorbit",name(),6)) {
			printf("  S_casApp_noSupport\n");
		}
#endif
		return S_casApp_noSupport;
	case gddAppType_class:
		// Would require supporting DBR_CLASS_NAME
#if DEBUG_RTYP
		fflush(stderr);
		printf(" gddAppType_class (%d):\n",(int)at);
		dumpdd(1,"dd(incoming)",name(),&dd);
#endif
		dd.put(str);
#if DEBUG_RTYP
		dumpdd(1,"dd(outgoing)",name(),&dd);
		printf(" gddAppType(outgoing): %d \n",(int)dd.applicationType());
		fflush(stdout);
#endif
#if DEBUG_DELAY
		if(!strncmp("Xorbit",name(),6)) {
			printf("  S_casApp_noSupport\n");
		}
#endif
//		return S_casApp_noSupport;
		return S_casApp_success;
	case gddAppType_dbr_stsack_string:
		if((event_data &&
		  !(event_data->applicationType()==gddAppType_dbr_stsack_string))
		  || !pv->alhMonitored())
		{
			// Only start monitoring alh data if it is requested
			pv->markAlhGetPending();
			pv->alhMonitor();
		}
		if(!ready() || pv->alhGetPending()) {
			// Specify async return if alh data not ready
			gateDebug0(10,"gateVcData::read() alh data not ready\n");
			// the read will complete when the data is available
			alhRio.add(*(new gateAsyncR(ctx,dd,&alhRio)));
			if(!global_resources->getCacheMode())
			{
				pv->get(timeType);
			}
			return S_casApp_asyncCompletion;
		} else if(pendWio.count()||nrWio.count()) {
			// Pending write in progress, don't read now
			return S_casApp_postponeAsyncIO;
		} else {
			// Copy the current state into the dd
            gateDebug1(10,"gateVcData::read() %s using cached pv data for reply\n",name());
            copyState(dd);
		}
		return S_casApp_success;
	case gddAppType_value: if(!global_resources->getCacheMode()) read_type = timeType;
	default:
		if(!ready()) {
			// Specify async return if PV not ready
			gateDebug0(10,"gateVcData::read() pv not ready\n");
			// the read will complete when the connection is complete
			if(read_type == ctrlType)
				ctrl_rio.add(*(new gateAsyncR(ctx,dd,&ctrl_rio)));
			else
				time_rio.add(*(new gateAsyncR(ctx,dd,&time_rio)));
			if(!global_resources->getCacheMode())
			{
				pv->get(read_type);
			}
#if DEBUG_GDD
			fflush(stderr);
			printf("gateVcData::read: return S_casApp_asyncCompletion\n");
			fflush(stdout);
#endif
#if DEBUG_DELAY
			if(!strncmp("Xorbit",name(),6)) {
				printf("  S_casApp_asyncCompletion (%s %s)\n",
				  ready()?"Ready":"Not Ready",
				  pv->alhGetPending()?"alhGetPending":"No alhGetPending");
			}
#endif
			return S_casApp_asyncCompletion;
		} else if(pendWio.count()||nrWio.count()) {
			// Pending write in progress, don't read now
#if DEBUG_GDD
			fflush(stderr);
			printf("gateVcData::read: return S_casApp_postponeAsyncIO\n");
			fflush(stdout);
#endif
#if DEBUG_DELAY
			if(!strncmp("Xorbit",name(),6)) {
				printf("  S_casApp_asyncCompletion (Pending write)\n");
			}
#endif
			return S_casApp_postponeAsyncIO;
		} else if(!global_resources->getCacheMode())
		{// get request forwarded to pv if caching disabled
			if(read_type == ctrlType)
				ctrl_rio.add(*(new gateAsyncR(ctx,dd,&ctrl_rio)));
			else
				time_rio.add(*(new gateAsyncR(ctx,dd,&time_rio)));

			pv->get(read_type);
			return S_casApp_asyncCompletion;
		}
		else{
			// Copy the current state into the dd
			copyState(dd);
#if DEBUG_GDD
			fflush(stderr);
			printf("gateVcData::read: return S_casApp_success\n");
			fflush(stdout);
#endif
#if DEBUG_TIMESTAMP
			{
				TS_STAMP ts;
				dd.getTimeStamp(&ts);
				fprintf(stderr,"gateVcData::read %s %u %u\n",
				  name(),ts.secPastEpoch,ts.nsec);
#if 0
				dd.dump();
#endif
			}
#endif
#if DEBUG_DELAY
			if(!strncmp("Xorbit",name(),6)) {
				printf("  S_casApp_success\n");
			}
#endif
			return S_casApp_success;
		}
	}
}

// This is the virtual write function defined in casPV.  It should no
// longer be called if casChannel::write is implemented.
caStatus gateVcData::write(const casCtx& ctx, const gdd& dd)
{
	fprintf(stderr,"Virtual gateVcData::write called for %s.\n"
	  "  This is an error!\n",name());
	return S_casApp_noSupport;
}

// This is the virtual write function defined in casPV.  It should no
// longer be called if casChannel::write is implemented.
caStatus gateVcData::writeNotify(const casCtx& ctx, const gdd& dd)
{
	fprintf(stderr,"Virtual gateVcData::writeNotify called for %s.\n"
	  "  This is an error!\n",name());
	return S_casApp_noSupport;
}

// This is a non-virtual-function write that allows passing a pointer
// to the gateChannel.  Currently chan is not used.
caStatus gateVcData::
    write(const casCtx& ctx, const gdd& dd, gateChan &/*chan*/)
{
    return this->writeSpecifyingCBMechanism (ctx, dd, false);
}

// This is a non-virtual-function write that allows passing a pointer
// to the gateChannel.  Currently chan is not used.
caStatus gateVcData::
    writeNotify(const casCtx& ctx, const gdd& dd, gateChan &/*chan*/)
{
    return this->writeSpecifyingCBMechanism (ctx, dd, true);
}

// This is a non-virtual-function write that allows passing a pointer
// to the gateChannel.  Currently chan is not used.
caStatus gateVcData::writeSpecifyingCBMechanism(
    const casCtx& ctx, const gdd& dd, bool isPutNotify)
{
	gateDebug1(10,"gateVcData::write() name=%s\n",name());

#if DEBUG_GDD || DEBUG_SLIDER
	heading("gateVcData::write",name());
	dumpdd(1,"dd(incoming)",name(),&dd);
#endif

	// Branch on application type
	unsigned at=dd.applicationType();
	switch(at) {
	case gddAppType_class:
		// Would require supporting DBR_CLASS_NAME
	case gddAppType_dbr_stsack_string:
		// Cannot be written
#if 0
		fprintf(stderr,"%s gateVcData::write: "
		  "Got unsupported app type %d for %s\n",
		  timeStamp(),at,name());
		fflush(stderr);
#endif
		return S_casApp_noSupport;
	case gddAppType_ackt:
	case gddAppType_acks:
	default:
		if(global_resources->isReadOnly()) return S_cas_noWrite;
		if(!ready()) {
			// Handle async return if PV not ready
			gateDebug0(10,"gateVcData::write() pv not ready\n");
			if ( nrWio.count () > maxSimlWriteIO ) {
			    return S_casApp_postponeAsyncIO;
			}
			gateAsyncW * pGateAsyncW =
			    new ( std :: nothrow )
				gateAsyncW ( ctx,dd,isPutNotify );
			if ( ! pGateAsyncW ) {
			    return S_casApp_noMemory;
			}
			pGateAsyncW->listAdd ( nrWio );
#if DEBUG_GDD || DEBUG_SLIDER
			fflush(stderr);
			printf("S_casApp_asyncCompletion\n");
			fflush(stdout);
#endif
			return S_casApp_asyncCompletion;
		} else if(pendWio.count()>=maxSimlWriteIO) {
			// Pending write already in progress
#if DEBUG_GDD || DEBUG_SLIDER
			fflush(stderr);
			printf("S_casApp_postponeAsyncIO\n");
			fflush(stdout);
#endif
			return S_casApp_postponeAsyncIO;
		} else {
			gateAsyncW * pGateAsyncW = 0;
			if ( isPutNotify ) {
#if DEBUG_GDD || DEBUG_SLIDER
			    fflush(stderr);
			    printf("pending_write\n");
			    fflush(stdout);
#endif
			    pGateAsyncW = new ( std :: nothrow )
				    gateAsyncW (ctx,dd,true);
			    if(!pGateAsyncW) return S_casApp_noMemory;
			    pGateAsyncW->listAdd ( pendWio );
			}
			caStatus stat = pv->put(dd, pGateAsyncW);
			if ( isPutNotify ) {
			    if(stat != S_casApp_success) {
				pGateAsyncW->listRemove ();
				pGateAsyncW->postIOCompletion ( stat );
			    }
			    return S_casApp_asyncCompletion;
			}
			else {
			    return stat;
			}
			// Start a pending write
		}
	}
}

void gateVcData::putCB(int putStatus, gateAsyncW & writeIO  )
{
	gateDebug2(10,"gateVcData::putCB() status=%d name=%s\n",status,name());

	caStatus stat = static_cast<caStatus>( -1);
	if(putStatus == ECA_NORMAL)
	    stat = S_casApp_success;
	else if(putStatus == ECA_DISCONNCHID || putStatus == ECA_DISCONN)
	    stat = S_casApp_canceledAsyncIO;
	writeIO.listRemove ();
	writeIO.postIOCompletion ( stat );
	// we will allow the wirteIO to remain in the pendWIO list,
	// and limit the total number of simultaneous io operations,
	// until a response is is dispatched to the client
}

aitEnum gateVcData::bestExternalType(void) const
{
	gateDebug0(10,"gateVcData::bestExternalType()\n");

	if(pv) return pv->nativeType();
	else return aitEnumFloat64;  // this sucks - potential problem area
}

unsigned gateVcData::maxDimension(void) const
{
	unsigned dim;

	// This information could be asked for very early, before the data
	// gdd is ready.

	if(pv_data) {
		dim=pv_data->dimension();
	} else {
		if(maximumElements()>1)
		  dim=1;
		else
		  dim=0;
	}

	gateDebug2(10,"gateVcData::maxDimension() %s %d\n",name(),(int)dim);
	return dim;
}

#if NODEBUG
aitIndex gateVcData::maxBound(unsigned /*dim*/) const
#else
aitIndex gateVcData::maxBound(unsigned dim) const
#endif
{
	gateDebug3(10,"gateVcData::maxBound(%u) %s %d\n",
		dim,name(),(int)maximumElements());
	return maximumElements();
}

aitIndex gateVcData::maximumElements(void) const
{
	return pv?pv->maxElements():0;
}

void gateVcData::removeEntry(void)
{
#if DEBUG_ACCESS
	printf("gateVcData::removeEntry %s asentry=%p\n",
	  getName(),asentry);
#endif
	// Replace the pointer in the gateVcData
	asentry=NULL;

	// Loop over gateVcChan's and delete the client
	tsDLIter<gateVcChan> iter=chan_list.firstIter();
	while(iter.valid()) {
		iter->deleteAsClient();
		iter++;
	}
}

void gateVcData::resetEntry(gateAsEntry *asentryIn)
{
#if DEBUG_ACCESS
	printf("gateVcData::resetEntry %s asentry=%p asentryIn=%p\n",
	  getName(),asentry,asentryIn);
#endif
	// Replace the pointer in the gateVcData
	asentry=asentryIn;

	// Loop over gateVcChan's
	tsDLIter<gateVcChan> iter=chan_list.firstIter();
	while(iter.valid()) {
		iter->resetAsClient(asentry);
		iter++;
	}
}

void gateVcData::setReadAccess(aitBool b)
{
	read_access=b;
	postAccessRights();
}

void gateVcData::setWriteAccess(aitBool b)
{
	if(global_resources->isReadOnly())
		write_access=aitFalse;
	else
		write_access=b;

	postAccessRights();
}

// Loops over all channels and calls their postAccessRights event
void gateVcData::postAccessRights(void)
{
	gateDebug0(5,"gateVcData::postAccessRights() posting access rights\n");

	tsDLIter<gateVcChan> iter=chan_list.firstIter();
	while(iter.valid()) {
		iter->postAccessRightsEvent();
		iter++;
	}
}

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
