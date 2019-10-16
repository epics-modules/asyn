/*  asynInt64.h */
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

#ifndef asynInt64H
#define asynInt64H

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt64)(void *userPvt, asynUser *pasynUser, 
                                       epicsInt64 data);
typedef struct asynInt64Interrupt {
    int addr;
    asynUser *pasynUser;
    interruptCallbackInt64 callback;
    void *userPvt;
} asynInt64Interrupt;
#define asynInt64Type "asynInt64"
typedef struct asynInt64 {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, epicsInt64 value);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, epicsInt64 *value);
    asynStatus (*getBounds)(void *drvPvt, asynUser *pasynUser, 
                           epicsInt64 *low, epicsInt64 *high);
    asynStatus (*registerInterruptUser)(void *drvPvt,asynUser *pasynUser,
                           interruptCallbackInt64 callback, void *userPvt,
                           void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
                           void *registrarPvt);
} asynInt64;

/* asynInt64Base does the following:
   calls  registerInterface for asynInt64.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynInt64BaseType "asynInt64Base"
typedef struct asynInt64Base {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint64Interface);
} asynInt64Base;
epicsShareExtern asynInt64Base *pasynInt64Base;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt64H */
