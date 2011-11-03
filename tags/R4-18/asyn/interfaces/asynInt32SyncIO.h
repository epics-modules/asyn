/*  asynInt32SyncIO.h */
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

#ifndef asynInt32SyncIOH
#define asynInt32SyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt32SyncIOType "asynInt32SyncIO"
typedef struct asynInt32SyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                          asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser, epicsInt32 value,double timeout);
    asynStatus (*read)(asynUser *pasynUser, epicsInt32 *pvalue,double timeout);
    asynStatus (*getBounds)(asynUser *pasynUser, 
                    epicsInt32 *plow, epicsInt32 *phigh);
    asynStatus (*writeOnce)(const char *port, int addr,
                    epicsInt32 value,double timeout, const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                    epicsInt32 *pvalue,double timeout, const char *drvInfo);
    asynStatus (*getBoundsOnce)(const char *port, int addr,
                    epicsInt32 *plow, epicsInt32 *phigh,const char *drvInfo);
} asynInt32SyncIO;
epicsShareExtern asynInt32SyncIO *pasynInt32SyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32SyncIOH */
