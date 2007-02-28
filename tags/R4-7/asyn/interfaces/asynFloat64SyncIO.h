/*  asynFloat64SyncIO.h */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*  28-June-2004 Mark Rivers
*/

#ifndef asynFloat64SyncIOH
#define asynFloat64SyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynFloat64SyncIOType "asynFloat64SyncIO"
typedef struct asynFloat64SyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                       asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser,epicsFloat64 value,double timeout);
    asynStatus (*read)(asynUser *pasynUser,epicsFloat64 *pvalue,double timeout);
    asynStatus (*writeOnce)(const char *port, int addr,
                       epicsFloat64 value,double timeout,const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                       epicsFloat64 *pvalue,double timeout,const char *drvInfo);
} asynFloat64SyncIO;
epicsShareExtern asynFloat64SyncIO *pasynFloat64SyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynFloat64SyncIOH */
