/*  asynHandelSyncIO.h */
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

#ifndef asynGenericPointerSyncIOH
#define asynGenericPointerSyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynGenericPointerSyncIOType "asynGenericPointerSyncIO"
typedef struct asynGenericPointerSyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                       asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser,void *pvalue,double timeout);
    asynStatus (*read)(asynUser *pasynUser,void *pvalue,double timeout);
    asynStatus (*writeRead)(asynUser *pasynUser,void *pwrite_buffer,void *pread_buffer,double timeout);
    asynStatus (*writeOnce)(const char *port, int addr,
                       void *pvalue,double timeout,const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                       void *pvalue,double timeout,const char *drvInfo);
    asynStatus (*writeReadOnce)(const char *port, int addr,
                       void *pwrite_buffer,void *pread_buffer,double timeout,const char *drvInfo);
} asynGenericPointerSyncIO;
epicsShareExtern asynGenericPointerSyncIO *pasynGenericPointerSyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynGenericPointerSyncIOH */
