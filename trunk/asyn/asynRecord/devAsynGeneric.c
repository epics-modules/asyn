/* devAsynGeneric.c */
/*
 *      Author: Marty Kraimer
 *      Date:   02DEC2003
 */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cantProceed.h>
#include <epicsAssert.h>
#include <link.h>
#include <epicsMutex.h>
#include <alarm.h>
#include <callback.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <link.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <drvSup.h>
#include <boRecord.h>
#include <longoutRecord.h>
#include <dbCommon.h>
#include <subRecord.h>
#include <stringoutRecord.h>
#include <registryFunction.h>
#include <epicsExport.h>

#include <asynDriver.h>

typedef enum {
    stateNoPort,stateInit,stateConnected,stateException
}devAsynState;

typedef struct dpvtAsyn {
    asynUser     *pasynUser;
    char         *portName;
    int          addr;
    epicsMutexId lock;
    devAsynState state;
    int          connectActive;
    int          connected;
    int          enabled;
    int          autoConnect;
    int          traceMask;
    int          traceIOMask;
    /*Following for exclusive use of asynConnectDeviceInit/asynConnectDevice*/
    dbAddr       dbaddr; /* of PortAddr stringout record*/
}dpvtAsyn;
    
typedef struct dpvtCommon{
    dpvtAsyn *pdpvtAsyn;
    dbAddr   dbaddr;
}dpvtCommon;

typedef struct dpvtConnect{
    CALLBACK     callback;
    asynUser     *pasynUser;
    asynCommon   *pasynCommon;
    void         *commonPvt;
    dpvtAsyn     *pdpvtAsyn;
    dbAddr       dbaddr;
}dpvtConnect;

static long initCommon(dbCommon *pdbCommon,struct link *plink,
    userCallback queue);
static long asynConnectDeviceInit(subRecord *psub,void *unused);
static long asynConnectDevice(subRecord *psub);
static long asynConnectedInit(subRecord *psub,void *unused);
static long asynConnected(subRecord *psub);
static dpvtAsyn *dpvtAsynFind(dpvtCommon *pdpvtCommon);

/* Create the dset for devAsynTrace */
typedef struct dsetAsyn{
	long      number;
	DEVSUPFUN report;
	DEVSUPFUN init;
	DEVSUPFUN init_record;
	DEVSUPFUN get_ioint_info;
	DEVSUPFUN io;
}dsetAsyn;

static long initEnable();
static long writeEnable();
static long initAutoConnect();
static long writeAutoConnect();
static long initConnect();
static long writeConnect();

static long initTrace();
static long writeTrace();
static long initTraceIO();
static long writeTraceIO();
static long initTraceIOTruncateSize();
static long writeTraceIOTruncateSize();
static long initTraceFile();
static long writeTraceFile();

dsetAsyn devAsynEnableG = {5,0,0,initEnable,0,writeEnable};
epicsExportAddress(dset,devAsynEnableG);

dsetAsyn devAsynAutoConnectG = {5,0,0,initAutoConnect,0,writeAutoConnect};
epicsExportAddress(dset,devAsynAutoConnectG);

dsetAsyn devAsynConnectG = {5,0,0,initConnect,0,writeConnect};
epicsExportAddress(dset,devAsynConnectG);

dsetAsyn devAsynTraceG = {5,0,0,initTrace,0,writeTrace};
epicsExportAddress(dset,devAsynTraceG);

dsetAsyn devAsynTraceIOG = {5,0,0,initTraceIO,0,writeTraceIO};
epicsExportAddress(dset,devAsynTraceIOG);

dsetAsyn devAsynTraceIOTruncateSizeG =
{5,0,0,initTraceIOTruncateSize,0,writeTraceIOTruncateSize};
epicsExportAddress(dset,devAsynTraceIOTruncateSizeG);

dsetAsyn devAsynTraceFileG = {5,0,0,initTraceFile,0,writeTraceFile};
epicsExportAddress(dset,devAsynTraceFileG);

static long initCommon(dbCommon *pdbCommon,struct link *plink,
    userCallback queue)
{
    dpvtCommon *pdpvtCommon;
    char       *parm;
    dbAddr     dbaddr;
    long       status;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO\n",pdbCommon->name);
        pdbCommon->pact = 1;
        return 0;
    }
    parm = plink->value.instio.string;
    status = dbNameToAddr(parm,&dbaddr);
    if(status) {
        printf("%s dbNameToAddr failed for %s\n",pdbCommon->name,parm);
        pdbCommon->pact = 1;
        return 0;
    }
    pdpvtCommon = (dpvtCommon *)callocMustSucceed(1,sizeof(dpvtCommon),"devAsynTest");
    pdpvtCommon->dbaddr = dbaddr;
    pdbCommon->dpvt = pdpvtCommon;
    return 0;
} 

static dpvtAsyn *dpvtAsynFind(dpvtCommon *pdpvtCommon)
{
    dpvtAsyn *pdpvtAsyn = pdpvtCommon->pdpvtAsyn;

    if(!pdpvtAsyn) {
        dbCommon *pdbCommon = (dbCommon *)pdpvtCommon->dbaddr.precord;
        pdpvtCommon->pdpvtAsyn = (dpvtAsyn *)pdbCommon->dpvt;
        pdpvtAsyn = pdpvtCommon->pdpvtAsyn;
    }
    return pdpvtAsyn;
}

/*The subroutines for connecting */

static void exception(asynUser *pasynUser,asynException exception)
{
    subRecord *psub = (subRecord *)pasynUser->userPvt;
    dpvtAsyn  *pdpvtAsyn = (dpvtAsyn *)psub->dpvt;
    int       yesNo;

    epicsMutexMustLock(pdpvtAsyn->lock);
    if(pdpvtAsyn->state==stateNoPort) goto unlock;
    if(pdpvtAsyn->state==stateException) goto unlock;
    switch(exception) {
    case asynExceptionConnect:
        if(pdpvtAsyn->connectActive) goto unlock;
        yesNo = pasynManager->isConnected(pasynUser);
        if(pdpvtAsyn->connected==yesNo) goto unlock;
        break;
    case asynExceptionEnable:
        yesNo = pasynManager->isEnabled(pasynUser);
        if(pdpvtAsyn->enabled==yesNo) goto unlock;
        break;
    case asynExceptionAutoConnect:
        yesNo = pasynManager->isAutoConnect(pasynUser);
        if(pdpvtAsyn->autoConnect==yesNo) goto unlock;
        break;
    }
    pdpvtAsyn->state = stateException;
    if(!pdpvtAsyn->connectActive) scanOnce((void *)psub);
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
}

static long asynConnectDeviceInit(subRecord *psub,void *unused)
{
    dbAddr   dbaddr;
    long     status;
    dpvtAsyn *pdpvtAsyn;

    status = dbNameToAddr(psub->desc,&dbaddr);
    if(status) {
        printf("%s dbNameToAddr failed for %s\n",psub->name,psub->desc);
        psub->pact = 1;
        return 0;
    }
    pdpvtAsyn = (dpvtAsyn *)callocMustSucceed(
        1,sizeof(dpvtAsyn),"asynConnectDeviceInit");
    pdpvtAsyn->lock = epicsMutexMustCreate();
    pdpvtAsyn->state = stateNoPort;
    pdpvtAsyn->dbaddr = dbaddr;
    psub->val = 0.0;
    psub->udf = 0;
    psub->dpvt = pdpvtAsyn;
    return 0;
}

static long asynConnectDevice(subRecord *psub)
{
    dpvtAsyn    *pdpvtAsyn = (dpvtAsyn *)psub->dpvt;
    char        buffer[100];
    int         addr = 0;
    int         lenportName = 0;
    asynUser    *pasynUser = 0;
    char        *separator;
    char        *portName;
    asynStatus  status;
    long        dbstatus;
    
    if(!pdpvtAsyn) {
        recGblSetSevr(psub,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    pasynUser = pdpvtAsyn->pasynUser;
    epicsMutexMustLock(pdpvtAsyn->lock);
    if(pdpvtAsyn->state==stateException) {
        /* Just make all  linked records init */
        pdpvtAsyn->state = stateInit;
        epicsMutexUnlock(pdpvtAsyn->lock);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s stateException\n",
            psub->name);
        return 0;
    }
    pdpvtAsyn->state = stateNoPort;
    if(pdpvtAsyn->pasynUser) {
        pasynUser = pdpvtAsyn->pasynUser;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s exceptionCallbackRemove\n",
            psub->name);
        status = pasynManager->exceptionCallbackRemove(pasynUser);
        if(status==asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect\n",
                psub->name);
            status = pasynManager->disconnect(pasynUser);
        }
        if(status==asynSuccess)
            status = pasynManager->freeAsynUser(pasynUser);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                psub->name,pasynUser->errorMessage);
            recGblSetSevr(psub,WRITE_ALARM,MAJOR_ALARM);
        }
        free(pdpvtAsyn->portName);
        pdpvtAsyn->portName = 0;
        pdpvtAsyn->pasynUser = 0;
    }
    buffer[0] = 0;
    dbstatus = dbGet(&pdpvtAsyn->dbaddr,DBR_STRING,buffer,0,0,0);
    if(dbstatus) recGblRecordError(dbstatus,(void *)psub,"dbGetLinkValue");
    /*allow comma or blank to separate port and addr*/
    separator = strchr(buffer,',');
    if(!separator)  separator = strchr(buffer,' ');
    if(separator) {
        sscanf(separator+1,"%d",&addr);
        lenportName = separator - buffer;
        *separator = 0;
    } else { /*Just let addr = 0*/
        lenportName = strlen(buffer);
    }
    if(lenportName<=0) {
       printf("%s can't determine port,addr\n",psub->name);
       goto bad;
    }
    portName = (char *)callocMustSucceed(lenportName,sizeof(char),"devAsyn");
    strcpy(portName,buffer);
    pasynUser = pasynManager->createAsynUser(0,0);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connectDevice\n",psub->name);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynManager error %s\n",psub->name,pasynUser->errorMessage);
        goto freeAsynUser;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s exceptionCallbackAdd\n",psub->name);
    status = pasynManager->exceptionCallbackAdd(pasynUser,exception);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            psub->name,pasynUser->errorMessage);
        goto disconnect;
    }
    pdpvtAsyn->portName = portName;
    pdpvtAsyn->addr = addr;
    pasynUser->userPvt = psub;
    pdpvtAsyn->pasynUser = pasynUser;
    pdpvtAsyn->state = stateInit;
    epicsMutexUnlock(pdpvtAsyn->lock);
    return(0);
disconnect:
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect\n", psub->name);
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) goto printMessage;
freeAsynUser:
    pasynManager->freeAsynUser(pasynUser);
    free(portName);
printMessage:
    if(status!=asynSuccess)
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                psub->name,pasynUser->errorMessage);
bad:
    epicsMutexUnlock(pdpvtAsyn->lock);
    recGblSetSevr(psub,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long asynConnectedInit(subRecord *psub,void *unused)
{
    dbAddr   dbaddr;
    long     status;
    dpvtCommon *pdpvtCommon;

    status = dbNameToAddr(psub->desc,&dbaddr);
    if(status) {
        printf("%s dbNameToAddr failed for %s\n",psub->name,psub->desc);
        psub->pact = 1;
        return 0;
    }
    pdpvtCommon = (dpvtCommon *)callocMustSucceed(
        1,sizeof(dpvtCommon),"asynConnectDeviceInit");
    pdpvtCommon->dbaddr = dbaddr;
    psub->dpvt = pdpvtCommon;
    psub->val = 0;
    psub->udf = 0;
    return 0;
}

static long asynConnected(subRecord *psub)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)psub->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",psub->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pdpvtAsyn->state = stateConnected;
    epicsMutexUnlock(pdpvtAsyn->lock);
    return(0);
bad:
    recGblSetSevr(psub,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initEnable(boRecord *pbo)
{
    return initCommon((dbCommon *)pbo,&pbo->out,0);
}

static long writeEnable(boRecord *pbo)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbo->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        pbo->val = pasynManager->isEnabled(pasynUser);
        pdpvtAsyn->enabled = pbo->val;
        pbo->udf = 0;
        break;
    case stateConnected:
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s enable %d\n",
            pbo->name,(int)pbo->val);
        pbo->val = (pbo->val ? 1 : 0); /*make sure val is yesNo*/
        pdpvtAsyn->enabled = pbo->val;
        status = pasynManager->enable(pasynUser,(int)pbo->val);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s enable failed  %s\n",
                pbo->name,pasynUser->errorMessage);
            goto unlock;
        }
        break;
    default:
        printf("%s state doesn't allow enable. Try again later.\n",pbo->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initAutoConnect(boRecord *pbo)
{
    return initCommon((dbCommon *)pbo,&pbo->out,0);
}

static long writeAutoConnect(boRecord *pbo)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbo->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        pbo->val = pasynManager->isAutoConnect(pasynUser);
        pdpvtAsyn->autoConnect = pbo->val;
        pbo->udf = 0;
        break;
    case stateConnected:
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s autoConnect %d\n",
            pbo->name,(int)pbo->val);
        pbo->val = (pbo->val ? 1 : 0); /*make sure val is yesNo*/
        pdpvtAsyn->autoConnect = pbo->val;
        status = pasynManager->autoConnect(pasynUser,(int)pbo->val);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s autoConnect failed  %s\n",
                pbo->name,pasynUser->errorMessage);
            goto unlock;
        }
        break;
    default:
        printf("%s state doesn't allow autoConnect. Try again later.\n",pbo->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

/*callback to issue disconnect/connect to low level driver*/
static void connect(asynUser *pasynUser)
{
    boRecord    *pbo = (boRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbo;
    dpvtConnect *pdpvtConnect = (dpvtConnect *)pbo->dpvt;
    dpvtAsyn    *pdpvtAsyn = pdpvtConnect->pdpvtAsyn;
    asynCommon  *pasynCommon = pdpvtConnect->pasynCommon;
    void        *drvPvt = pdpvtConnect->commonPvt;
    asynStatus  status = asynSuccess;
    int         val;

    assert(pasynCommon);
    assert(drvPvt);
    val = pbo->val;
    if(val==0) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect %d\n",
            pbo->name,(int)pbo->val);
        status = pasynCommon->disconnect(drvPvt,pasynUser);
        if(status==asynSuccess) pdpvtAsyn->connected = 0;
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connect %d\n",
            pbo->name,(int)pbo->val);
        status = pasynCommon->connect(drvPvt,pasynUser);
        if(status==asynSuccess) pdpvtAsyn->connected = 1;
    }
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            pbo->name,pasynUser->errorMessage);
        dbScanLock(precord);
        recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
        dbScanUnlock(precord);
    }
    callbackRequestProcessCallback(&pdpvtConnect->callback,pbo->prio,(void *)pbo);
}

static long initConnect(boRecord *pbo)
{
    struct link *plink = &pbo->out;
    dpvtConnect *pdpvtConnect;
    char        *parm;
    dbAddr      dbaddr;
    long        status;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO",pbo->name);
        pbo->pact = 1;
        return -1;
    }
    parm = plink->value.instio.string;
    status = dbNameToAddr(parm,&dbaddr);
    if(status) {
        printf("%s dbNameToAddr failed\n",pbo->name);
        pbo->pact = 1;
        return 0;
    }
    pdpvtConnect = (dpvtConnect *)callocMustSucceed(
        1,sizeof(dpvtConnect),"devAsynTest");
    pdpvtConnect->dbaddr = dbaddr;
    pbo->dpvt = pdpvtConnect;
    return 0;
}

static long writeConnect(boRecord *pbo)
{
    dpvtConnect   *pdpvtConnect = (dpvtConnect *)pbo->dpvt;
    asynUser      *pasynUser = pdpvtConnect->pasynUser;
    dpvtAsyn      *pdpvtAsyn = pdpvtConnect->pdpvtAsyn;
    dbCommon      *pdbCommon;
    asynInterface *pasynInterface;
    asynStatus    status;
    devAsynState  state;
    int           isConnected;

    if(!pdpvtAsyn) {
        pdbCommon = (dbCommon *)pdpvtConnect->dbaddr.precord;
        pdpvtConnect->pdpvtAsyn = (dpvtAsyn *)pdbCommon->dpvt;
        pdpvtAsyn = pdpvtConnect->pdpvtAsyn;
    }
    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    pdbCommon = (dbCommon *)pdpvtConnect->dbaddr.precord;
    epicsMutexMustLock(pdpvtAsyn->lock);
    state = pdpvtAsyn->state;
    if(pbo->pact) {
        assert(pdpvtAsyn->connectActive);
        pdpvtAsyn->connectActive = 0;
        if(state==stateException) scanOnce((void *)pdbCommon);
        epicsMutexUnlock(pdpvtAsyn->lock);
        return 0;
    }
    if(state==stateInit && pasynUser) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect\n",pbo->name);
        status = pasynManager->disconnect(pasynUser);
        if(status==asynSuccess)
            status = pasynManager->freeAsynUser(pasynUser);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                pbo->name,pasynUser->errorMessage);
            recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
        }
        pdpvtConnect->commonPvt = 0;
        pdpvtConnect->pasynCommon = 0;
        pdpvtConnect->pasynUser = 0;
        pasynUser = 0;
    }
    if(pdpvtAsyn->state==stateNoPort) {
        printf("%s no port attached\n",pbo->name);
        goto unlock;
    }
    if(!pasynUser) {
        pasynUser = pasynManager->createAsynUser(connect,0);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connectDevice\n", pbo->name);
        status = pasynManager->connectDevice(pasynUser,
            pdpvtAsyn->portName,pdpvtAsyn->addr);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                pbo->name,pasynUser->errorMessage);
            goto freeAsynUser;
        }
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s findInterface %s\n",
            pbo->name,asynCommonType);
        pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
        if(!pasynInterface) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                pbo->name,pasynUser->errorMessage);
            goto disconnect;
        }
        pdpvtConnect->pasynCommon = (asynCommon *)pasynInterface->pinterface;
        pdpvtConnect->commonPvt = pasynInterface->drvPvt;
        pasynUser->userPvt = pbo;
        pdpvtConnect->pasynUser = pasynUser;
        pbo->val = pasynManager->isConnected(pasynUser);
        pbo->udf = 0;
    }
    isConnected = pasynManager->isConnected(pasynUser);
    if(pbo->val==isConnected) goto success;
    if(pdpvtAsyn->state==stateConnected) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n", pbo->name);
        status = pasynManager->queueRequest(pasynUser,
           asynQueuePriorityConnect,0.0);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s enable failed %s\n",
                pbo->name,pasynUser->errorMessage);
            recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
        } else {
            pdpvtAsyn->connectActive = 1;
        }
    }
success:
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
disconnect:
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect\n", pbo->name);
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) goto printMessage;
freeAsynUser:
    status = pasynManager->freeAsynUser(pasynUser);
printMessage:
    if(status!=asynSuccess)
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                pbo->name,pasynUser->errorMessage);
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initTrace(boRecord *pbo)
{
    return initCommon((dbCommon *)pbo,&pbo->out,0);
}

static long writeTrace(boRecord	*pbo)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbo->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status;
    int        traceMask;
    int        mask = pbo->mask;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        traceMask = pasynTrace->getTraceMask(pasynUser);
        if(traceMask&mask) {
            pbo->val = 1; pbo->rval = mask;
        } else {
            pbo->val = 0; pbo->rval = 0;
        }
        pdpvtAsyn->traceMask = traceMask;
        pbo->udf = 0;
        break;
    case stateConnected:
        traceMask = pasynTrace->getTraceMask(pasynUser);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s getTraceMask %d\n",
            pbo->name,traceMask);
        if(pbo->rval!=(traceMask&mask)) { /* It changed */
            traceMask = (traceMask & ~mask) | (pbo->rval & mask);
            pdpvtAsyn->traceMask = traceMask;
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s setTraceMask %d\n",
                pbo->name,traceMask);
            status = pasynTrace->setTraceMask(pasynUser,traceMask);
            if(status!=asynSuccess) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s setTraceMask failed %s\n",
                    pbo->name,pasynUser->errorMessage);
                recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
            }
        }
        break;
    default:
        printf("%s state doesn't allow setTraceMask. "
               "Try again later.\n",pbo->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initTraceIO(boRecord *pbo)
{
    return initCommon((dbCommon *)pbo,&pbo->out,0);
}

static long writeTraceIO(boRecord	*pbo)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbo->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status;
    int        traceIOMask;
    int        mask = pbo->mask;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        traceIOMask = pasynTrace->getTraceIOMask(pasynUser);
        if(traceIOMask&mask) {
            pbo->val = 1; pbo->rval = mask;
        } else {
            pbo->val = 0; pbo->rval = 0;
        }
        pdpvtAsyn->traceIOMask = traceIOMask;
        pbo->udf = 0;
        break;
    case stateConnected:
        traceIOMask = pasynTrace->getTraceIOMask(pasynUser);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s getTraceIOMask %d\n",
            pbo->name,traceIOMask);
        if(pbo->rval!=(traceIOMask&mask)) { /* It changed */
            traceIOMask = (traceIOMask & ~mask) | (pbo->rval & mask);
            pdpvtAsyn->traceIOMask = traceIOMask;
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s setTraceIOMask %d\n",
                    pbo->name,traceIOMask);
            status = pasynTrace->setTraceIOMask(pasynUser,traceIOMask);
            if(status!=asynSuccess) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s setTraceIOMask failed %s\n",
                    pbo->name,pasynUser->errorMessage);
                recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
            }
        }
        break;
    default:
        printf("%s state doesn't allow setTraceIOMask. "
               "Try again later.\n",pbo->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initTraceIOTruncateSize(longoutRecord *plongout)
{
    return initCommon((dbCommon *)plongout,&plongout->out,0);
}

static long writeTraceIOTruncateSize(longoutRecord *plongout)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)plongout->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",plongout->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        plongout->val = (long)pasynTrace->getTraceIOTruncateSize(pasynUser);
        plongout->udf = 0;
        break;
    case stateConnected:
        status = pasynTrace->setTraceIOTruncateSize(pasynUser,(int)plongout->val);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s setTraceIOTruncateSize failed %s\n",
                plongout->name,pasynUser->errorMessage);
            recGblSetSevr(plongout,WRITE_ALARM,MAJOR_ALARM);
        }
        break;
    default:
        printf("%s state doesn't allow setTraceIOTruncateSize. "
               "Try again later.\n",plongout->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(plongout,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initTraceFile(stringoutRecord *pstringout)
{
    return initCommon((dbCommon *)pstringout,&pstringout->out,0);
}

static long writeTraceFile(stringoutRecord *pstringout)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pstringout->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    FILE       *fd;
    asynStatus status;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pstringout->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        pstringout->udf = 0;
        break;
    case stateConnected:
        fd = pasynTrace->getTraceFile(pasynUser);
        if(fd) {
            if(fclose(fd)) printf("%s fclose failed\n",pstringout->name);
        }
        fd = fopen(pstringout->val,"a+");
        if(!fd) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s fopen failed\n",pstringout->name);
            recGblSetSevr(pstringout,WRITE_ALARM,MAJOR_ALARM);
            return 0;
        }
        status = pasynTrace->setTraceFile(pasynUser,fd);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s setTraceFile failed %s\n",
                pstringout->name,pasynUser->errorMessage);
            recGblSetSevr(pstringout,WRITE_ALARM,MAJOR_ALARM);
        }
        break;
    default:
        printf("%s state doesn't allow setTraceFile. "
               "Try again later.\n",pstringout->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pstringout,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static registryFunctionRef ref[] = {
    {"asynConnectDeviceInit",(REGISTRYFUNCTION)&asynConnectDeviceInit},
    {"asynConnectDevice",(REGISTRYFUNCTION)&asynConnectDevice},
    {"asynConnectedInit",(REGISTRYFUNCTION)&asynConnectedInit},
    {"asynConnected",(REGISTRYFUNCTION)&asynConnected}
};

static void asynSubRegistrar(void)
{
    registryFunctionRefAdd(ref,NELEMENTS(ref));
}
epicsExportRegistrar(asynSubRegistrar);

/* add support so that dbior generates asynDriver reports */
static long drvAsynReport(int level);
struct {
        long     number;
        DRVSUPFUN report;
        DRVSUPFUN init;
} drvAsyn={
        2,
        drvAsynReport,
        0
};
epicsExportAddress(drvet,drvAsyn);

static long drvAsynReport(int level)
{
    pasynManager->report(stdout,level);
    return(0);
}
