/*  asynInt64SyncIO.h */
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

#ifndef asynInt64SyncIOH
#define asynInt64SyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt64SyncIOType "asynInt64SyncIO"
typedef struct asynInt64SyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                          asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser, epicsInt64 value,double timeout);
    asynStatus (*read)(asynUser *pasynUser, epicsInt64 *pvalue,double timeout);
    asynStatus (*getBounds)(asynUser *pasynUser, 
                    epicsInt64 *plow, epicsInt64 *phigh);
    asynStatus (*writeOnce)(const char *port, int addr,
                    epicsInt64 value,double timeout, const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                    epicsInt64 *pvalue,double timeout, const char *drvInfo);
    asynStatus (*getBoundsOnce)(const char *port, int addr,
                    epicsInt64 *plow, epicsInt64 *phigh,const char *drvInfo);
} asynInt64SyncIO;
epicsShareExtern asynInt64SyncIO *pasynInt64SyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt64SyncIOH */
