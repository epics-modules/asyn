/*  asynUInt32DigitalCallback.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  28-June-2004 Mark Rivers

*/

#ifndef asynUInt32DigitalCallbackH
#define asynUInt32DigitalCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef enum {
    interruptOnZeroToOne, interruptOnOneToZero, interruptOnBoth
} interruptReason;

#define asynUInt32DigitalCallbackType "asynUInt32DigitalCallback"
/*cancelCallback cancels based on callback and userPvt */
typedef struct asynUInt32DigitalCallback {
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
        void (*callback)(void *userPvt, epicsUInt32 data),
        epicsUInt32 mask,void *userPvt);
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser,
        void (*callback)(void *pvt, epicsUInt32 data),void *pvt);
    asynStatus (*setInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 mask, interruptReason reason);
    asynStatus (*getInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *mask, interruptReason reason);
} asynUInt32DigitalCallback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynUInt32DigitalCallbackH */
