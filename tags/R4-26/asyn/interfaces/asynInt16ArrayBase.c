/*  asynInt16ArrayBase.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  11-OCT-2004 Marty Kraimer
 *  26-MAR-2008 Mark Rivers, converted to use macro
*/

#include <epicsTypes.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynInt16Array.h"

#include "asynXXXArrayBase.h"

ASYN_XXX_ARRAY_BASE_FUNCS(asynInt16Array, asynInt16ArrayType, asynInt16ArrayBase, pasynInt16ArrayBase,
                          asynInt16ArrayInterrupt, interruptCallbackInt16Array, epicsInt16)

