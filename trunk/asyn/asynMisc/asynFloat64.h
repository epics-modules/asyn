/*  asynFloat64.h

    28-June-2004 Mark Rivers

*/

#ifndef asynFloat64H
#define asynFloat64H

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64Type "asynFloat64"
typedef struct {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, epicsFloat64 value);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, epicsFloat64 *value);
}asynFloat64;

#endif /* asynFloat64H */
