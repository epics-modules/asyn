/*  asynFloat64Array.h */
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

#ifndef asynFloat64ArrayH
#define asynFloat64ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynFloat64ArrayType "asynFloat64Array"
typedef struct asynFloat64Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements, size_t *nIn);
} asynFloat64Array;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64ArrayH */
