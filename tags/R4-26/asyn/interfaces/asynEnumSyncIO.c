/*asynEnumSyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynEnum
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
#include "asynEnum.h"
#include "asynDrvUser.h"
#include "asynEnumSyncIO.h"

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynEnum     *pasynEnum;
   void         *enumPvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
}ioPvt;

/*asynEnumSyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser, char *strings[], int values[], int severities[], 
                          size_t nElements, double timeout);
static asynStatus readOp(asynUser *pasynUser, char *strings[], int values[], int severities[], 
                         size_t nElements, size_t *nIn, double timeout);
static asynStatus writeOpOnce(const char *port, int addr, char *strings[], int values[], int severities[], 
                              size_t nElements, double timeout,const char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr, char *strings[], int values[], int severities[], 
                             size_t nElements, size_t *nIn, double timeout, const char *drvInfo);
static asynEnumSyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    writeOpOnce,
    readOpOnce,
};
epicsShareDef asynEnumSyncIO *pasynEnumSyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynEnumSyncIO");
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
    pasynInterface = pasynManager->findInterface(pasynUser, asynEnumType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "port does not implement interface %s",asynEnumType);
       return asynError;
    }
    pioPvt->pasynEnum = (asynEnum *)pasynInterface->pinterface;
    pioPvt->enumPvt = pasynInterface->drvPvt;
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
 

static asynStatus writeOp(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, double timeout)
{
    asynStatus status, unlockStatus;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynEnum->write(pioPvt->enumPvt, pasynUser, strings, values, severities, nElements);
    if (status==asynSuccess) {
        int i;
        for (i=0; i<(int)nElements; i++) {
          asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                    "asynEnumSyncIO wrote: %d string=%s, value=%d severity=%d\n", i, strings[i], values[i], severities[i]);
        }
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus readOp(asynUser *pasynUser, char *strings[], int values[], int severities[], size_t nElements, size_t *nIn, double timeout)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pioPvt->pasynEnum->read(pioPvt->enumPvt, pasynUser, strings, values, severities, nElements, nIn);
    if (status==asynSuccess) {
        int i;
        for (i=0; i<(int)*nIn; i++) {
          asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                    "asynEnumSyncIO read: %d string=%s, value=%d, severity=%d\n", i, strings[i], values[i], severities[i]);
        }
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return(status);
}

static asynStatus writeOpOnce(const char *port, int addr,
    char *strings[], int values[], int severities[], size_t nElements, double timeout, const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynEnumSyncIO connect failed %s\n",
            pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = writeOp(pasynUser, strings, values, severities, nElements, timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynEnumSyncIO writeOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOpOnce(const char *port, int addr,
                   char *strings[], int values[], int severities[], size_t nElements, size_t *nIn, double timeout, const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynEnumSyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = readOp(pasynUser, strings, values, severities, nElements, nIn, timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynEnumSyncIO readOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}
