/*  asynUInt32DigitalCallback.h

    28-June-2004 Mark Rivers

*/

#ifndef asynUInt32DigitalCallbackH
#define asynUInt32DigitalCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

typedef void (*asynUInt32DigitalCallbackCallback)(void *pvt, epicsUInt32 data);
typedef enum {
    interruptOnZeroToOne, interruptOnOneToZero, interruptOnBoth
} interruptReason;

#define asynUInt32DigitalCallbackType "asynUInt32DigitalCallback"
typedef struct asynUInt32DigitalCallback {
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
        asynUInt32DigitalCallbackCallback callback,epicsUInt32 mask,void *pvt);
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser,
        asynUInt32DigitalCallbackCallback callback,epicsUInt32 mask,void *pvt);
    asynStatus (*setInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 mask, interruptReason reason);
    asynStatus (*getInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *mask, interruptReason reason);
} asynUInt32DigitalCallback;

#endif /* asynUInt32DigitalCallbackH */
