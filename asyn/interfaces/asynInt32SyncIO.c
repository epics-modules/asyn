/*asynInt32SyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynInt32
 * Author:  Marty Kraimer
 * Created: 12OCT2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynInt32.h"
#include "asynDrvUser.h"
#include "asynInt32SyncIO.h"

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynInt32    *pasynInt32;
   void         *int32Pvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
}ioPvt;

/*asynInt32SyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser, epicsInt32 value,double timeout);
static asynStatus readOp(asynUser *pasynUser,epicsInt32 *pvalue,double timeout);
static asynStatus getBounds(asynUser *pasynUser,
                       epicsInt32 *plow, epicsInt32 *phigh);
static asynStatus writeOpOnce(const char *port, int addr,
                       epicsInt32 value,double timeout,const char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr,
                       epicsInt32 *pvalue,double timeout,const char *drvInfo);
static asynStatus getBoundsOnce(const char *port, int addr,
                       epicsInt32 *plow, epicsInt32 *phigh,const char *drvInfo);
static asynInt32SyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    getBounds,
    writeOpOnce,
    readOpOnce,
    getBoundsOnce
};
epicsShareDef asynInt32SyncIO *pasynInt32SyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynInt32SyncIO");
    pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = pioPvt;
    *ppasynUser = pasynUser;
    status = pasynManager->connectDevice(pasynUser, port, addr);    
    if (status != asynSuccess) {
        return status;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "port does not implement interface %s",asynCommonType);
       return asynError;
    }
    pioPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pioPvt->pcommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "port does not implement interface %s",asynInt32Type);
       return asynError;
    }
    pioPvt->pasynInt32 = (asynInt32 *)pasynInterface->pinterface;
    pioPvt->int32Pvt = pasynInterface->drvPvt;
    if(drvInfo) {
        /* Check for asynDrvUser interface */
        pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
        if(pasynInterface) {
            asynDrvUser *pasynDrvUser;
            void       *drvPvt;
            pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            status = pasynDrvUser->create(drvPvt,pasynUser,drvInfo,0,0);
            if(status==asynSuccess) {
                pioPvt->pasynDrvUser = pasynDrvUser;
                pioPvt->drvUserPvt = drvPvt;
            } else {
                return status;
            }
        }
    }
    return asynSuccess ;
}

static asynStatus disconnect(asynUser *pasynUser)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status;

    if(pioPvt->pasynDrvUser) {
        status = pioPvt->pasynDrvUser->destroy(pioPvt->drvUserPvt,pasynUser);
        if(status!=asynSuccess) {
            return status;
        }
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    free(pioPvt);
    return asynSuccess;
}
 

static asynStatus writeOp(asynUser *pasynUser, epicsInt32 value,double timeout)
{
    asynStatus status, unlockStatus;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynInt32->write(pioPvt->int32Pvt, pasynUser, value);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynInt32SyncIO wrote: %d\n", value);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus readOp(asynUser *pasynUser, epicsInt32 *pvalue, double timeout)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynInt32->read(pioPvt->int32Pvt, pasynUser, pvalue);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynInt32SyncIO read: %d\n", *pvalue);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus getBounds(asynUser *pasynUser,
                            epicsInt32 *plow, epicsInt32 *phigh)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynInt32->getBounds(pioPvt->int32Pvt,pasynUser,plow,phigh);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
                  "asynInt32SyncIO getBounds: status=%d low %d high %d\n",
                  status, *plow,*phigh);
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus writeOpOnce(const char *port, int addr,
    epicsInt32 value,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynInt32SyncIO connect failed %s\n",
            pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = writeOp(pasynUser,value,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynInt32SyncIO writeOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOpOnce(const char *port, int addr,
                   epicsInt32 *pvalue,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynInt32SyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = readOp(pasynUser,pvalue,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynInt32SyncIO readOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus getBoundsOnce(const char *port, int addr,
                epicsInt32 *plow, epicsInt32 *phigh,const char *drvInfo)
{
    asynStatus         status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynInt32SyncIO connect failed %s\n",
            pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = getBounds(pasynUser,plow,phigh);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynInt32SyncIO getBounds failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return(status);
}
