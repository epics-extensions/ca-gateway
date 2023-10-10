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
// Date: 7/96

// gateStat: Contains data and CAS interface for one bit of gate
// status info.  Update is done via a gate server's method (setStat)
// calling gateStat::post_data.

#ifndef GATE_STAT_H
#define GATE_STAT_H

#include <epicsVersion.h>

#include "casdef.h"
#include "aitTypes.h"

typedef enum {
	GATE_STAT_TYPE_PV=0,
	GATE_STAT_TYPE_DESC
} gateStatType;

class gdd;
class gateServer;
class gateStat;
class gateStatDesc;
class gateAs;
class gateAsClient;
class gateAsEntry;

class gateStatChan : public gateChan, public tsDLNode<gateStatChan>
{
public:
	gateStatChan(const casCtx &ctx, casPV *pvIn, gateAsEntry *asentryIn,
	  const char * const user, const char * const host
#ifdef EPICS_HAS_AS_IPAG
          ,epicsUInt32 ip_addr
#endif
          );
	~gateStatChan(void);

	virtual caStatus write(const casCtx &ctx, const gdd &value);
	virtual bool readAccess(void) const;
	virtual bool writeAccess(void) const;
};

class gateStat : public casPV
{
public:
	gateStat(gateServer *s, gateAsEntry *e, const char *n, int t);
	virtual ~gateStat(void);

	// CA server interface functions
	virtual caStatus interestRegister(void);
	virtual void interestDelete(void);
	virtual aitEnum bestExternalType(void) const;
	virtual caStatus read(const casCtx &ctx, gdd &prototype);
	virtual caStatus write(const casCtx &ctx, const gdd &dd);
	virtual unsigned maxSimultAsyncOps(void) const;
	virtual const char *getName() const;
	virtual casChannel *createChannel (const casCtx &ctx,
		const char* const pUserName, const char* const pHostName
#ifdef EPICS_HAS_AS_IPAG
                ,epicsUInt32 ip_addr
#endif
                );

	caStatus write(const casCtx &ctx, const gdd &dd, gateChan &chan);

    gateAsEntry* getEntry(void) const { return asentry; }
	void removeEntry(void);
	void resetEntry(gateAsEntry *asentryIn);
	void addChan(gateStatChan *chan) { chan_list.add(*chan); }
	void removeChan(gateStatChan *chan) {
		chan_list.remove(*chan); chan->setCasPv(NULL); }

	void postData(long val);
	void postData(unsigned long val);
	void postData(double val);

	void report(FILE *fp);

    gdd* pvData(void) const { return value; }

protected:
	gdd *value;
	gdd *attr;
	int post_data;
	int type;
	gateStatType statType;
	gateServer *serv;
	char *name;
	tsDLList<gateStatChan> chan_list;
	gateAsEntry* asentry;
};

// The gateStatDesc exists only to provide the equivalent of a DESC
// field.  It inherits from gateStat so we can use the parts that are
// already implemented, especially the gateChannel part.  We just
// override the parts that are different.
class gateStatDesc : public gateStat
{
public:
	gateStatDesc(gateServer *s, gateAsEntry *e, const char *n, int t);
	virtual ~gateStatDesc(void);

	// CA server interface functions
	virtual aitEnum bestExternalType(void) const { return aitEnumString; }
	virtual caStatus read(const casCtx &ctx, gdd &prototype);

	// Do not allow writes
	virtual caStatus write(const casCtx &ctx, const gdd &dd)
	  { return S_casApp_noSupport; }
	caStatus write(const casCtx &ctx, const gdd &dd, gateChan &chan)
	  { return S_casApp_noSupport; }

	// Override these to do nothing.  They should not be called.
	void postData(long val) {};
	void postData(unsigned long val) {};
	void postData(double val) {};

private:
};

#endif

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
