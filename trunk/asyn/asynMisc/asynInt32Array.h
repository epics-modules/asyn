/*  asynInt32Array.h

    28-June-2004 Mark Rivers

*/

#ifndef asynInt32ArrayH
#define asynInt32ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynInt32ArrayType "asynInt32Array"
typedef struct asynInt32Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value,size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value,size_t nelements, size_t *nIn);
} asynInt32;

#endif /* asynInt32H */
