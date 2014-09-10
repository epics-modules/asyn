/*asynCommonSyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynCommon
 * Author:  Marty Kraimer
 * Created: 12OCT2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>
#include <epicsEvent.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynCommonSyncIO.h"

#define DEFAULT_CONNECT_TIMEOUT 1.0

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
   epicsEventId connectEvent;
   int          connect;
   asynStatus   connectStatus;
   
}ioPvt;

/*asynCommonSyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus connectDevice(asynUser *pasynUser);
static asynStatus disconnectDevice(asynUser *pasynUser);
static asynStatus report(asynUser *pasynUser, FILE *fp, int details);
static asynCommonSyncIO interface = {
    connect,
    disconnect,
    connectDevice,
    disconnectDevice,
    report
};
epicsShareDef asynCommonSyncIO *pasynCommonSyncIO = &interface;

/* Private methods */
static void connectDeviceCallback(asynUser *pasynUser);

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynCommonSyncIO");
    pioPvt->connectEvent = epicsEventMustCreate(epicsEventEmpty);
    pasynUser = pasynManager->createAsynUser(connectDeviceCallback,0);
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
    epicsEventDestroy(pioPvt->connectEvent);
    free(pioPvt);
    return asynSuccess;
}
 

static void connectDeviceCallback(asynUser *pasynUser)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    if (pioPvt->connect)
        pioPvt->connectStatus = pioPvt->pasynCommon->connect(pioPvt->pcommonPvt, pasynUser);
    else
        pioPvt->connectStatus = pioPvt->pasynCommon->disconnect(pioPvt->pcommonPvt, pasynUser);
    epicsEventSignal(pioPvt->connectEvent);
}

static asynStatus connectDevice(asynUser *pasynUser)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pioPvt->connect = 1;
    status = pasynManager->queueRequest(pasynUser, asynQueuePriorityConnect, 0.);
    if (status != asynSuccess) return(status);
    epicsEventMustWait(pioPvt->connectEvent);
    return(pioPvt->connectStatus);
}

static asynStatus disconnectDevice(asynUser *pasynUser)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pioPvt->connect = 0;
    status = pasynManager->queueRequest(pasynUser, asynQueuePriorityConnect, 0.);
    if (status != asynSuccess) return(status);
    epicsEventMustWait(pioPvt->connectEvent);
    return(pioPvt->connectStatus);
}

static asynStatus report(asynUser *pasynUser, FILE *fp, int details)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pioPvt->pasynCommon->report(pioPvt->pcommonPvt, fp, details);
    return(asynSuccess);
}
