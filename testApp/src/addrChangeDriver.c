/*addrChangeDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*test driver for multi-device driver using single-device driver*/
/* 
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynOctet.h>

#define NUM_DEVICES 2
typedef struct deviceInfo {
    int  isConnected;
    char eosIn[2];
    int  eosInLen;
    char eosOut[2];
    int  eosOutLen;
}deviceInfo;

typedef struct eosSave {
    int  restore;
    char eos[2];
    int  len;
    int  addr;
}eosSave;

typedef struct otherPort {
    char       *portName;
    int        addr;
    int        isConnected;
    int        multiDevice;
    int        autoConnect;
    int        canBlock;
    asynUser   *pasynUser;
    asynCommon *pasynCommon;
    void       *commonPvt;
    asynOctet  *pasynOctet;
    void       *octetPvt;
}otherPort;

typedef struct addrChangePvt {
    deviceInfo    device[NUM_DEVICES];
    char          *portName;
    int           isConnected;
    asynUser      *pasynUser;
    asynInterface common;
    asynInterface octet;
    otherPort     *potherPort;
    eosSave       *peosSave;
}addrChangePvt;
    
/* private routines */
static asynStatus otherPortInit(addrChangePvt *paddrChangePvt);
static int addrChangeDriverInit(const char *portName, const char *otherPort,
    int addr);
static void exceptCallback(asynUser * pasynUser, asynException exception);
static asynStatus lockPort(addrChangePvt *paddrChangePvt,asynUser *pasynUser);
static void unlockPort(addrChangePvt *paddrChangePvt,asynUser *pasynUser);
static void saveEosIn(addrChangePvt *paddrChangePvt);
static void restoreEosIn(addrChangePvt *paddrChangePvt);
static void saveEosOut(addrChangePvt *paddrChangePvt);
static void restoreEosOut(addrChangePvt *paddrChangePvt);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

/* asynOctet methods */
static asynStatus writeIt(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus writeRaw(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus readRaw(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus flushIt(void *drvPvt,asynUser *pasynUser);
static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
     interruptCallbackOctet callback, void *userPvt,
     void **registrarPvt);
static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser);
static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getOutputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynOctet octet = {
    writeIt,writeRaw,readIt,readRaw, flushIt,
    registerInterruptUser,cancelInterruptUser,
    setInputEos, getInputEos,setOutputEos,getOutputEos };

static asynStatus otherPortInit(addrChangePvt *paddrChangePvt)
{
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynUser      *pasynUser;
    asynStatus    status;
    int           addr;
    asynInterface *pasynInterface;

    potherPort->pasynUser = pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = paddrChangePvt;
    pasynUser->timeout = 1.0;
    status = pasynManager->connectDevice(pasynUser,
        potherPort->portName,addr);
    if (status != asynSuccess) {
        printf("connectDevice failed %s\n", pasynUser->errorMessage);
        goto freeAsynUser;
    }
    status = pasynManager->isMultiDevice(pasynUser,potherPort->portName,
                                         &potherPort->multiDevice);
    if(status!=asynSuccess) {
        printf("isMultiDevice failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    status = pasynManager->canBlock(pasynUser,&potherPort->canBlock);
    if(status!=asynSuccess) {
        printf("canBlock failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    status = pasynManager->isAutoConnect(pasynUser,&potherPort->autoConnect);
    if(status!=asynSuccess) {
        printf("isAutoConnect failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    status = pasynManager->isConnected(pasynUser,&potherPort->isConnected);
    if(status!=asynSuccess) {
        printf("isAutoConnect failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("interface %s not found\n",asynCommonType);
        goto disconnect;
    }
    potherPort->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    potherPort->commonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("interface %s not found\n",asynOctetType);
        goto disconnect;
    }
    potherPort->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    potherPort->octetPvt = pasynInterface->drvPvt;
    return asynSuccess;
disconnect:
    pasynManager->disconnect(pasynUser);
freeAsynUser:
    pasynManager->freeAsynUser(pasynUser);
    free(paddrChangePvt);
    return asynError;
}

static int addrChangeDriverInit(const char *portName, const char *otherPortName,
    int addr)
{
    size_t        nbytes;
    addrChangePvt *paddrChangePvt;
    otherPort     *potherPort;
    eosSave       *peosSave;
    asynUser      *pasynUser;
    asynStatus    status;
    int           attributes;

    nbytes = sizeof(addrChangePvt) + sizeof(otherPort) + sizeof(eosSave)
             + strlen(portName) + 1 + strlen(otherPortName) + 1;
   
    paddrChangePvt = callocMustSucceed(nbytes,sizeof(char),
        "addrChangeDriverInit");
    paddrChangePvt->potherPort = potherPort = (otherPort *)(paddrChangePvt + 1);
    paddrChangePvt->peosSave = peosSave = (eosSave *)(potherPort + 1);
    paddrChangePvt->portName = (char *)(peosSave + 1);
    potherPort->portName = paddrChangePvt->portName + strlen(portName) + 1;
    strcpy(paddrChangePvt->portName,portName);
    strcpy(potherPort->portName,otherPortName);
    potherPort->addr = addr;
    /* Now initialize rest of otherPort*/
    if(otherPortInit(paddrChangePvt)!=asynSuccess) return 0;
    /*Now initialize addrChangePvt and register*/
    paddrChangePvt->common.interfaceType = asynCommonType;
    paddrChangePvt->common.pinterface  = (void *)&asyn;
    paddrChangePvt->common.drvPvt = paddrChangePvt;
    paddrChangePvt->octet.interfaceType = asynOctetType;
    paddrChangePvt->octet.pinterface  = (void *)&octet;
    paddrChangePvt->octet.drvPvt = paddrChangePvt;
    attributes = ASYN_MULTIDEVICE;
    if(potherPort->canBlock) attributes |= ASYN_CANBLOCK;
    status = pasynManager->registerPort(portName,
        attributes,potherPort->autoConnect,0,0);
    if(status!=asynSuccess) {
        pasynUser = potherPort->pasynUser;
        printf("addrChangeDriverInit registerDriver failed\n");
        pasynManager->disconnect(pasynUser);
        pasynManager->freeAsynUser(pasynUser);
        free(paddrChangePvt);
        return 0;
    }
    status = pasynManager->registerInterface(portName,&paddrChangePvt->common);
    if(status!=asynSuccess){
        printf("addrChangeDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&paddrChangePvt->octet);
    if(status!=asynSuccess){
        printf("addrChangeDriverInit registerInterface failed\n");
        return 0;
    }
    paddrChangePvt->pasynUser = pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = paddrChangePvt;
    pasynUser->timeout = 1.0;
    status = pasynManager->connectDevice(pasynUser,portName,-1);
    if (status != asynSuccess) {
        printf("connectDevice failed %s WHY???\n", pasynUser->errorMessage);
    }
    pasynManager->exceptionCallbackAdd(potherPort->pasynUser, exceptCallback);
    return(0);
}

static void exceptCallback(asynUser * pasynUser, asynException exception)
{
    addrChangePvt *paddrChangePvt = pasynUser->userPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int           isConnected = 0;
    asynStatus    status;

    status = pasynManager->isConnected(pasynUser,&isConnected);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s isConnected to %s failed %s\n",
            paddrChangePvt->portName,potherPort->portName,
            pasynUser->errorMessage);
        return;
    }
    if(isConnected==potherPort->isConnected) return;
    potherPort->isConnected = isConnected;
    if(isConnected || !paddrChangePvt->isConnected) return;
    paddrChangePvt->isConnected = 0;
    pasynManager->exceptionDisconnect(paddrChangePvt->pasynUser);
}

static asynStatus lockPort(addrChangePvt *paddrChangePvt,asynUser *pasynUser)
{
    otherPort *potherPort = paddrChangePvt->potherPort;
    int       addr;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<0 || addr >1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal address %d. Must be 1 or 2\n",addr);
        return asynError;
    }
    paddrChangePvt->peosSave->addr = addr;
    status = pasynManager->lockPort(potherPort->pasynUser,1);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " lockPort %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    return status;
}

static void unlockPort(addrChangePvt *paddrChangePvt,asynUser *pasynUser)
{
    otherPort *potherPort = paddrChangePvt->potherPort;

    if((pasynManager->unlockPort(potherPort->pasynUser)) ) {
        asynPrint(potherPort->pasynUser,ASYN_TRACE_ERROR,
            "%s unlockPort error %s\n",
            potherPort->portName,potherPort->pasynUser->errorMessage);
    }
}

static void saveEosIn(addrChangePvt *paddrChangePvt)
{
    otherPort  *potherPort = paddrChangePvt->potherPort;
    asynUser   *pasynUser = potherPort->pasynUser;
    asynOctet  *pasynOctet = potherPort->pasynOctet;
    void       *octetPvt = potherPort->octetPvt;
    eosSave    *peosSave = paddrChangePvt->peosSave;
    int        addr = peosSave->addr;
    deviceInfo *pdeviceInfo = &paddrChangePvt->device[addr];
    asynStatus status;

    peosSave->restore = 0;
    status = pasynOctet->getInputEos(octetPvt,pasynUser,
        peosSave->eos,sizeof(peosSave->eos), &peosSave->len);
    if(status!=asynSuccess) return;
    status = pasynOctet->setInputEos(octetPvt,pasynUser,
        pdeviceInfo->eosIn,pdeviceInfo->eosInLen);
    if(status!=asynSuccess) return;
    peosSave->restore = 1;
}

static void restoreEosIn(addrChangePvt *paddrChangePvt)
{
    otherPort  *potherPort = paddrChangePvt->potherPort;
    asynUser   *pasynUser = potherPort->pasynUser;
    asynOctet  *pasynOctet = potherPort->pasynOctet;
    void       *octetPvt = potherPort->octetPvt;
    eosSave    *peosSave = paddrChangePvt->peosSave;
    int        addr = peosSave->addr;
    deviceInfo *pdeviceInfo = &paddrChangePvt->device[addr];
    asynStatus status;

    if(!peosSave->restore) return;
    status = pasynOctet->setInputEos(octetPvt,pasynUser,
        peosSave->eos,peosSave->len);
    if(status!=asynSuccess) {
         printf("%s %d setInputEos while restoring %s\n",
             paddrChangePvt->portName,addr,pasynUser->errorMessage);
    }
}

static void saveEosOut(addrChangePvt *paddrChangePvt)
{
    otherPort  *potherPort = paddrChangePvt->potherPort;
    asynUser   *pasynUser = potherPort->pasynUser;
    asynOctet  *pasynOctet = potherPort->pasynOctet;
    void       *octetPvt = potherPort->octetPvt;
    eosSave    *peosSave = paddrChangePvt->peosSave;
    int        addr = peosSave->addr;
    deviceInfo *pdeviceInfo = &paddrChangePvt->device[addr];
    asynStatus status;

    peosSave->restore = 0;
    status = pasynOctet->getOutputEos(octetPvt,pasynUser,
        peosSave->eos,sizeof(peosSave->eos), &peosSave->len);
    if(status!=asynSuccess) return;
    status = pasynOctet->setOutputEos(octetPvt,pasynUser,
        pdeviceInfo->eosIn,pdeviceInfo->eosInLen);
    if(status!=asynSuccess) return;
    peosSave->restore = 1;
}

static void restoreEosOut(addrChangePvt *paddrChangePvt)
{
    otherPort  *potherPort = paddrChangePvt->potherPort;
    asynUser   *pasynUser = potherPort->pasynUser;
    asynOctet  *pasynOctet = potherPort->pasynOctet;
    void       *octetPvt = potherPort->octetPvt;
    eosSave    *peosSave = paddrChangePvt->peosSave;
    int        addr = peosSave->addr;
    deviceInfo *pdeviceInfo = &paddrChangePvt->device[addr];
    asynStatus status;

    if(!peosSave->restore) return;
    status = pasynOctet->setOutputEos(octetPvt,pasynUser,
        peosSave->eos,peosSave->len);
    if(status!=asynSuccess) {
         printf("%s %d setOutputEos while restoring %s\n",
             paddrChangePvt->portName,addr,pasynUser->errorMessage);
    }
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int i;

    fprintf(fp,"    %s connected to %s\n",
         paddrChangePvt->portName,potherPort->portName);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    addrChangePvt    *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort        *potherPort = paddrChangePvt->potherPort;
    int              isConnected = 0;
    int              isAutoConnect = 0;
    int              addr;
    asynStatus       status;

    status = pasynManager->isConnected(potherPort->pasynUser,&isConnected);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s isConnected error %s\n",
            potherPort->portName,potherPort->pasynUser->errorMessage);
        return asynError;
    }
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s addrChangeDriver:connect addr %d\n",paddrChangePvt->portName,addr);
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addrChangeDriver:connect illegal addr %d\n",
            paddrChangePvt->portName,addr);
        return asynError;
    }
    if(addr>=0) {
        deviceInfo *pdeviceInfo = &paddrChangePvt->device[addr];
        if(pdeviceInfo->isConnected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s addrChangeDriver:connect device %d already connected\n",
                paddrChangePvt->portName,addr);
            return asynError;
        }
        pdeviceInfo->isConnected = 1;
        pasynManager->exceptionConnect(pasynUser);
        return(asynSuccess);
    }
    if(paddrChangePvt->isConnected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " already connected");
        return asynError;
    }
    if(!isConnected) {
        status = pasynManager->isAutoConnect(potherPort->pasynUser,&isAutoConnect);
        if(status!=asynSuccess) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                " port %s isAutoConnect error %s\n",
                potherPort->portName,potherPort->pasynUser->errorMessage);
            return asynError;
        }
        if(isAutoConnect) {
            status = potherPort->pasynCommon->connect(
                potherPort->commonPvt,potherPort->pasynUser);
            if(status!=asynSuccess) {
                 epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                     " connect to %s failed %s",
                     potherPort->portName,potherPort->pasynUser->errorMessage);
                 return asynError;
            }
            status = pasynManager->isConnected(potherPort->pasynUser,&isConnected);
            if(status!=asynSuccess) {
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    " port %s isConnected error %s\n",
                    potherPort->portName,potherPort->pasynUser->errorMessage);
                return asynError;
            }
        }
    }
    if(!isConnected) return asynSuccess;
    paddrChangePvt->isConnected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    int           addr;
    asynStatus    status;
    deviceInfo    *pdeviceInfo;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addrChangeDriver:connect illegal addr %d\n",
            paddrChangePvt->portName,addr);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "illegal addr %d", addr);
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s addrChangeDriver:disconnect addr %d\n",
        paddrChangePvt->portName,addr);
    if(addr<=-1) {
        if(!paddrChangePvt->isConnected) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "not connected");
            return asynError;
        }
        paddrChangePvt->isConnected = 0;
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    pdeviceInfo = &paddrChangePvt->device[addr];
    if(!pdeviceInfo->isConnected) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "not connected");
        return asynError;
    }
    pdeviceInfo->isConnected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return(asynSuccess);
}

/* asynOctet methods*/
static asynStatus writeIt(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->write(octetPvt,potherPort->pasynUser,
        data,numchars,nbytesTransfered);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus writeRaw(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->writeRaw(octetPvt,potherPort->pasynUser,
        data,numchars,nbytesTransfered);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->read(octetPvt,potherPort->pasynUser,
        data,maxchars,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus readRaw(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->readRaw(octetPvt,potherPort->pasynUser,
        data,maxchars,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus flushIt(void *drvPvt,asynUser *pasynUser)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->flush(octetPvt,potherPort->pasynUser);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus registerInterruptUser(void *drvPvt,asynUser *pasynUser,
     interruptCallbackOctet callback, void *userPvt,
     void **registrarPvt)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->registerInterruptUser(octetPvt,potherPort->pasynUser,
        callback,userPvt,registrarPvt);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus cancelInterruptUser(void *registrarPvt, asynUser *pasynUser)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)registrarPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    asynOctet     *pasynOctet = potherPort->pasynOctet;
    void          *octetPvt = potherPort->octetPvt;
    asynStatus    status;

    status = lockPort(paddrChangePvt,pasynUser);
    if(status!=asynSuccess) return status;
    status = pasynOctet->cancelInterruptUser(octetPvt,potherPort->pasynUser);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",potherPort->portName,
            potherPort->pasynUser->errorMessage);
    }
    unlockPort(paddrChangePvt,pasynUser);
    return status;
}

static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int           addr;
    asynStatus    status;
    deviceInfo    *pdeviceInfo;
    int           i;
    
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<0 || addr>1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal address %d. Must be 1 or 2\n",addr);
        return asynError;
    }
    if(eoslen<0 || eoslen>2) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal eoslen %d. Must be 0 or 1 or 2\n",eoslen);
        return asynError;
    }
    pdeviceInfo = &paddrChangePvt->device[addr];
    pdeviceInfo->eosInLen = eoslen;
    for(i=0; i<eoslen; i++) pdeviceInfo->eosIn[i] = eos[i];
    return asynSuccess;
}

static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int           addr;
    asynStatus    status;
    deviceInfo    *pdeviceInfo;
    int           i;
    
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<0 || addr>1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal address %d. Must be 1 or 2\n",addr);
        return asynError;
    }
    pdeviceInfo = &paddrChangePvt->device[addr];
    if(eossize<pdeviceInfo->eosInLen) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal eossize %d. Must be 0 or 1 or 2\n",eossize);
        return asynError;
    }
    *eoslen = pdeviceInfo->eosInLen;
    for(i=0; i<pdeviceInfo->eosInLen; i++) eos[i] = pdeviceInfo->eosIn[i];
    return asynSuccess;
}

static asynStatus setOutputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int           addr;
    asynStatus    status;
    deviceInfo    *pdeviceInfo;
    int           i;
    
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<0 || addr>1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal address %d. Must be 1 or 2\n",addr);
        return asynError;
    }
    if(eoslen<0 || eoslen>2) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal eoslen %d. Must be 0 or 1 or 2\n",eoslen);
        return asynError;
    }
    pdeviceInfo = &paddrChangePvt->device[addr];
    pdeviceInfo->eosOutLen = eoslen;
    for(i=0; i<eoslen; i++) pdeviceInfo->eosOut[i] = eos[i];
    return asynSuccess;
}

static asynStatus getOutputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    otherPort     *potherPort = paddrChangePvt->potherPort;
    int           addr;
    asynStatus    status;
    deviceInfo    *pdeviceInfo;
    int           i;
    
    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(addr<0 || addr>1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal address %d. Must be 1 or 2\n",addr);
        return asynError;
    }
    pdeviceInfo = &paddrChangePvt->device[addr];
    if(eossize<pdeviceInfo->eosOutLen) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Illegal eossize %d. Must be 0 or 1 or 2\n",eossize);
        return asynError;
    }
    *eoslen = pdeviceInfo->eosOutLen;
    for(i=0; i<pdeviceInfo->eosOutLen; i++) eos[i] = pdeviceInfo->eosOut[i];
    return asynSuccess;
}

/* register addrChangeDriverInit*/
static const iocshArg addrChangeDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg addrChangeDriverInitArg1 = { "otherPort", iocshArgString};
static const iocshArg addrChangeDriverInitArg2 = { "addr", iocshArgInt };
static const iocshArg *addrChangeDriverInitArgs[] = {
    &addrChangeDriverInitArg0,&addrChangeDriverInitArg1,
    &addrChangeDriverInitArg2};
static const iocshFuncDef addrChangeDriverInitFuncDef = {
    "addrChangeDriverInit", 3, addrChangeDriverInitArgs};
static void addrChangeDriverInitCallFunc(const iocshArgBuf *args)
{
    addrChangeDriverInit(args[0].sval,args[1].sval,args[2].ival);
}

static void addrChangeDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&addrChangeDriverInitFuncDef, addrChangeDriverInitCallFunc);
    }
}
epicsExportRegistrar(addrChangeDriverRegister);
