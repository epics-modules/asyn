/*  asynFloat64Array.h

    28-June-2004 Mark Rivers

*/

#ifndef asynFloat64ArrayH
#define asynFloat64ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>

#define asynFloat64ArrayType "asynFloat64Array"
typedef struct asynFloat64Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements, size_t *nIn);
} asynFloat64Array;

#endif /* asynFloat64ArrayH */
