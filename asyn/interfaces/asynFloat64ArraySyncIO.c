/*asynFloat64ArraySyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynFloat64Array
 * Author:  Mark Rivers
 * Created: 13APR2008
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynFloat64Array.h"
#include "asynDrvUser.h"
#include "asynFloat64ArraySyncIO.h"

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynFloat64Array  *pasynFloat64Array;
   void         *float64ArrayPvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
}ioPvt;

/*asynFloat64ArraySyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser,epicsFloat64 *pvalue,size_t nelem,double timeout);
static asynStatus readOp(asynUser *pasynUser,epicsFloat64 *pvalue,size_t nelem,size_t *nIn,double timeout);
static asynStatus writeOpOnce(const char *port, int addr,
                     epicsFloat64 *pvalue, size_t nelem, double timeout, const char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr,
                     epicsFloat64 *pvalue,size_t nelem, size_t *nIn, double timeout, const char *drvInfo);
static asynFloat64ArraySyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    writeOpOnce,
    readOpOnce
};
epicsShareDef asynFloat64ArraySyncIO *pasynFloat64ArraySyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynFloat64ArraySyncIO");
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
           "interface %s is not supported by port",asynCommonType);
       return asynError;
    }
    pioPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pioPvt->pcommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser, asynFloat64ArrayType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "interface %s is not supported by port",asynFloat64ArrayType);
       return asynError;
    }
    pioPvt->pasynFloat64Array = (asynFloat64Array *)pasynInterface->pinterface;
    pioPvt->float64ArrayPvt = pasynInterface->drvPvt;
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

static asynStatus writeOp(asynUser *pasynUser,epicsFloat64 *pvalue,size_t nelem,double timeout)
{
    asynStatus status, unlockStatus;
    ioPvt      *pPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pPvt->pasynFloat64Array->write(pPvt->float64ArrayPvt, pasynUser,pvalue,nelem);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynFloat64ArraySyncIO wrote: %e\n",
                  *pvalue);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return status;
}

static asynStatus readOp(asynUser *pasynUser,epicsFloat64 *pvalue,size_t nelem,size_t *nIn,double timeout)
{
    ioPvt      *pPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pPvt->pasynFloat64Array->read(pPvt->float64ArrayPvt, pasynUser, pvalue, nelem, nIn);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynFloat64ArraySyncIO read: %e\n", *pvalue);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return status;
}

static asynStatus writeOpOnce(const char *port, int addr,
    epicsFloat64 *pvalue,size_t nelem,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynFloat64ArraySyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = writeOp(pasynUser,pvalue,nelem,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynFloat64ArraySyncIO writeOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOpOnce(const char *port, int addr,
                   epicsFloat64 *pvalue,size_t nelem,size_t *nIn,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynFloat64ArraySyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = readOp(pasynUser,pvalue,nelem,nIn,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynFloat64ArraySyncIO readOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}
