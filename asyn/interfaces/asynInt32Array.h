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
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt32Array)(
              void *userPvt, asynUser *pasynUser,
              epicsInt32 *value, size_t nelements);
typedef struct asynInt32ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackInt32Array callback;
    void *userPvt;
} asynInt32ArrayInterrupt;
#define asynInt32ArrayType "asynInt32Array"
typedef struct asynInt32Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt32 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
             interruptCallbackInt32Array callback, void *userPvt,
             void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
             void *registrarPvt);
} asynInt32Array;

#define asynInt32ArrayBaseType "asynInt32ArrayBase"
typedef struct asynInt32ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint32ArrayInterface);
} asynInt32ArrayBase;
epicsShareExtern asynInt32ArrayBase *pasynInt32ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32ArrayH */
