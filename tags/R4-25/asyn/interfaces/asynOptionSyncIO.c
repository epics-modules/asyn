/*asynOptionSyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynOption
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
#include "asynOption.h"
#include "asynDrvUser.h"
#include "asynOptionSyncIO.h"

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynOption    *pasynOption;
   void         *OptionPvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
}ioPvt;

/*asynOptionSyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus setOption(asynUser *pasynUser, const char *key, const char *val, double timeout);
static asynStatus getOption(asynUser *pasynUser, const char *key, char *val, int sizeval, double timeout);
static asynStatus setOptionOnce(const char *port, int addr,
                       const char *key, const char *val, double timeout, const char *drvInfo);
static asynStatus getOptionOnce(const char *port, int addr,
                       const char *key, char *val, int sizeval, double timeout, const char *drvInfo);
static asynOptionSyncIO interface = {
    connect,
    disconnect,
    setOption,
    getOption,
    setOptionOnce,
    getOptionOnce,
};
epicsShareDef asynOptionSyncIO *pasynOptionSyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynOptionSyncIO");
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
    pasynInterface = pasynManager->findInterface(pasynUser, asynOptionType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "port does not implement interface %s",asynOptionType);
       return asynError;
    }
    pioPvt->pasynOption = (asynOption *)pasynInterface->pinterface;
    pioPvt->OptionPvt = pasynInterface->drvPvt;
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
 

static asynStatus setOption(asynUser *pasynUser, const char *key, const char *val, double timeout)
{
    asynStatus status, unlockStatus;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynOption->setOption(pioPvt->OptionPvt, pasynUser, key, val);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynOptionSyncIO wrote: key=%s, val=%s\n",key, val);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus getOption(asynUser *pasynUser, const char *key, char *val, int sizeval, double timeout)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynOption->getOption(pioPvt->OptionPvt, pasynUser, key, val, sizeval);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynOptionSyncIO read: key=%s, val=%s\n", key, val);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus setOptionOnce(const char *port, int addr,
    const char *key, const char *val, double timeout, const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port, addr, &pasynUser, drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynOptionSyncIO connect failed %s\n",
            pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = setOption(pasynUser, key, val, timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynOptionSyncIO setOption failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus getOptionOnce(const char *port, int addr,
    const char *key, char *val, int sizeval, double timeout, const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port, addr, &pasynUser, drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynOptionSyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = getOption(pasynUser, key, val, sizeval, timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynOptionSyncIO getOption failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

