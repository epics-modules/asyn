/*  asynInt16Array.h */
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

#ifndef asynInt16ArrayH
#define asynInt16ArrayH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackInt16Array)(
              void *userPvt, asynUser *pasynUser,
              epicsInt16 *value, size_t nelements);
typedef struct asynInt16ArrayInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackInt16Array callback;
    void *userPvt;
} asynInt16ArrayInterrupt;
#define asynInt16ArrayType "asynInt16Array"
typedef struct asynInt16Array {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       epicsInt16 *value, size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       epicsInt16 *value, size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
             interruptCallbackInt16Array callback, void *userPvt,
             void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
             void *registrarPvt);
} asynInt16Array;

#define asynInt16ArrayBaseType "asynInt16ArrayBase"
typedef struct asynInt16ArrayBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pint16ArrayInterface);
} asynInt16ArrayBase;
epicsShareExtern asynInt16ArrayBase *pasynInt16ArrayBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt16ArrayH */
