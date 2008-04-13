/*  asynHandle.h
 *
 ***********************************************************************
 * Copyright (c) 2002 The University of Chicago, as Operator of Argonne
 * National Laboratory, and the Regents of the University of
 * California, as Operator of Los Alamos National Laboratory, and
 * Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
 * asynDriver is distributed subject to a Software License Agreement
 * found in file LICENSE that is included with this distribution.
 ***********************************************************************
 *
 *  31-March-2008 Mark Rivers
 */

#ifndef asynHandleH
#define asynHandleH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackHandle)(void *userPvt, asynUser *pasynUser,
              void *handle);
              
typedef struct asynHandleInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackHandle callback;
    void *userPvt;
} asynHandleInterrupt;

#define asynHandleType "asynHandle"
typedef struct asynHandle {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, void *handle);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, void *handle);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
                    interruptCallbackHandle callback, void *userPvt, void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
                    void *registrarPvt);
} asynHandle;

/* asynHandleBase does the following:
   calls  registerInterface for asynHandle.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynHandleBaseType "asynHandleBase"
typedef struct asynHandleBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pasynHandleInterface);
} asynHandleBase;

epicsShareExtern asynHandleBase *pasynHandleBase;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynHandleH */
