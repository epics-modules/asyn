/*  asynGenericPointer.h
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

#ifndef asynGenericPointerH
#define asynGenericPointerH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackGenericPointer)(void *userPvt, asynUser *pasynUser,
              void *pointer);
              
typedef struct asynGenericPointerInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackGenericPointer callback;
    void *userPvt;
} asynGenericPointerInterrupt;

#define asynGenericPointerType "asynGenericPointer"
typedef struct asynGenericPointer {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, void *pointer);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, void *pointer);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
                    interruptCallbackGenericPointer callback, void *userPvt, void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
                    void *registrarPvt);
} asynGenericPointer;

/* asynGenericPointerBase does the following:
   calls  registerInterface for asynGenericPointer.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynGenericPointerBaseType "asynGenericPointerBase"
typedef struct asynGenericPointerBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pasynGenericPointerInterface);
} asynGenericPointerBase;

epicsShareExtern asynGenericPointerBase *pasynGenericPointerBase;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynGenericPointerH */
