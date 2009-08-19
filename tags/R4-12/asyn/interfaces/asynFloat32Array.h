/*  asynFloat32Array.h */
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

#ifndef asynFloat32ArrayH
#define asynFloat32ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackFloat32Array)(
              void *userPvt, asynUser *pasynUser,
              epicsFloat32 *data, size_t nelements);
typedef struct asynFloat32ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackFloat32Array callback;
    void *userPvt;
} asynFloat32ArrayInterrupt;
#define asynFloat32ArrayType "asynFloat32Array"
typedef struct asynFloat32Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat32 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsFloat32 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
         interruptCallbackFloat32Array callback,
         void *userPvt,void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
         void *registrarPvt);
} asynFloat32Array;

/* asynFloat32ArrayBase does the following:
   calls  registerInterface for asynFloat32Array.
   Implements registerInterruptUser and cancelInterruptUser
   Provides default implementations of all methods.
   registerInterruptUser and cancelInterruptUser can be called
   directly rather than via queueRequest.
*/

#define asynFloat32ArrayBaseType "asynFloat32ArrayBase"
typedef struct asynFloat32ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pfloat32ArrayInterface);
} asynFloat32ArrayBase;
epicsShareExtern asynFloat32ArrayBase *pasynFloat32ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat32ArrayH */
