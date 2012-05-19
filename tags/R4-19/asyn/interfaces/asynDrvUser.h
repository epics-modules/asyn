/* asynDrvUser.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* Generic Asynchronous Driver support
 * Author: Marty Kraimer
 * August 2004
 */

#ifndef asynDrvUserH
#define asynDrvUserH

#include <epicsStdio.h>
#include "shareLib.h"

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynDrvUserType "asynDrvUser"
typedef struct  asynDrvUser {
    /*The following do not have to be called via queueRequest callback*/
    asynStatus (*create)(void *drvPvt,asynUser *pasynUser,
        const char *drvInfo, const char **pptypeName,size_t *psize);
    asynStatus (*getType)(void *drvPvt,asynUser *pasynUser,
        const char **pptypeName,size_t *psize);
    asynStatus (*destroy)(void *drvPvt,asynUser *pasynUser);
}asynDrvUser;

#ifdef __cplusplus
}
#endif  /* __cplusplus */
#endif  /* asynDrvUserH */
