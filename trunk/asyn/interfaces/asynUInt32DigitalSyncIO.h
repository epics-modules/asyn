/*  asynUInt32DigitalSyncIO.h */
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

#ifndef asynUInt32DigitalSyncIOH
#define asynUInt32DigitalSyncIOH

#include <asynDriver.h>
#include <epicsTypes.h>
#include <shareLib.h>

#ifdef __cplusplus
extern "C" {
#endif  /* __cplusplus */

#define asynUInt32DigitalSyncIOType "asynUInt32DigitalSyncIO"
typedef struct asynUInt32DigitalSyncIO {
    asynStatus (*connect)(const char *port, int addr, 
                       asynUser **ppasynUser, const char *drvInfo);
    asynStatus (*disconnect)(asynUser *pasynUser);
    asynStatus (*write)(asynUser *pasynUser,
                       epicsUInt32 value,epicsUInt32 mask,double timeout);
    asynStatus (*read)(asynUser *pasynUser,
                       epicsUInt32 *pvalue,epicsUInt32 mask,double timeout);
    asynStatus (*setInterrupt)(asynUser *pasynUser,
                       epicsUInt32 mask, interruptReason reason,double timeout);
    asynStatus (*clearInterrupt)(asynUser *pasynUser,
                       epicsUInt32 mask,double timeout);
    asynStatus (*getInterrupt)(asynUser *pasynUser,
                       epicsUInt32 *mask, interruptReason reason,double timeout);
    asynStatus (*writeOnce)(const char *port, int addr,
                       epicsUInt32 value,epicsUInt32 mask,double timeout,
                       const char *drvInfo);
    asynStatus (*readOnce)(const char *port, int addr,
                       epicsUInt32 *pvalue,epicsUInt32 mask,double timeout,
                       const char *drvInfo);
    asynStatus (*setInterruptOnce)(const char *port, int addr,
                       epicsUInt32 mask, interruptReason reason,double timeout,
                       const char *drvInfo);
    asynStatus (*clearInterruptOnce)(const char *port, int addr,
                       epicsUInt32 mask,double timeout,const char *drvInfo);
    asynStatus (*getInterruptOnce)(const char *port, int addr,
                       epicsUInt32 *mask, interruptReason reason,double timeout,
                       const char *drvInfo);
} asynUInt32DigitalSyncIO;
epicsShareExtern asynUInt32DigitalSyncIO *pasynUInt32DigitalSyncIO;

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* asynUInt32DigitalSyncIOH */
