/* asynFloat64Callback.h */
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

#ifndef asynFloat64CallbackH
#define asynFloat64CallbackH

#include <asynDriver.h>
#include <epicsTypes.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynFloat64CallbackType "asynFloat64Callback"
/*cancelCallback cancels based on callback and userPvt */
typedef struct asynFloat64Callback {
    asynStatus (*registerCallback)(void *drvPvt, asynUser *pasynUser,
                        void (*callback)(void *userPvt, epicsFloat64 data),
                        void *userPvt);
    asynStatus (*cancelCallback)(void *drvPvt, asynUser *pasynUser,
                        void (*callback)(void *userPvt, epicsFloat64 data),
                        void *userPvt);
} asynFloat64Callback;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64CallbackH*/
