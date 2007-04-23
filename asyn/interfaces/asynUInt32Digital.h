/*  asynUInt32Digital.h */
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

#ifndef asynUInt32DigitalH
#define asynUInt32DigitalH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef enum {
    interruptOnZeroToOne, interruptOnOneToZero, interruptOnBoth
} interruptReason;

typedef void (*interruptCallbackUInt32Digital)(void *userPvt, 
                 asynUser *pasynUser, epicsUInt32 data);
typedef struct asynUInt32DigitalInterrupt {
    epicsUInt32 mask;
    int addr;
    asynUser *pasynUser;
    interruptCallbackUInt32Digital callback;
    void *userPvt;
} asynUInt32DigitalInterrupt;
#define asynUInt32DigitalType "asynUInt32Digital"
typedef struct asynUInt32Digital {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
         epicsUInt32 value, epicsUInt32 mask);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *value, epicsUInt32 mask);
    asynStatus (*setInterrupt)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 mask, interruptReason reason);
    asynStatus (*clearInterrupt)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 mask);
    asynStatus (*getInterrupt)(void *drvPvt, asynUser *pasynUser,
        epicsUInt32 *mask, interruptReason reason);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
        interruptCallbackUInt32Digital callback,void *userPvt,epicsUInt32 mask,
        void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
        void *registrarPvt);
} asynUInt32Digital;

/* asynUInt32DigitalBase does the following:
   calls  registerInterface for asynUInt32Digital.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynUInt32DigitalBaseType "asynUInt32DigitalBase"
typedef struct asynUInt32DigitalBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pasynUInt32DigitalInterface);
} asynUInt32DigitalBase;
epicsShareExtern asynUInt32DigitalBase *pasynUInt32DigitalBase;


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynUInt32DigitalH */
