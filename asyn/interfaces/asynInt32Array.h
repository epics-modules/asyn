/*  asynInt32Array.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*    28-June-2004 Mark Rivers

*/

#ifndef asynInt32ArrayH
#define asynInt32ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt32ArrayType "asynInt32Array"
typedef struct asynInt32Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
             void (*callback)(void *userPvt,epicsInt32 *val,size_t nelem),
             void *userPvt);
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser,
             void (*callback)(void *userPvt,epicsInt32 *val,size_t nelem),
             void *userPvt);
} asynInt32Array;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32ArrayH */
