/*  asynUInt32Digital.h

    28-June-2004 Mark Rivers

*/

#ifndef asynUInt32DigitalH
#define asynUInt32DigitalH

#include <asynDriver.h>
#include <epicsTypes.h>

typedef void (*asynUInt32DigitalCallback)(void *pvt, epicsUInt32 data);
typedef enum {
    interruptOnChange, interruptOnOne, interruptOnZero
}interruptReason;

#define asynUInt32DigitalType "asynUInt32Digital"
typedef struct asynUInt32Digital {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
         epicsUInt32 value, epicsUInt32 mask);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *value, epicsUInt32 mask);
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
        asynUInt32DigitalCallback callback,epicsUInt32 mask,void *pvt)
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*setInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 mask, interruptReason reason);
    asynStatus (*getInterruptMask)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *mask, interruptReason reason);
} asynInt32Digital;

#endif /* asynInt32DigitalH */
