/* asynFloat64ArrayCallback.h */
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

#ifndef asynFloat64ArrayCallbackH
#define asynFloat64ArrayCallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynFloat64ArrayCallbackType "asynFloat64ArrayCallback"

typedef void (*asynFloat64ArrayDataCallback)(void *drvPvt, epicsFloat64 *data);
typedef void (*asynFloat64ArrayIntervalCallback)(void *drvPvt, double seconds);

typedef struct asynFloat64ArrayCallback {
    double     (*setCallbackInterval)(void *drvPvt, asynUser *pasynUser,
                                      double seconds);
    double     (*getCallbackInterval)(void *drvPvt, asynUser *pasynUser);
    asynStatus (*registerCallbacks)  (void *drvPvt, asynUser *pasynUser,
                                      asynFloat64ArrayDataCallabck dataCallback,
                                      asynFloat64ArrayIntervalCallback 
                                          intervalCallback, 
                                      void *pvt);
} asynFloat64ArrayCallback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64ArrayCallbackH*/
