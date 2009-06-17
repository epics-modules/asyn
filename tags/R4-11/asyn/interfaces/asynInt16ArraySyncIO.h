/*  asynInt16ArraySyncIO.h */
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

#ifndef asynInt16ArraySyncIOH
#define asynInt16ArraySyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynInt16ArraySyncIOType "asynInt16ArraySyncIO"
typedef struct asynInt16ArraySyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                       asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser,epicsInt16 *pvalue,size_t nelem,double timeout);
    asynStatus (*read)(asynUser *pasynUser,epicsInt16 *pvalue,size_t nelem,size_t *Nin,double timeout);
    asynStatus (*writeOnce)(const char *port, int addr,
                       epicsInt16 *pvalue,size_t nelem,double timeout,const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                       epicsInt16 *pvalue,size_t nelem,size_t *nIn,double timeout,const char *drvInfo);
} asynInt16ArraySyncIO;
epicsShareExtern asynInt16ArraySyncIO *pasynInt16ArraySyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynInt16ArraySyncIOH */
