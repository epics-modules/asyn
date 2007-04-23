/*  asynFloat64.h */
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

#ifndef asynFloat64H
#define asynFloat64H

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackFloat64)(void *userPvt, asynUser *pasynUser,
            epicsFloat64 data);
typedef struct asynFloat64Interrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackFloat64 callback;
    void *userPvt;
} asynFloat64Interrupt;
#define asynFloat64Type "asynFloat64"
typedef struct asynFloat64 {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, epicsFloat64 value);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, epicsFloat64 *value);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
        interruptCallbackFloat64 callback, void *userPvt,void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
        void *registrarPvt);
} asynFloat64;

/* asynFloat64Base does the following:
   calls  registerInterface for asynFloat64.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynFloat64BaseType "asynFloat64Base"
typedef struct asynFloat64Base {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pasynFloat64Interface);
} asynFloat64Base;
epicsShareExtern asynFloat64Base *pasynFloat64Base;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64H */
