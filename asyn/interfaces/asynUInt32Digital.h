/*  asynUInt32Digital.h

    28-June-2004 Mark Rivers

*/

#ifndef asynUInt32DigitalH
#define asynUInt32DigitalH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynUInt32DigitalType "asynUInt32Digital"
typedef struct asynUInt32Digital {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
         epicsUInt32 value, epicsUInt32 mask);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *value, epicsUInt32 mask);
} asynUInt32Digital;

#endif /* asynUInt32DigitalH */
