/* asynInt32Callback.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  10-July-2004  MLR  Mark Rivers

*/

#ifndef asynInt32CallbackH
#define asynInt32CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt32CallbackType "asynInt32Callback"

typedef void (*asynInt32DataCallback)(void *drvPvt, epicsInt32 data);
typedef void (*asynInt32IntervalCallback)(void *drvPvt, double seconds);

typedef struct asynInt32Callback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)(void *drvPvt, asynUser *pasynUser,
                                    asynInt32DataCallback dataCallback, 
                                    asynInt32IntervalCallback intervalCallback,
                                    void *pvt);
} asynInt32Callback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32CallbackH*/
