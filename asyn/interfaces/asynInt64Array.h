/*  asynInt64Array.h */
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

#ifndef asynInt64ArrayH
#define asynInt64ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt64Array)(
              void *userPvt, asynUser *pasynUser,
              epicsInt64 *value, size_t nelements);
typedef struct asynInt64ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackInt64Array callback;
    void *userPvt;
} asynInt64ArrayInterrupt;
#define asynInt64ArrayType "asynInt64Array"
typedef struct asynInt64Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt64 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt64 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
             interruptCallbackInt64Array callback, void *userPvt,
             void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
             void *registrarPvt);
} asynInt64Array;

#define asynInt64ArrayBaseType "asynInt64ArrayBase"
typedef struct asynInt64ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint64ArrayInterface);
} asynInt64ArrayBase;
epicsShareExtern asynInt64ArrayBase *pasynInt64ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt64ArrayH */
