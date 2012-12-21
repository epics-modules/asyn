/* devTestBlock.c */
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
    22MAR2005
    This is a test for asynManager:blockProcessCallback
*/
/* NOTE: This code does the following:
*    blockProcessCallback()
*    delay
*    unblockProcessCallback()
*    write()
*    blockProcessCallback()
*    delay
*    unblockProcessCallback()
*    read()
*
* A normal application should
*
*    blockProcessCallback()
*    write()
*    delay
*    read()
*    unblockProcessCallback()
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
#include <recSup.h>
#include <devSup.h>

#include "asynDriver.h"
#include "asynDrvUser.h"
#include "asynOctet.h"
#include "asynEpicsUtils.h"

#include <epicsExport.h>
typedef enum {stateIdle,stateWrite} processState;
typedef struct devPvt{
    dbCommon    *precord;
    asynUser    *pasynUser;
    char        *portName;
    int         addr;
    int         blockAll;
    double      queueDelay;
    asynOctet   *poctet;
    void        *octetPvt;
    CALLBACK    processCallback;
    /* Following for writeRead */
    CALLBACK    callback;
    processState state;
    DBADDR      dbAddr;
}devPvt;

static asynStatus queueIt(devPvt *pdevPvt);
static void queueItDelayed(CALLBACK * pvt);
static asynStatus writeIt(asynUser *pasynUser,
        const char *message,size_t nbytes);
static asynStatus readIt(asynUser *pasynUser,char *message,
        size_t maxBytes, size_t *nBytesRead);
static long processCommon(dbCommon *precord);

static long initSiWriteRead(stringinRecord *precord);
static void callbackSiWriteRead(asynUser *pasynUser);

typedef struct commonDset {
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     processCommon;
} commonDset;

commonDset devTestBlockInp   = {
    5,0,0,initSiWriteRead,  0            ,processCommon};

epicsExportAddress(dset, devTestBlockInp);

static long initSiWriteRead(stringinRecord *precord)
{
    DBLINK        *plink = &precord->inp;
    devPvt        *pdevPvt;
    asynStatus    status;
    asynUser      *pasynUser;
    asynInterface *pasynInterface;
    asynOctet     *poctet;
    char          *userParam = 0;

    pdevPvt = callocMustSucceed(1,sizeof(*pdevPvt),"devTestBlock::initCommon");
    precord->dpvt = pdevPvt;
    pdevPvt->precord = (dbCommon *)precord;
    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callbackSiWriteRead, 0);
    pasynUser->userPvt = pdevPvt;
    pdevPvt->pasynUser = pasynUser;
    status = pasynEpicsUtils->parseLink(pasynUser, plink, 
                &pdevPvt->portName, &pdevPvt->addr,&userParam);
    if (status != asynSuccess) {
        printf("%s devTestBlock::initCommon error in link %s\n",
                     precord->name, pasynUser->errorMessage);
        goto bad;
    }
    /* see if initial VAL is "blockAll" */
    if(strcmp(precord->val,"blockAll")==0) pdevPvt->blockAll = 1;
    /* let queueDelay just be .1*/
    pdevPvt->queueDelay = .1;
    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser,
        pdevPvt->portName, pdevPvt->addr);
    if (status != asynSuccess) {
        printf("%s devTestBlock::initCommon connectDevice failed %s\n",
                     precord->name, pasynUser->errorMessage);
        goto bad;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s devTestBlock::initCommon interface %s not found\n",
            precord->name,asynOctetType);
        goto bad;
    }
    pdevPvt->poctet = poctet = pasynInterface->pinterface;
    pdevPvt->octetPvt = pasynInterface->drvPvt;
    callbackSetCallback(queueItDelayed,&pdevPvt->callback);
    callbackSetUser(pdevPvt,&pdevPvt->callback);
    if(dbNameToAddr(userParam,&pdevPvt->dbAddr)) {
        printf("%s devTestBlock:initDbAddr record %s not present\n",
            precord->name,userParam);
        precord->pact = 1;
    }
    return(0);

bad:
   precord->pact=1;
   return(-1);
}

static long processCommon(dbCommon *precord)
{
    devPvt     *pdevPvt = (devPvt *)precord->dpvt;
    asynStatus status;

    if (precord->pact == 0) {
        precord->pact = 1;
        status = queueIt(pdevPvt);
        if(status==asynSuccess) return 0;
        precord->pact = 0;
    } 
    return(0);
}

static asynStatus queueIt(devPvt *pdevPvt)
{
    asynStatus status;
    dbCommon   *precord = pdevPvt->precord;
    asynUser   *pasynUser = pdevPvt->pasynUser;

    status = pasynManager->blockProcessCallback(pasynUser,pdevPvt->blockAll);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s blockProcessCallback failed %s\n",
            precord->name,pasynUser->errorMessage);
        return status;
    }
    callbackRequestDelayed(&pdevPvt->callback,pdevPvt->queueDelay);
    return status;
}

static void queueItDelayed(CALLBACK * pvt)
{
    devPvt     *pdevPvt;
    asynStatus status;
    dbCommon   *precord;
    asynUser   *pasynUser;

    callbackGetUser(pdevPvt,pvt);
    precord = pdevPvt->precord;
    pasynUser = pdevPvt->pasynUser;
    precord = pdevPvt->precord;
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityMedium,0.0);
    if(status!=asynSuccess) {
        asynPrint(pdevPvt->pasynUser, ASYN_TRACE_ERROR,
            "%s queueRequest failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
        status = pasynManager->unblockProcessCallback(pasynUser,pdevPvt->blockAll);
        if(status!=asynSuccess) {
            asynPrint(pdevPvt->pasynUser, ASYN_TRACE_ERROR,
                "%s queueRequest failed %s\n",
                precord->name,pasynUser->errorMessage);
        }
        callbackRequestProcessCallback(
            &pdevPvt->processCallback,precord->prio,precord);
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
            "%s devTestBlock: writeIt failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
        return status;
    }
    if(nbytes != nbytesTransfered) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s devTestBlock: writeIt requested %lu but sent %lu bytes\n",
            precord->name,(unsigned long)nbytes,(unsigned long)nbytesTransfered);
        recGblSetSevr(precord, WRITE_ALARM, MINOR_ALARM);
        return asynError;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,nbytes,
       "%s devTestBlock: writeIt\n",precord->name);
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
            "%s devTestBlock: readIt failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord, READ_ALARM, INVALID_ALARM);
        return status;
    }
    asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,message,*nBytesRead,
       "%s devTestBlock: readIt eomReason %d\n",precord->name,eomReason);
    return status;
}

static void callbackSiWriteRead(asynUser *pasynUser)
{
    devPvt         *pdevPvt = (devPvt *)pasynUser->userPvt;
    stringinRecord *precord = (stringinRecord *)pdevPvt->precord;
    asynStatus     status;
    size_t         nBytesRead;
    long           dbStatus;
    char           raw[MAX_STRING_SIZE];
    char           translate[MAX_STRING_SIZE];
    size_t         len = sizeof(precord->val);

    status = pasynManager->unblockProcessCallback(pasynUser,pdevPvt->blockAll);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pasynManager:unblockProcessCallback failed %s\n",
            precord->name,pasynUser->errorMessage);
        recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
        goto done;
    }
    switch(pdevPvt->state) {
    case stateIdle:
        dbStatus = dbGet(&pdevPvt->dbAddr,DBR_STRING,raw,0,0,0);
        if(dbStatus) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s dbGet failed\n",precord->name);
            recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
            goto done;
        }
        dbTranslateEscape(translate,raw);
        status = writeIt(pasynUser,translate,strlen(translate));
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s asynOctet:write failed %s\n",
                precord->name,pasynUser->errorMessage);
            recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
            goto done;
        }
        pdevPvt->state = stateWrite;
        status = queueIt(pdevPvt);
        if(status!=asynSuccess) goto done;
        return;
    case stateWrite:
        status = readIt(pasynUser,precord->val,len,&nBytesRead);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s asynOctet:write failed %s\n",
                precord->name,pasynUser->errorMessage);
            recGblSetSevr(precord,READ_ALARM,INVALID_ALARM);
            goto done;
        }
        if(status==asynSuccess) {
            if(nBytesRead==len) nBytesRead--;
            precord->val[nBytesRead] = 0;
        }
        pdevPvt->state = stateIdle;
        break; /*all done*/
    }
done:
    pdevPvt->state = stateIdle;
    callbackRequestProcessCallback(
        &pdevPvt->processCallback,precord->prio,precord);
}
