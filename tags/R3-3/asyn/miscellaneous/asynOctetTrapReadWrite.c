/* asynOctetTrapReadWrite.c*/
/***********************************************************************
* Copyright (c) 2004 Swiss Light Source (SLS).
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
    union {
        asynOctetTrapReadCallback read;
        asynOctetTrapWriteCallback write;
        void* any;
    } callback;
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

static asynStatus trapRead(void *ppvt, asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus trapWrite(void *ppvt, asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus trapFlush(void *ppvt, asynUser *pasynUser);
static asynStatus trapSetEos(void *ppvt, asynUser *pasynUser,
    const char *eos, int eoslen);
static asynStatus trapGetEos(void *ppvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);

static asynOctet octet = {
    trapRead, trapWrite, trapFlush, trapSetEos, trapGetEos
};

/* intercept read and write */

static asynStatus trapRead(void *ppvt, asynUser *pasynUser,
    char *data, int maxchars, int *transfered, int *eomReason)
{
    asynStatus result;
    client* pclient;
    client* pnext;
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    result = ptrapPvt->plowerLevel->read(ptrapPvt->lowerLevelPvt,
        pasynUser, data, maxchars, transfered, eomReason);
    epicsMutexLock(ptrapPvt->mutex);
    pclient = ptrapPvt->readClients;
    while (pclient)
    {
        pnext = pclient->next; /* necessary to allow removing this
                                  client in its own callback */
        if (pclient->pasynUser != pasynUser)
        {
            /* dont trap client's own read */
            pclient->callback.read(pclient->pasynUser, data,
                *transfered, *eomReason, result);
        }
        pclient = pnext;
    }
    epicsMutexUnlock(ptrapPvt->mutex);
    return result;
}

static asynStatus trapWrite(void *ppvt, asynUser *pasynUser,
    const char *data,int numchars,int *transfered)
{
    asynStatus result;
    client* pclient;
    client* pnext;
    trapPvt *ptrapPvt = (trapPvt *)ppvt;

    result = ptrapPvt->plowerLevel->write(ptrapPvt->lowerLevelPvt,
        pasynUser, data, numchars, transfered);
    epicsMutexLock(ptrapPvt->mutex);
    pclient = ptrapPvt->writeClients;
    while (pclient)
    {
        pnext = pclient->next; /* necessary to allow removing this
                                  client in its own callback */
        if (pclient->pasynUser != pasynUser)
        {
            /* dont trap client's own write */
            pclient->callback.write(pclient->pasynUser, data,
                *transfered, result);
        }
        pclient = pnext;
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
    asynUser *pasynUser, asynOctetTrapReadCallback callback);
static asynStatus trapInstallWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapWriteCallback callback);
static asynStatus trapRemoveReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadCallback callback);
static asynStatus trapRemoveWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapWriteCallback callback);

static asynOctetTrapReadWrite trap = {
    trapInstallReadCallback, trapInstallWriteCallback,
    trapRemoveReadCallback, trapRemoveWriteCallback
};

static asynStatus trapInstallCallback (void *drvPvt, int write,
    asynUser *pasynUser, void* callback)
{
    client* pclient;
    client** pprev;
    trapPvt* ptrapPvt = (trapPvt*)drvPvt;

    pclient = calloc(1, sizeof(client));
    if (!pclient)
    {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "out of memory");
        return asynError;
    }
    pclient->pasynUser = pasynUser;
    pclient->callback.any = callback;
    epicsMutexLock(ptrapPvt->mutex);
    for (pprev = write ? &ptrapPvt->writeClients : &ptrapPvt->readClients;
        *pprev; pprev=&(*pprev)->next);
    *pprev = pclient;
    epicsMutexUnlock(ptrapPvt->mutex);
    return asynSuccess;
}

static asynStatus trapRemoveCallback (void *drvPvt, int write,
    asynUser *pasynUser, void* callback)
{
    client* pclient;
    client** pprev;
    trapPvt* ptrapPvt = (trapPvt*)drvPvt;

    epicsMutexLock(ptrapPvt->mutex);
    for (pprev = write ? &ptrapPvt->writeClients : &ptrapPvt->readClients;
        (pclient = *pprev); pprev=&(*pprev)->next)
    {
        if (pclient->pasynUser == pasynUser &&
            pclient->callback.any == callback)
        {
            *pprev = pclient->next;
            free(pclient);
            epicsMutexUnlock(ptrapPvt->mutex);
            return asynSuccess;
        }
    }
    epicsMutexUnlock(ptrapPvt->mutex);
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
        "callback was not installed");
    return asynError;
}

static asynStatus trapInstallReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadCallback callback)
{
    return trapInstallCallback(drvPvt, 0, pasynUser, callback);
}

static asynStatus trapInstallWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapWriteCallback callback)
{
    return trapInstallCallback(drvPvt, 1, pasynUser, callback);
}

static asynStatus trapRemoveReadCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapReadCallback callback)
{
    return trapRemoveCallback(drvPvt, 0, pasynUser, callback);
}

static asynStatus trapRemoveWriteCallback (void *drvPvt,
    asynUser *pasynUser, asynOctetTrapWriteCallback callback)
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
