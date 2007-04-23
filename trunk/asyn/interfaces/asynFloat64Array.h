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
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackFloat64Array)(
              void *userPvt, asynUser *pasynUser,
              epicsFloat64 *data, size_t nelements);
typedef struct asynFloat64ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackFloat64Array callback;
    void *userPvt;
} asynFloat64ArrayInterrupt;
#define asynFloat64ArrayType "asynFloat64Array"
typedef struct asynFloat64Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat64 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
         interruptCallbackFloat64Array callback,
         void *userPvt,void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
         void *registrarPvt);
} asynFloat64Array;

/* asynFloat64ArrayBase does the following:
   calls  registerInterface for asynFloat64Array.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynFloat64ArrayBaseType "asynFloat64ArrayBase"
typedef struct asynFloat64ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pfloat64ArrayInterface);
} asynFloat64ArrayBase;
epicsShareExtern asynFloat64ArrayBase *pasynFloat64ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64ArrayH */
