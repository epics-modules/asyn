/* asynInt32ArrayArrayCallback.h */
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

#ifndef asynInt32ArrayCallbackH
#define asynInt32ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt32ArrayCallbackType "asynInt32ArrayCallback"

typedef void (*asynInt32ArrayDataCallback)(void *drvPvt, epicsInt32 *daya);
typedef void (*asynInt32ArrayIntervalCallback)(void *drvPvt, double seconds);

typedef struct asynInt32ArrayCallback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)   (void *drvPvt, asynUser *pasynUser,
                                      asynInt32ArrayDataCallback dataCallback, 
                                      asynInt32ArrayIntervalCallback 
                                          intervalCallback, 
                                      void *pvt);
} asynInt32ArrayCallback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32ArrayCallbackH */
