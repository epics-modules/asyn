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
#include <iocsh.h>

#include <asynDriver.h>
#include <asynOctet.h>

#include <epicsExport.h>

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

typedef struct lowerPort {
    char       *portName;
    int        addr;
    int        canBlock;
    int        autoConnect;
    asynUser   *pasynUser;
    asynOctet  *pasynOctet;
    void       *octetPvt;
}lowerPort;

typedef struct addrChangePvt {
    deviceInfo    device[NUM_DEVICES];
    char          *portName;
    int           isConnected;
    asynUser      *pasynUser;
    asynInterface common;
    asynInterface lockPort;
    asynInterface octet;
    lowerPort     *plowerPort;
    eosSave       *peosSave;
    void          *pasynPvt;   /*For registerInterruptSource*/
}addrChangePvt;
    
/* private routines */
static asynStatus lowerPortInit(addrChangePvt *paddrChangePvt);
static int addrChangeDriverInit(const char *portName, const char *lowerPort,
    int addr);
static void exceptCallback(asynUser * pasynUser, asynException exception);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon common = { report, connect, disconnect };

/* asynLockPortNotify methods */
static asynStatus lockPort(void *drvPvt,asynUser *pasynUser);
static asynStatus unlockPort(void *drvPvt,asynUser *pasynUser);
static asynLockPortNotify lockPortNotify = {lockPort,unlockPort};


/* asynOctet methods */
static asynStatus writeIt(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);


static asynStatus lowerPortInit(addrChangePvt *paddrChangePvt)
{
    lowerPort     *plowerPort = paddrChangePvt->plowerPort;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    plowerPort->pasynUser = pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = paddrChangePvt;
    pasynUser->timeout = 1.0;
    status = pasynManager->connectDevice(pasynUser,
        plowerPort->portName,plowerPort->addr);
    if (status != asynSuccess) {
        printf("connectDevice failed %s\n", pasynUser->errorMessage);
        goto freeAsynUser;
    }
    status = pasynManager->canBlock(pasynUser,&plowerPort->canBlock);
    if(status!=asynSuccess) {
        printf("canBlock failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    status = pasynManager->isAutoConnect(pasynUser,&plowerPort->autoConnect);
    if(status!=asynSuccess) {
        printf("isAutoConnect failed %s\n",pasynUser->errorMessage);
        goto disconnect;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("interface %s not found\n",asynOctetType);
        goto disconnect;
    }
    plowerPort->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    plowerPort->octetPvt = pasynInterface->drvPvt;
    return asynSuccess;
disconnect:
    pasynManager->disconnect(pasynUser);
freeAsynUser:
    pasynManager->freeAsynUser(pasynUser);
    free(paddrChangePvt);
    return asynError;
}

static int addrChangeDriverInit(const char *portName, const char *lowerPortName,
    int addr)
{
    size_t        nbytes;
    addrChangePvt *paddrChangePvt;
    lowerPort     *plowerPort;
    eosSave       *peosSave;
    asynUser      *pasynUser;
    asynStatus    status;
    int           attributes;
    asynOctet     *pasynOctet;

    nbytes = sizeof(addrChangePvt) + sizeof(lowerPort) + sizeof(eosSave)
             + sizeof(asynOctet) 
             + strlen(portName) + 1 + strlen(lowerPortName) + 1;
   
    paddrChangePvt = callocMustSucceed(nbytes,sizeof(char),
        "addrChangeDriverInit");
    paddrChangePvt->plowerPort = plowerPort = (lowerPort *)(paddrChangePvt + 1);
    paddrChangePvt->peosSave = peosSave = (eosSave *)(plowerPort + 1);
    pasynOctet = (asynOctet *)(peosSave + 1);
    paddrChangePvt->portName = (char *)(pasynOctet + 1);
    plowerPort->portName = paddrChangePvt->portName + strlen(portName) + 1;
    strcpy(paddrChangePvt->portName,portName);
    strcpy(plowerPort->portName,lowerPortName);
    plowerPort->addr = addr;
    /* Now initialize rest of lowerPort*/
    if(lowerPortInit(paddrChangePvt)!=asynSuccess) return 0;
    /*Now initialize addrChangePvt and register*/
    attributes = ASYN_MULTIDEVICE;
    if(plowerPort->canBlock) attributes |= ASYN_CANBLOCK;
    status = pasynManager->registerPort(portName,
        attributes,plowerPort->autoConnect,0,0);
    if(status!=asynSuccess) {
        pasynUser = plowerPort->pasynUser;
        printf("addrChangeDriverInit registerDriver failed\n");
        pasynManager->disconnect(pasynUser);
        pasynManager->freeAsynUser(pasynUser);
        free(paddrChangePvt);
        return 0;
    }
    paddrChangePvt->common.interfaceType = asynCommonType;
    paddrChangePvt->common.pinterface  = (void *)&common;
    paddrChangePvt->common.drvPvt = paddrChangePvt;
    status = pasynManager->registerInterface(portName,&paddrChangePvt->common);
    if(status!=asynSuccess){
        printf("addrChangeDriverInit registerInterface failed\n");
        return 0;
    }
    paddrChangePvt->lockPort.interfaceType = asynLockPortNotifyType;
    paddrChangePvt->lockPort.pinterface = (void *)&lockPortNotify;
    paddrChangePvt->lockPort.drvPvt = paddrChangePvt;
    status = pasynManager->registerInterface(portName,&paddrChangePvt->lockPort);
    if(status!=asynSuccess){
        printf("addrChangeDriverInit registerInterface asynLockPortNotify failed\n");
        return 0;
    }
    pasynOctet->write = writeIt;
    pasynOctet->read = readIt;
    paddrChangePvt->octet.interfaceType = asynOctetType;
    paddrChangePvt->octet.pinterface  = pasynOctet;
    paddrChangePvt->octet.drvPvt = paddrChangePvt;
    status = pasynOctetBase->initialize(portName,&paddrChangePvt->octet,
        0,0,0);
    if(status==asynSuccess)
        status = pasynManager->registerInterruptSource(
            portName,&paddrChangePvt->octet,&paddrChangePvt->pasynPvt);
    if(status!=asynSuccess){
        printf("addrChangeDriverInit pasynOctetBase->initialize failed\n");
        return 0;
    }
    paddrChangePvt->pasynUser = pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = paddrChangePvt;
    pasynUser->timeout = 1.0;
    status = pasynManager->connectDevice(pasynUser,portName,-1);
    if (status != asynSuccess) {
        printf("connectDevice failed %s WHY???\n", pasynUser->errorMessage);
    }
    pasynManager->exceptionCallbackAdd(plowerPort->pasynUser, exceptCallback);
    return(0);
}

static void exceptCallback(asynUser * pasynUser, asynException exception)
{
    addrChangePvt *paddrChangePvt = pasynUser->userPvt;
    lowerPort     *plowerPort = paddrChangePvt->plowerPort;
    int           isConnected = 0;
    asynStatus    status;

    status = pasynManager->isConnected(pasynUser,&isConnected);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s isConnected to %s failed %s\n",
            paddrChangePvt->portName,plowerPort->portName,
            pasynUser->errorMessage);
        return;
    }
    if(isConnected) return;
    if(!paddrChangePvt->isConnected) return;
    paddrChangePvt->isConnected = 0;
    pasynManager->exceptionDisconnect(paddrChangePvt->pasynUser);
}

static asynStatus lockPort(void *drvPvt,asynUser *pasynUser)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    lowerPort *plowerPort = paddrChangePvt->plowerPort;
    asynUser *pasynUserLower = plowerPort->pasynUser;
    asynStatus status;

    status = pasynManager->lockPort(pasynUserLower);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s lockPort to %s %s",
            paddrChangePvt->portName,plowerPort->portName,
            pasynUserLower->errorMessage);
        asynPrint(pasynUser,ASYN_TRACE_ERROR, "%s\n",pasynUser->errorMessage);
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s lockPort %s\n",paddrChangePvt->portName,plowerPort->portName);
    return asynSuccess;
}

static asynStatus unlockPort(void *pvt,asynUser *pasynUser)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)pvt;
    lowerPort *plowerPort = paddrChangePvt->plowerPort;
    asynUser *pasynUserLower = plowerPort->pasynUser;
    

    if((pasynManager->unlockPort(pasynUserLower))!=asynSuccess ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s unlockPort error %s\n",
            plowerPort->portName,pasynUserLower->errorMessage);
    }
    return asynSuccess;
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    lowerPort     *plowerPort = paddrChangePvt->plowerPort;

    fprintf(fp,"    %s connected to %s\n",
         paddrChangePvt->portName,plowerPort->portName);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    addrChangePvt    *paddrChangePvt = (addrChangePvt *)drvPvt;
    int              addr;
    asynStatus       status;

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
    lowerPort     *plowerPort = paddrChangePvt->plowerPort;
    asynOctet     *pasynOctet = plowerPort->pasynOctet;
    void          *octetPvt = plowerPort->octetPvt;
    asynStatus    status;

    status = pasynOctet->write(octetPvt,plowerPort->pasynUser,
        data,numchars,nbytesTransfered);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",plowerPort->portName,
            plowerPort->pasynUser->errorMessage);
    }
    return status;
}

static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    addrChangePvt *paddrChangePvt = (addrChangePvt *)drvPvt;
    lowerPort     *plowerPort = paddrChangePvt->plowerPort;
    asynOctet     *pasynOctet = plowerPort->pasynOctet;
    void          *octetPvt = plowerPort->octetPvt;
    asynStatus    status;

    status = pasynOctet->read(octetPvt,plowerPort->pasynUser,
        data,maxchars,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            " port %s error %s",plowerPort->portName,
            plowerPort->pasynUser->errorMessage);
    }
    pasynOctetBase->callInterruptUsers(pasynUser,paddrChangePvt->pasynPvt,
        data,nbytesTransfered,eomReason);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,data,*nbytesTransfered,
        "addrChangeDriver\n");
    return status;
}

/* register addrChangeDriverInit*/
static const iocshArg addrChangeDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg addrChangeDriverInitArg1 = { "lowerPort", iocshArgString};
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
