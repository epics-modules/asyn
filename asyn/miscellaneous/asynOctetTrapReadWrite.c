/* asynOctetTrapReadWrite.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* 
*  Interpose Interface for asynOctet
*
*  This driver intercepts reads and writes and calls any client
*  that has installed a callback.
*
*  Author: Dirk Zimoch
*
*/
 

#include "asynOctetTrapReadWrite.h"
#include <stdlib.h>
#include <cantProceed.h>
#include <epicsString.h>
#include <epicsMutex.h>
#include <iocsh.h>
#include <epicsExport.h>

typedef struct client {
    asynUser *pasynUser;
    asynOctetTrapReadWriteCallback callback;
    struct client *next;
} client;

typedef struct trapPvt {
    const char *portName;
    asynInterface octet;
    asynInterface trap;
    asynOctet *plowerLevel;
    void *lowerLevelPvt;
    client *readClients;
    client *writeClients;
    epicsMutexId mutex;
} trapPvt;

/* asynOctet methods */

static asynStatus trapRead(void *ppvt,
    asynUser *pasynUser, char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus trapWrite(void *ppvt,
    asynUser *pasynUser, const char *data,int numchars,int *nbytesTransfered);
static asynStatus trapFlush(void *ppvt, asynUser *pasynUser);
static asynStatus trapSetEos(void *ppvt,
    asynUser *pasynUser, const char *eos, int eoslen);
static asynStatus trapGetEos(void *ppvt,
    asynUser *pasynUser, char *eos, int eossize, int *eoslen);

static asynOctet octet = {
    trapRead,trapWrite,trapFlush,trapSetEos,trapGetEos
};

/* intercept read and write */

static asynStatus trapRead(void *ppvt, asynUser *pasynUser,
    char *data,int maxchars,int *transfered,int *eomReason)
{
    asynStatus result;
    client* pclient;
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    result = ptrapPvt->plowerLevel->read(ptrapPvt->lowerLevelPvt,
        pasynUser, data, maxchars, transfered,eomReason);
    epicsMutexLock(ptrapPvt->mutex);
    for (pclient = ptrapPvt->readClients; pclient; pclient = pclient->next)
    {
        pclient->callback(pclient->pasynUser, data, *transfered, result);
    }
    epicsMutexUnlock(ptrapPvt->mutex);
    return result;
}

static asynStatus trapWrite(void *ppvt, asynUser *pasynUser,
    const char *data,int numchars,int *transfered)
{
    asynStatus result;
    client* pclient;
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    result = ptrapPvt->plowerLevel->write(ptrapPvt->lowerLevelPvt,
        pasynUser, data, numchars, transfered);
    epicsMutexLock(ptrapPvt->mutex);
    for (pclient = ptrapPvt->writeClients; pclient; pclient = pclient->next)
    {
        pclient->callback(pclient->pasynUser, data, *transfered, result);
    }
    epicsMutexUnlock(ptrapPvt->mutex);
    return result;
}

/* just pass flush, setEos and getEos to low level driver */

static asynStatus trapFlush(void *ppvt, asynUser *pasynUser)
{
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    return ptrapPvt->plowerLevel->flush(ptrapPvt->lowerLevelPvt,
        pasynUser);
}

static asynStatus trapSetEos(void *ppvt, asynUser *pasynUser,
    const char *eos, int eoslen)
{
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    return ptrapPvt->plowerLevel->setEos(ptrapPvt->lowerLevelPvt,
        pasynUser, eos, eoslen);
}

static asynStatus trapGetEos(void *ppvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    return ptrapPvt->plowerLevel->getEos(ptrapPvt->lowerLevelPvt,
        pasynUser, eos, eossize, eoslen);
}

/* asynOctetTrapReadWrite methods */

static asynStatus trapInstallReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback);
static asynStatus trapInstallWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback);
static asynStatus trapRemoveReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback);
static asynStatus trapRemoveWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback);

static asynOctetTrapReadWrite trap = {
    trapInstallReadCallback, trapInstallWriteCallback,
    trapRemoveReadCallback, trapRemoveWriteCallback
};

static asynStatus trapInstallCallback (void *drvPvt, int write,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    client* pclient;
    client** pprev;
    trapPvt* ptrapPvt = (trapPvt*)drvPvt;

    pclient = calloc(1, sizeof(client));
    if (!pclient) return asynError;
    pclient->pasynUser = pasynUser;
    pclient->callback = callback;
    epicsMutexLock(ptrapPvt->mutex);
    for (pprev = write ? &ptrapPvt->writeClients : &ptrapPvt->readClients;
        *pprev; pprev=&(*pprev)->next);
    *pprev = pclient;
    epicsMutexUnlock(ptrapPvt->mutex);
    return asynSuccess;
}

static asynStatus trapRemoveCallback (void *drvPvt, int write,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    client* pclient;
    client** pprev;
    trapPvt* ptrapPvt = (trapPvt*)drvPvt;

    epicsMutexLock(ptrapPvt->mutex);
    for (pprev = write ? &ptrapPvt->writeClients : &ptrapPvt->readClients;
        *pprev; pprev=&(*pprev)->next)
    {
        if ((*pprev)->pasynUser == pasynUser &&
            (*pprev)->callback == callback)
        {
            pclient = *pprev;
            *pprev = pclient->next;
            free(pclient);
            epicsMutexUnlock(ptrapPvt->mutex);
            return asynSuccess;
        }
    }
    epicsMutexUnlock(ptrapPvt->mutex);
    return asynError;
}

static asynStatus trapInstallReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    return trapInstallCallback(drvPvt, 0, pasynUser, callback);
}

static asynStatus trapInstallWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    return trapInstallCallback(drvPvt, 1, pasynUser, callback);
}

static asynStatus trapRemoveReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    return trapRemoveCallback(drvPvt, 0, pasynUser, callback);
}

static asynStatus trapRemoveWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadWriteCallback callback)
{
    return trapRemoveCallback(drvPvt, 1, pasynUser, callback);
}

int epicsShareAPI asynOctetTrapReadWriteConfig(const char *portName, int addr)
{
    trapPvt *ptrapPvt;
    asynInterface *plowerLevelInterface;

    ptrapPvt = callocMustSucceed(1, sizeof(trapPvt),
        "asynOctetTrapReadWriteConfig");
    ptrapPvt->mutex = epicsMutexMustCreate();
    epicsMutexLock(ptrapPvt->mutex);
    ptrapPvt->portName = epicsStrDup(portName);
    ptrapPvt->octet.interfaceType = asynOctetType;
    ptrapPvt->octet.pinterface = &octet;
    ptrapPvt->octet.drvPvt = ptrapPvt;
    ptrapPvt->trap.interfaceType = asynOctetTrapReadWriteType;
    ptrapPvt->trap.pinterface = &trap;
    ptrapPvt->trap.drvPvt = ptrapPvt;
    ptrapPvt->readClients = NULL;
    ptrapPvt->writeClients = NULL;
        
    if (pasynManager->registerInterface(portName, &ptrapPvt->trap)
        != asynSuccess)
    {
        printf("asynOctetTrapReadWriteConfig: registerInterface %s failed.\n",
            portName);
    }
    else if ((pasynManager->interposeInterface(portName, addr,
        &ptrapPvt->octet, &plowerLevelInterface) != asynSuccess)
        || (plowerLevelInterface == NULL))
    {
        printf("asynOctetTrapReadWriteConfig: interposeInterface %s failed.\n",
            portName);
    }
    else
    {
        ptrapPvt->plowerLevel = (asynOctet *)plowerLevelInterface->pinterface;
        ptrapPvt->lowerLevelPvt = plowerLevelInterface->drvPvt;
        epicsMutexUnlock(ptrapPvt->mutex);
        return 0;
    }
    free((void *)ptrapPvt->portName);
    epicsMutexDestroy(ptrapPvt->mutex);
    free(ptrapPvt);
    return -1;
}

/* register asynOctetTrapReadWriteConfig*/
static const iocshArg asynOctetTrapReadWriteConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg asynOctetTrapReadWriteConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg *asynOctetTrapReadWriteConfigArgs[] = 
    {&asynOctetTrapReadWriteConfigArg0,&asynOctetTrapReadWriteConfigArg1};
static const iocshFuncDef asynOctetTrapReadWriteConfigFuncDef =
    {"asynOctetTrapReadWriteConfig", 2, asynOctetTrapReadWriteConfigArgs};
static void asynOctetTrapReadWriteConfigCallFunc(const iocshArgBuf *args)
{
    asynOctetTrapReadWriteConfig(args[0].sval,args[1].ival);
}

static void asynOctetTrapReadWriteRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynOctetTrapReadWriteConfigFuncDef,
            asynOctetTrapReadWriteConfigCallFunc);
    }
}

epicsExportRegistrar(asynOctetTrapReadWriteRegister);
