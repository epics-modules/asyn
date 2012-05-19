/*  asynInt32.h */
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

#ifndef asynInt32H
#define asynInt32H

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt32)(void *userPvt, asynUser *pasynUser, 
                                       epicsInt32 data);
typedef struct asynInt32Interrupt {
    int addr;
    asynUser *pasynUser;
    interruptCallbackInt32 callback;
    void *userPvt;
} asynInt32Interrupt;
#define asynInt32Type "asynInt32"
typedef struct asynInt32 {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser, epicsInt32 value);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser, epicsInt32 *value);
    asynStatus (*getBounds)(void *drvPvt, asynUser *pasynUser, 
                           epicsInt32 *low, epicsInt32 *high);
    asynStatus (*registerInterruptUser)(void *drvPvt,asynUser *pasynUser,
                           interruptCallbackInt32 callback, void *userPvt,
                           void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
                           void *registrarPvt);
} asynInt32;

/* asynInt32Base does the following:
   calls  registerInterface for asynInt32.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynInt32BaseType "asynInt32Base"
typedef struct asynInt32Base {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint32Interface);
} asynInt32Base;
epicsShareExtern asynInt32Base *pasynInt32Base;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32H */
