/*  asynInt32ArraySyncIO.h */
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

#ifndef asynInt32ArraySyncIOH
#define asynInt32ArraySyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt32ArraySyncIOType "asynInt32ArraySyncIO"
typedef struct asynInt32ArraySyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                       asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser,epicsInt32 *pvalue,size_t nelem,double timeout);
    asynStatus (*read)(asynUser *pasynUser,epicsInt32 *pvalue,size_t nelem,size_t *Nin,double timeout);
    asynStatus (*writeOnce)(const char *port, int addr,
                       epicsInt32 *pvalue,size_t nelem,double timeout,const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                       epicsInt32 *pvalue,size_t nelem,size_t *nIn,double timeout,const char *drvInfo);
} asynInt32ArraySyncIO;
epicsShareExtern asynInt32ArraySyncIO *pasynInt32ArraySyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt32ArraySyncIOH */
