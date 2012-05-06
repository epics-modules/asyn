/*  asynEnum.h */
/***********************************************************************
* Copyright (c) 2012 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*    30-January-2012 Mark Rivers

*/

#ifndef asynEnumH
#define asynEnumH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

typedef void (*interruptCallbackEnum)(
              void *userPvt, asynUser *pasynUser,
              char *strings[], int values[], int severities[], size_t nelements);
typedef struct asynEnumInterrupt {
    asynUser *pasynUser;
    int addr;
    interruptCallbackEnum callback;
    void *userPvt;
} asynEnumInterrupt;
#define asynEnumType "asynEnum"
typedef struct asynEnum {
    asynStatus (*write)(void *drvPvt, asynUser *pasynUser,
                       char *strings[], int values[], int severities[], size_t nelements);
    asynStatus (*read)(void *drvPvt, asynUser *pasynUser,
                       char *strings[], int values[], int severities[], size_t nelements, size_t *nIn);
    asynStatus (*registerInterruptUser)(void *drvPvt, asynUser *pasynUser,
             interruptCallbackEnum callback, void *userPvt,
             void **registrarPvt);
    asynStatus (*cancelInterruptUser)(void *drvPvt, asynUser *pasynUser,
             void *registrarPvt);
} asynEnum;

#define asynEnumBaseType "asynEnumBase"
typedef struct asynEnumBase {
    asynStatus (*initialize)(const char *portName,
                            asynInterface *pEnumInterface);
} asynEnumBase;
epicsShareExtern asynEnumBase *pasynEnumBase;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynEnumH */
