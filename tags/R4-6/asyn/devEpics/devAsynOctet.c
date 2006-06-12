/* devAsynOctet.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Marty Kraimer
    02SEP2004

    This file provides device support for stringin, stringout, and waveform.
    NOTE: waveform must be a array of chars
    asynSiOctetCmdResponse,asynWfOctetCmdResponse:
        INP has a command string.
        The command string is sent and a response read.
    asynSiOctetWriteRead,asynWfOctetWriteRead
        INP contains the name of a PV (string or array of chars)
        The value read from PV is sent and a respose read.
    asynSiOctetRead,asynWfOctetRead
        INP contains <drvUser> which is passed to asynDrvUser.create
        A response is read from the device.
    asynSoOctetWrite,asynWfOctetWrite
        INP contains <drvUser> which is passed to asynDrvUser.create
        VAL is sent
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsMutex.h>
#include <epicsString.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <dbScan.h>
#include <callback.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <waveformRecord.h>
#include <menuFtype.h>
#include <recSup.h>
#include <devSup.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynOctet.h"
#include "asynEpicsUtils.h"
#include <epicsExport.h>

typedef struct devPvt{
    dbCommon    *precord;
    asynUser    *pasynUser;
    char        *portName;
    int         addr;
    asynOctet   *poctet;
    void        *octetPvt;
    int         canBlock;
    char        *userParam;
    /*Following are for CmdResponse */
    char        *buffer;
    size_t      bufSize;
    size_t      bufLen;
    /* Following for writeRead */
    DBADDR      dbAddr;
    /* Following are for I/O Intr*/
    CALLBACK    callback;
    IOSCANPVT   ioScanPvt;
    void        *registrarPvt;
    int         gotValue; /*For interruptCallback*/
    interruptCallbackOctet asynCallback;
}devPvt;

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback);
static long initWfCommon(waveformRecord *pwf);
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void interruptCallbackSi(void *drvPvt, asynUser *pasynUser,
       char *data,size_t numchars, int eomReason);
static void interruptCallbackWaveform(void *drvPvt, asynUser *pasynUser,
       char *data,size_t numchars, int eomReason);
static void initDrvUser(devPvt *pdevPvt);
static void initCmdBuffer(devPvt *pdevPvt);
static void initDbAddr(devPvt *pdevPvt);
static asynStatus writeIt(asynUser *pasynUser,
        const char *message,size_t nbytes);
static asynStatus readIt(asynUser *pasynUser,char *message,
        size_t maxBytes, size_t *nBytesRead);
static long processCommon(dbCommon *precord);
static void finish(dbCommon *precord);

static long initSiCmdResponse(stringinRecord *psi);
static void callbackSiCmdResponse(asynUser *pasynUser);
static long initSiWriteRead(stringinRecord *psi);
static void callbackSiWriteRead(asynUser *pasynUser);
static long initSiRead(stringinRecord *psi);
static void callbackSiRead(asynUser *pasynUser);
static long initSoWrite(stringoutRecord *pso);
static void callbackSoWrite(asynUser *pasynUser);

static long initWfCmdResponse(waveformRecord *pwf);
static void callbackWfCmdResponse(asynUser *pasynUser);
static long initWfWriteRead(waveformRecord *pwf);
static void callbackWfWriteRead(asynUser *pasynUser);
static long initWfRead(waveformRecord *pwf);
static void callbackWfRead(asynUser *pasynUser);
static long initWfWrite(waveformRecord *pwf);
static void callbackWfWrite(asynUser *pasynUser);

typedef struct commonDset {
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;
} commonDset;

commonDset asynSiOctetCmdResponse = {
    5,0,0,initSiCmdResponse,0,            processCommon};
commonDset asynSiOctetWriteRead   = {
    5,0,0,initSiWriteRead,  0            ,processCommon};
commonDset asynSiOctetRead        = {
    5,0,0,initSiRead,       getIoIntInfo,processCommon};
commonDset asynSoOctetWrite       = {
    5,0,0,initSoWrite,      0,           processCommon};
commonDset asynWfOctetCmdResponse = {
    5,0,0,initWfCmdResponse,0,           processCommon};
commonDset asynWfOctetWriteRead   = {
    5,0,0,initWfWriteRead,  0,           processCommon};
commonDset asynWfOctetRead        = {
    5,0,0,initWfRead,       getIoIntInfo,processCommon};
commonDset asynWfOctetWrite       = {
    5,0,0,initWfWrite,      0,           processCommon};

epicsExportAddress(dset, asynSiOctetCmdResponse);
epicsExportAddress(dset, asynSiOctetWriteRead);
epicsExportAddress(dset, asynSiOctetRead);
epicsExportAddress(dset, asynSoOctetWrite);
epicsExportAddress(dset, asynWfOctetCmdResponse);
epicsExportAddress(dset, asynWfOctetWriteRead);
epicsExportAddress(dset, asynWfOctetRead);
epicsExportAddress(dset, asynWfOctetWrite);

static long initCommon(dbCommon *precord, DBLINK *plink, userCallback callback)
{
    devPvt        *pdevPvt;
    asynStatus    status;
    asynUser      *pasynUser;
    asynInterface *pasynInterface;
    commonDset    *pdset = (commonDset *)precord->dset;
    asynOctet     *poctet;

    pdevPvt = callocMustSucceed(1,sizeof(*pdevPvt),"devAsynOctet::initCommon");
    precord->dpvt = pdevPvt;
    pdevPvt->precord = precord;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback, 0);
    pasynUser->userPvt = pdevPvt;
    pdevPvt->pasynUser = pasynUser;
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                &pdevPvt->portName, &pdevPvt->addr,&pdevPvt->userParam);
    if (status != asynSuccess) {
        printf("%s devAsynOctet::initCommon error in link %s\n",
                     precord->name, pasynUser->errorMessage);
        goto bad;
    }
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser,
        pdevPvt->portName, pdevPvt->addr);
    if (status != asynSuccess) {
        printf("%s devAsynOctet::initCommon connectDevice failed %s\n",
                     precord->name, pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s devAsynOctet::initCommon interface %s not found\n",
            precord->name,asynOctetType);
        goto bad;
    }
    pdevPvt->poctet = poctet = pasynInterface->pinterface;
    pdevPvt->octetPvt = pasynInterface->drvPvt;
    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pdevPvt->canBlock);
    if(pdset->get_ioint_info) {
        scanIoInit(&pdevPvt->ioScanPvt);
    }
    return(0);

bad:
   precord->pact=1;
   return(-1);
}

static long initWfCommon(waveformRecord *pwf)
{
    if(pwf->ftvl!=menuFtypeCHAR && pwf->ftvl!=menuFtypeUCHAR) {
       printf("%s FTVL Must be CHAR or UCHAR\n",pwf->name);
       pwf->pact = 1;
       return -1;
    } 
    if(pwf->nelm<=0) {
       printf("%s NELM must be > 0\n",pwf->name);
       pwf->pact = 1;
       return -1;
    } 
    return 0;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    devPvt *pdevPvt = (devPvt *)pr->dpvt;
    asynStatus status;

    if (cmd == 0) {
        /* Add to scan list.  Register interrupts */
        asynPrint(pdevPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynOctet::getIoIntInfo registering interrupt\n",
            pr->name);
        status = pdevPvt->poctet->registerInterruptUser(
           pdevPvt->octetPvt,pdevPvt->pasynUser,
           pdevPvt->asynCallback,pdevPvt,&pdevPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynOctet registerInterruptUser %s\n",
                   pr->name,pdevPvt->pasynUser->errorMessage);
        }
    } else {
        asynPrint(pdevPvt->pasynUser, ASYN_TRACE_FLOW,
            "%s devAsynOctet::getIoIntInfo cancelling interrupt\n",
             pr->name);
        status = pdevPvt->poctet->cancelInterruptUser(pdevPvt->octetPvt,
             pdevPvt->pasynUser,pdevPvt->registrarPvt);
        if(status!=asynSuccess) {
            printf("%s devAsynOctet cancelInterruptUser %s\n",
                   pr->name,pdevPvt->pasynUser->errorMessage);
        }
    }
    *iopvt = pdevPvt->ioScanPvt;
    return 0;
}

static void interruptCallbackSi(void *drvPvt, asynUser *pasynUser,
       char *data,size_t numchars, int eomReason)
{
    devPvt         *pdevPvt = (devPvt *)drvPvt;
    stringinRecord *psi = (stringinRecord *)pdevPvt->precord;
    int            num;
    
    pdevPvt->gotValue = 1; 
    num = (numchars>=MAX_STRING_SIZE ? MAX_STRING_SIZE : numchars);
    if(num>0) {
        strncpy(psi->val,data,num);
        psi->udf = 0;
        if(num<MAX_STRING_SIZE) psi->val[num] = 0;
    }
    scanIoRequest(pdevPvt->ioScanPvt);
}

static void interruptCallbackWaveform(void *drvPvt, asynUser *pasynUser,
       char *data,size_t numchars, int eomReason)
{
    devPvt         *pdevPvt = (devPvt *)drvPvt;
    waveformRecord *pwf = (waveformRecord *)pdevPvt->precord;
    int            num;
    
    pdevPvt->gotValue = 1; 
    num = (numchars>=pwf->nelm ? pwf->nelm : numchars);
    if(num>0) {
        char *pbuf = (char *)pwf->bptr;
        memcpy(pbuf,data,num);
        if(num<pwf->nelm) pbuf[num] = 0;
        pwf->nord = num;
        pwf->udf = 0;
    }
    scanIoRequest(pdevPvt->ioScanPvt);
}

static void initDrvUser(devPvt *pdevPvt)
{
    asynUser      *pasynUser = pdevPvt->pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;
    dbCommon      *precord = pdevPvt->precord;

    /*call drvUserCreate*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
    if(pasynInterface && pdevPvt->userParam) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;

        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,pdevPvt->userParam,0,0);
        if(status!=asynSuccess) {
            printf("%s devAsynOctet drvUserCreate failed %s\n",
                     precord->name, pasynUser->errorMessage);
        }
    }
}

static void initCmdBuffer(devPvt *pdevPvt)
{
    int       len;
    dbCommon *precord = pdevPvt->precord;

    len = strlen(pdevPvt->userParam);
    if(len<=0) {
        printf("%s  no userParam\n",precord->name);
        precord->pact = 1;
        return;
    }
    pdevPvt->buffer = callocMustSucceed(len,sizeof(char),"devAsynOctet");
    dbTranslateEscape(pdevPvt->buffer,pdevPvt->userParam);
    pdevPvt->bufSize = len;
    pdevPvt->bufLen = strlen(pdevPvt->buffer);
}

static void initDbAddr(devPvt *pdevPvt)
{
    char      *userParam;
    dbCommon *precord = pdevPvt->precord;

    userParam = pdevPvt->userParam;
    if(dbNameToAddr(userParam,&pdevPvt->dbAddr)) {
        printf("%s devAsynOctet:initDbAddr record %s not present\n",
            precord->name,userParam);
        precord->pact = 1;
    }
}

static asynStatus writeIt(asynUser *pasynUser,const char *message,size_t nbytes)
{
    devPvt     *pdevPvt = (devPvt *)pasynUser->userPvt;
    dbCommon   *precord = pdevPvt->precord;
    asynOctet  *poctet = pdevPvt->poctet;
    void       *octetPvt = pdevPvt->octetPvt;
    asynStatus status;
    size_t     nbytesTransfered;

    status = poctet->write(octetPvt,pasynUser,message,nbytes,&nbytesTransfered);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devAsynOctet: writeIt failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
        return status;
    }
    if(nbytes != nbytesTransfered) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devAsynOctet: writeIt requested %d but sent %d bytes\n",
            precord->name,nbytes,nbytesTransfered);
        recGblSetSevr(precord, WRITE_ALARM, MINOR_ALARM);
        return asynError;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nbytes,
       "%s devAsynOctet: writeIt\n",precord->name);
    return status;
}

static asynStatus readIt(asynUser *pasynUser,char *message,
        size_t maxBytes, size_t *nBytesRead)
{
    devPvt     *pdevPvt = (devPvt *)pasynUser->userPvt;
    dbCommon   *precord = pdevPvt->precord;
    asynOctet  *poctet = pdevPvt->poctet;
    void       *octetPvt = pdevPvt->octetPvt;
    asynStatus status;
    int        eomReason;

    status = poctet->read(octetPvt,pasynUser,message,maxBytes,
        nBytesRead,&eomReason);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devAsynOctet: readIt failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord, READ_ALARM, INVALID_ALARM);
        return status;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,*nBytesRead,
       "%s devAsynOctet: readIt eomReason %d\n",precord->name,eomReason);
    return status;
}

static long processCommon(dbCommon *precord)
{
    devPvt     *pdevPvt = (devPvt *)precord->dpvt;
    asynStatus status;

    if (!pdevPvt->gotValue && precord->pact == 0) {
        if(pdevPvt->canBlock) precord->pact = 1;
        status = pasynManager->queueRequest(
           pdevPvt->pasynUser, asynQueuePriorityMedium, 0.0);
        if((status==asynSuccess) && pdevPvt->canBlock) return 0;
        if(pdevPvt->canBlock) precord->pact = 0;
        if (status != asynSuccess) {
            asynPrint(pdevPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s devAsynOctet::processCommon, error queuing request %s\n", 
                precord->name,pdevPvt->pasynUser->errorMessage);
            recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
        }
    }
    return(0);
}

static void finish(dbCommon *pr)
{
    devPvt     *pPvt = (devPvt *)pr->dpvt;

    if(pr->pact) callbackRequestProcessCallback(&pPvt->callback,pr->prio,pr);
}

static long initSiCmdResponse(stringinRecord *psi)
{
    devPvt     *pdevPvt;
    asynStatus status;

    status = initCommon((dbCommon *)psi,&psi->inp,callbackSiCmdResponse);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)psi->dpvt;
    initCmdBuffer(pdevPvt);
    return 0;
}

static void callbackSiCmdResponse(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pdevPvt->precord;
    asynStatus     status;
    size_t         len = sizeof(psi->val);
    size_t         nBytesRead;

    status = writeIt(pasynUser,pdevPvt->buffer,pdevPvt->bufLen);
    if(status==asynSuccess) {
        status = readIt(pasynUser,psi->val,len,&nBytesRead);
        if(status==asynSuccess) {
            psi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            psi->val[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)psi);
}

static long initSiWriteRead(stringinRecord *psi)
{
    asynStatus status;
    devPvt     *pdevPvt;

    status = initCommon((dbCommon *)psi,&psi->inp,callbackSiWriteRead);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)psi->dpvt;
    initDbAddr(pdevPvt);
    return 0;
}

static void callbackSiWriteRead(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pdevPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE];
    char           translate[MAX_STRING_SIZE];
    size_t         len = sizeof(psi->val);

    dbStatus = dbGet(&pdevPvt->dbAddr,DBR_STRING,raw,0,0,0);
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s dbGet failed\n",psi->name);
        recGblSetSevr(psi,READ_ALARM,INVALID_ALARM);
        finish((dbCommon *)psi);
        return;
    }
    dbTranslateEscape(translate,raw);
    status = writeIt(pasynUser,translate,strlen(translate));
    if(status==asynSuccess) {
        status = readIt(pasynUser,psi->val,len,&nBytesRead);
        if(status==asynSuccess) {
            psi->udf = 0;
            if(nBytesRead==len) nBytesRead--;
            psi->val[nBytesRead] = 0;
        }
    }
    finish((dbCommon *)psi);
}

static long initSiRead(stringinRecord *psi)
{
    asynStatus status;
    devPvt     *pdevPvt;

    status = initCommon((dbCommon *)psi,&psi->inp,callbackSiRead);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)psi->dpvt;
    pdevPvt->asynCallback = interruptCallbackSi;
    initDrvUser((devPvt *)psi->dpvt);
    return 0;
}

static void callbackSiRead(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *psi = (stringinRecord *)pdevPvt->precord;
    size_t         nBytesRead;
    asynStatus     status;
    size_t         len = sizeof(psi->val);

    status = readIt(pasynUser,psi->val,len,&nBytesRead);
    if(status==asynSuccess) {
        psi->udf = 0;
        if(nBytesRead==len) nBytesRead--;
        psi->val[nBytesRead] = 0;
    }
    finish((dbCommon *)psi);
}

static long initSoWrite(stringoutRecord *pso)
{
    asynStatus status;
    devPvt     *pdevPvt;

    status = initCommon((dbCommon *)pso,&pso->out,callbackSoWrite);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)pso->dpvt;
    initDrvUser((devPvt *)pso->dpvt);
    return 0;
}

static void callbackSoWrite(asynUser *pasynUser)
{
    devPvt          *pdevPvt = (devPvt *)pasynUser->userPvt;
    stringoutRecord *pso = (stringoutRecord *)pdevPvt->precord;
    asynStatus      status;

    status = writeIt(pasynUser,pso->val,strlen(pso->val));
    finish((dbCommon *)pso);
}

static long initWfCmdResponse(waveformRecord *pwf)
{
    devPvt     *pdevPvt;
    asynStatus status;

    if(initWfCommon(pwf)) return 0;
    status = initCommon((dbCommon *)pwf,&pwf->inp,callbackWfCmdResponse);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)pwf->dpvt;
    initCmdBuffer(pdevPvt);
    return 0;
}

static void callbackWfCmdResponse(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pdevPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;

    status = writeIt(pasynUser,pdevPvt->buffer,pdevPvt->bufLen);
    if(status==asynSuccess) {
        status = readIt(pasynUser,pwf->bptr,(size_t)pwf->nelm,&nBytesRead);
        if(status==asynSuccess) pwf->nord = nBytesRead;
    }
    finish((dbCommon *)pwf);
}

static long initWfWriteRead(waveformRecord *pwf)
{
    asynStatus status;
    devPvt     *pdevPvt;

    if(initWfCommon(pwf)) return 0;
    status = initCommon((dbCommon *)pwf,&pwf->inp,callbackWfWriteRead);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)pwf->dpvt;
    initDbAddr(pdevPvt);
    return 0;
}

static void callbackWfWriteRead(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pdevPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE];
    char           translate[MAX_STRING_SIZE];

    dbStatus = dbGet(&pdevPvt->dbAddr,DBR_STRING,raw,0,0,0);
    if(dbStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s dbGet failed\n",pwf->name);
        recGblSetSevr(pwf,READ_ALARM,INVALID_ALARM);
        finish((dbCommon *)pwf);
        return;
    }
    dbTranslateEscape(translate,raw);
    status = writeIt(pasynUser,translate,strlen(translate));
    if(status==asynSuccess) {
        status = readIt(pasynUser,pwf->bptr,(size_t)pwf->nelm,&nBytesRead);
        if(status==asynSuccess) pwf->nord = nBytesRead;
    }
    finish((dbCommon *)pwf);
}

static long initWfRead(waveformRecord *pwf)
{
    asynStatus status;
    devPvt     *pdevPvt;

    if(initWfCommon(pwf)) return 0;
    status = initCommon((dbCommon *)pwf,&pwf->inp,callbackWfRead);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)pwf->dpvt;
    pdevPvt->asynCallback = interruptCallbackWaveform;
    initDrvUser((devPvt *)pwf->dpvt);
    return 0;
}

static void callbackWfRead(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord *pwf = (waveformRecord *)pdevPvt->precord;
    size_t         nBytesRead;
    asynStatus     status;

    status = readIt(pasynUser,pwf->bptr,pwf->nelm,&nBytesRead);
    if(status==asynSuccess) pwf->nord = nBytesRead;
    finish((dbCommon *)pwf);
}

static long initWfWrite(waveformRecord *pwf)
{
    asynStatus status;
    devPvt     *pdevPvt;

    if(initWfCommon(pwf)) return 0;
    status = initCommon((dbCommon *)pwf,&pwf->inp,callbackWfWrite);
    if(status!=asynSuccess) return 0;
    pdevPvt = (devPvt *)pwf->dpvt;
    initDrvUser((devPvt *)pwf->dpvt);
    return 0;
}

static void callbackWfWrite(asynUser *pasynUser)
{
    devPvt          *pdevPvt = (devPvt *)pasynUser->userPvt;
    waveformRecord  *pwf = (waveformRecord *)pdevPvt->precord;
    asynStatus      status;

    status = writeIt(pasynUser,pwf->bptr,pwf->nord);
    finish((dbCommon *)pwf);
}
