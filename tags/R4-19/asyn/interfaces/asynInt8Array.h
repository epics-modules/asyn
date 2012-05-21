/*  asynInt8Array.h */
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

#ifndef asynInt8ArrayH
#define asynInt8ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt8Array)(
              void *userPvt, asynUser *pasynUser,
              epicsInt8 *value, size_t nelements);
typedef struct asynInt8ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackInt8Array callback;
    void *userPvt;
} asynInt8ArrayInterrupt;
#define asynInt8ArrayType "asynInt8Array"
typedef struct asynInt8Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt8 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt8 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
             interruptCallbackInt8Array callback, void *userPvt,
             void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
             void *registrarPvt);
} asynInt8Array;

#define asynInt8ArrayBaseType "asynInt8ArrayBase"
typedef struct asynInt8ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint8ArrayInterface);
} asynInt8ArrayBase;
epicsShareExtern asynInt8ArrayBase *pasynInt8ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt8ArrayH */
