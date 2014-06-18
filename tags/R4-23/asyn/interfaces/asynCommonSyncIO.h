/*  asynCommonSyncIO.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  28-Feb-2005 Mark Rivers
*/

#ifndef asynCommonSyncIOH
#define asynCommonSyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <stdio.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynCommonSyncIOType "asynCommonSyncIO"
typedef struct asynCommonSyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                          asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*connectDevice)(asynUser *pasynUser);
    asynStatus (*disconnectDevice)(asynUser *pasynUser);
    asynStatus (*report)(asynUser *pasynUser, FILE *fd, int details);
} asynCommonSyncIO;
epicsShareExtern asynCommonSyncIO *pasynCommonSyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynCommonSyncIOH */
