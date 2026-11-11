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
 * File:       gateAsyncIO.cc
 * Project:    CA Proxy Gateway
 *
 * Descr.:     Asynchronous Read / Write / pvExistTest
 *
 * Author(s):  J. Kowalkowski, J. Anderson, K. Evans (APS)
 *             R. Lange (BESSY)
 *
 *********************************************************************-*/

#include "gateResources.h"
#include "gateServer.h"
#include "gateVc.h"

// ------------------------------- async exist test methods

gateAsyncE::~gateAsyncE(void)
{
	gateDebug0(10,"~gateAsyncE()\n");
	// If it is in the eio queue, take it out
	removeFromQueue();
}

// ------------------------------- async read pending methods

gateAsyncR::~gateAsyncR(void)
{
	gateDebug1(10,"~gateAsyncR() (dd at %p)\n",(void *)&dd);
	// If it is in the rio queue, take it out
	removeFromQueue();
	// Unreference the dd
	dd.unreference();
}

// ------------------------------- async write pending methods

gateAsyncW::~gateAsyncW(void)
{
	gateDebug0(10,"~gateAsyncW()\n");
	// If a gatePvCallbackId still references this object,
	// NULL out its raw pointer so a later gatePvData::putCB()
	// does not dereference this freed object.
	if ( _pCbId ) {
		_pCbId->clearPrivatePtr ();
		_pCbId = 0;
	}
	listRemove ();
}

smartConstGDDPointer gateAsyncW::extractDD ()
{
    smartConstGDDPointer pDD;
    if ( _pDD.valid () ) {
        pDD.swap ( _pDD );
    }
    return pDD;
}

/* **************************** Emacs Editing Sequences ***************** */
/* Local Variables: */
/* tab-width: 4 */
/* c-basic-offset: 4 */
/* c-comment-only-line-offset: 0 */
/* c-file-offsets: ((substatement-open . 0) (label . 0)) */
/* End: */
