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
#ifndef _GATEASYNCIO_H_
#define _GATEASYNCIO_H_

/*+*********************************************************************
 *
 * File:       gateAsyncIO.h
 * Project:    CA Proxy Gateway
 *
 * Descr.:     Asynchronous Read / Write / pvExistTest
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 *********************************************************************-*/

#include "tsDLList.h"
#include "casdef.h"

class gateVcData;

// ---------------------- async exist test pending

class gateAsyncE : public casAsyncPVExistIO, public tsDLNode<gateAsyncE>
{
public:
	gateAsyncE(const casCtx &ctx, tsDLList<gateAsyncE> *eioIn) :
		casAsyncPVExistIO(ctx),eio(eioIn)
	{}

	virtual ~gateAsyncE(void);

	void removeFromQueue(void) {
		if(eio) {
			eio->remove(*this);
			eio=NULL;
		}
	}
private:
	tsDLList<gateAsyncE> *eio;
};

// ---------------------- async read pending

class gateAsyncR : public casAsyncReadIO, public tsDLNode<gateAsyncR>
{
public:
	gateAsyncR(const casCtx &ctx, gdd& ddIn, tsDLList<gateAsyncR> *rioIn) :
		casAsyncReadIO(ctx),dd(ddIn),rio(rioIn)
	{ dd.reference(); }

	virtual ~gateAsyncR(void);

	gdd& DD(void) const { return dd; }
	void removeFromQueue(void) {
		if(rio) {
			rio->remove(*this);
			rio=NULL;
		}
	}
private:
	gdd& dd;
	tsDLList<gateAsyncR> *rio;
};

// ---------------------- async write pending

class gateAsyncW : public casAsyncWriteIO, public tsDLNode<gateAsyncW>
{
public:
	gateAsyncW(const casCtx &ctx, const gdd& ddIn, 
	    tsDLList<gateAsyncW> *wioIn, int docallbackIn) :
	  casAsyncWriteIO(ctx),dd(ddIn),wio(wioIn),docallback(docallbackIn)
	  { dd.reference(); }

	virtual ~gateAsyncW(void);

	const gdd& DD(void) const { return dd; }
	void removeFromQueue(void) {
		if(wio) {
			wio->remove(*this);
			wio=NULL;
		}
	}
        void flush ( class gatePvData & pv );
private:
	const gdd& dd;
	tsDLList<gateAsyncW> *wio;
	int docallback;
};

class gatePendingWrite : public casAsyncWriteIO
{
public:
	gatePendingWrite(gateVcData &wowner, const casCtx &ctx, const gdd& wdd) :
	  casAsyncWriteIO(ctx),
	  owner(wowner),
	  dd(wdd)
	  { dd.reference(); }
	
	virtual ~gatePendingWrite(void);
	
	const gdd &DD(void) const { return dd; }
private:
	gateVcData &owner;
	const gdd &dd;
};

#endif

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
