/*  asynInt32.h

    28-June-2004 Mark Rivers

*/

#ifndef asynInt32H
#define asynInt32H

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32Type "asynInt32"
typedef struct {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, epicsInt32 value);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, epicsInt32 *value);
}asynInt32;

#endif /* asynInt32H */
