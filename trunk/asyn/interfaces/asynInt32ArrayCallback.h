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
/*cancelCallback cancels based on callback and userPvt */
typedef struct asynInt32ArrayCallback {
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
             void (*callback)(void *userPvt,epicsInt32 *val,epicsUInt32 nelem),
             void *userPvt);
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser,
             void (*callback)(void *userPvt,epicsInt32 *val,epicsUInt32 nelem),
             void *userPvt);
} asynInt32ArrayCallback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32ArrayCallbackH */
