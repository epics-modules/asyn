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
#include <epicsStdio.h>
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
#include <biRecord.h>
#include <boRecord.h>
#include <longoutRecord.h>
#include <dbCommon.h>
#include <stringoutRecord.h>
#include <registryFunction.h>
#include <epicsExport.h>

#include <asynDriver.h>

typedef enum {
    stateNoPort,stateInit,stateConnected
}devAsynState;

typedef struct dpvtAsyn {
    CALLBACK     callback; /*for connect*/
    asynUser     *pasynUser;
    char         *portName;
    int          addr;
    epicsMutexId lock;
    devAsynState state;
    int          connectActive;
    int          traceMask;
    int          traceIOMask;
    /* Following for asynBiState */
    biRecord     *pconnectState;
    biRecord     *penableState;
    biRecord     *pautoConnectState;
    /* Following for asynBoState */
    boRecord     *pconnect;
    asynCommon   *pasynCommon;
    void         *commonPvt;
}dpvtAsyn;
    
typedef struct dpvtCommon{
    dpvtAsyn *pdpvtAsyn;
    dbAddr   dbaddr;
}dpvtCommon;

static long initCommon(dbCommon *pdbCommon,struct link *plink);
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

static long initConnect();
static long writeConnect();
static long initConnected();
static long readConnected();

static long initBiState();
static long readState();
static long initBoState();
static long writeState();

static long initTrace();
static long writeTrace();
static long initTraceIO();
static long writeTraceIO();
static long initTraceIOTruncateSize();
static long writeTraceIOTruncateSize();
static long initTraceFile();
static long writeTraceFile();

dsetAsyn devAsynConnectG = {5,0,0,initConnect,0,writeConnect};
epicsExportAddress(dset,devAsynConnectG);

dsetAsyn devAsynConnectedG = {5,0,0,initConnected,0,readConnected};
epicsExportAddress(dset,devAsynConnectedG);

dsetAsyn devAsynBiStateG = {5,0,0,initBiState,0,readState};
epicsExportAddress(dset,devAsynBiStateG);

dsetAsyn devAsynBoStateG = {5,0,0,initBoState,0,writeState};
epicsExportAddress(dset,devAsynBoStateG);

dsetAsyn devAsynTraceG = {5,0,0,initTrace,0,writeTrace};
epicsExportAddress(dset,devAsynTraceG);

dsetAsyn devAsynTraceIOG = {5,0,0,initTraceIO,0,writeTraceIO};
epicsExportAddress(dset,devAsynTraceIOG);

dsetAsyn devAsynTraceIOTruncateSizeG =
{5,0,0,initTraceIOTruncateSize,0,writeTraceIOTruncateSize};
epicsExportAddress(dset,devAsynTraceIOTruncateSizeG);

dsetAsyn devAsynTraceFileG = {5,0,0,initTraceFile,0,writeTraceFile};
epicsExportAddress(dset,devAsynTraceFileG);

static long initCommon(dbCommon *pdbCommon,struct link *plink)
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

/*callback to issue disconnect/connect to low level driver*/
static void connect(asynUser *pasynUser)
{
    stringoutRecord *pso = (stringoutRecord *)pasynUser->userPvt;
    dpvtAsyn        *pdpvtAsyn = (dpvtAsyn *)pso->dpvt;
    boRecord        *pbo = (boRecord *)pdpvtAsyn->pconnect;
    dbCommon        *precord = (dbCommon *)pbo;
    asynCommon      *pasynCommon = pdpvtAsyn->pasynCommon;
    void            *drvPvt = pdpvtAsyn->commonPvt;
    asynStatus      status = asynSuccess;
    int             val;

    assert(pbo);
    assert(pasynCommon);
    assert(drvPvt);
    val = pbo->val;
    if(val==0) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect %d\n",
            pbo->name,(int)pbo->val);
        status = pasynCommon->disconnect(drvPvt,pasynUser);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connect %d\n",
            pbo->name,(int)pbo->val);
        status = pasynCommon->connect(drvPvt,pasynUser);
    }
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            pbo->name,pasynUser->errorMessage);
        dbScanLock(precord);
        recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
        dbScanUnlock(precord);
    }
    callbackRequestProcessCallback(&pdpvtAsyn->callback,pbo->prio,(void *)pbo);
}

/* exception callback for asynBiStatus */
static void exception(asynUser *pasynUser,asynException exception)
{
    stringoutRecord  *pso = (stringoutRecord *)pasynUser->userPvt;
    dpvtAsyn         *pdpvtAsyn = (dpvtAsyn *)pso->dpvt;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s exception %d\n",
        pso->name,(int)exception);
    epicsMutexMustLock(pdpvtAsyn->lock);
    switch(exception) {
    case asynExceptionConnect:
        if(pdpvtAsyn->pconnectState)
            scanOnce((dbCommon *)pdpvtAsyn->pconnectState); break;
    case asynExceptionEnable:
        if(pdpvtAsyn->penableState)
            scanOnce((dbCommon *)pdpvtAsyn->penableState); break;
    case asynExceptionAutoConnect:
        if(pdpvtAsyn->pautoConnectState)
            scanOnce((dbCommon *)pdpvtAsyn->pautoConnectState); break;
    default:
        break;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
}

static long initConnect(stringoutRecord *pso)
{
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;

    pdpvtAsyn = (dpvtAsyn *)callocMustSucceed(
        1,sizeof(dpvtAsyn),"asynConnectDeviceInit");
    pdpvtAsyn->lock = epicsMutexMustCreate();
    pdpvtAsyn->state = stateNoPort;
    pasynUser = pasynManager->createAsynUser(connect,0);
    pasynUser->userPvt = pso;
    pdpvtAsyn->pasynUser = pasynUser;
    pso->dpvt = pdpvtAsyn;
    return 0;
}

static long writeConnect(stringoutRecord *pso)
{
    dpvtAsyn      *pdpvtAsyn = (dpvtAsyn *)pso->dpvt;
    int           addr = 0;
    int           lenportName = 0;
    asynUser      *pasynUser = 0;
    char          *separator;
    char          *portName;
    asynStatus    status = asynSuccess;
    asynInterface *pasynInterface;
    char          buffer[100];
    
    if(!pdpvtAsyn) {
        recGblSetSevr(pso,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    pasynUser = pdpvtAsyn->pasynUser;
    epicsMutexMustLock(pdpvtAsyn->lock);
    if(pdpvtAsyn->state!=stateNoPort) {
        pdpvtAsyn->state = stateNoPort;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s exceptionCallbackRemove\n",
            pso->name);
        status = pasynManager->exceptionCallbackRemove(pasynUser);
        if(status==asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s disconnect\n",
                pso->name);
            status = pasynManager->disconnect(pasynUser);
        }
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
                pso->name,pasynUser->errorMessage);
            recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
        }
        free(pdpvtAsyn->portName);
        pdpvtAsyn->portName = 0;
        pdpvtAsyn->commonPvt = 0;
        pdpvtAsyn->pasynCommon = 0;
    }
    strncpy(buffer,pso->val,sizeof(buffer));
    buffer[sizeof(buffer) -1] = 0;
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
       printf("%s can't determine port,addr\n",pso->name);
       goto bad;
    }
    portName = (char *)callocMustSucceed(lenportName,sizeof(char),"devAsyn");
    strcpy(portName,buffer);
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connectDevice\n",pso->name);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynManager error %s\n",pso->name,pasynUser->errorMessage);
        goto bad;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s findInterface %s\n",
        pso->name,asynCommonType);
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            pso->name,pasynUser->errorMessage);
        goto disconnect;
    }
    pdpvtAsyn->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pdpvtAsyn->commonPvt = pasynInterface->drvPvt;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s exceptionCallbackAdd\n",pso->name);
    status = pasynManager->exceptionCallbackAdd(pasynUser,exception);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            pso->name,pasynUser->errorMessage);
        goto disconnect;
    }
    pdpvtAsyn->portName = portName;
    pdpvtAsyn->addr = addr;
    pdpvtAsyn->state = stateInit;
    epicsMutexUnlock(pdpvtAsyn->lock);
    return(0);
disconnect:
    pasynManager->disconnect(pasynUser);
    free(portName);
bad:
    epicsMutexUnlock(pdpvtAsyn->lock);
    recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initConnected(biRecord *pbi)
{ return initCommon((dbCommon *)pbi,&pbi->inp); }

static long readConnected(biRecord *pbi)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbi->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbi->name);
        goto bad;
    }
    if(pdpvtAsyn->state==stateInit) {
        pdpvtAsyn->state = stateConnected;
        pbi->val = pbi->rval = 1;
        pbi->udf = 0;
    } else if(pdpvtAsyn->state==stateNoPort){
        recGblSetSevr(pbi,WRITE_ALARM,MINOR_ALARM);
        pbi->val = pbi->rval = 0;
        pbi->udf = 0;
    }
    return(0);
bad:
    recGblSetSevr(pbi,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initBiState(biRecord *pbi)
{ return initCommon((dbCommon *)pbi,&pbi->inp); }

static long readState(biRecord *pbi)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbi->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    int        yesNo;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbi->name);
        goto bad;
    }
    pasynUser = pdpvtAsyn->pasynUser;
    epicsMutexMustLock(pdpvtAsyn->lock);
    switch(pdpvtAsyn->state) {
    case stateInit:
        switch(pbi->mask) {
            case 0x1: pdpvtAsyn->pconnectState = pbi; break;
            case 0x2: pdpvtAsyn->penableState = pbi; break;
            case 0x4: pdpvtAsyn->pautoConnectState = pbi; break;
            default:
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "%s illegal mask. Must be 0x1, 0x2, or 0x4\n",pbi->name);
                pbi->pact = 1;
                return 0;
        }
        /* no break on purpose */
    case stateConnected:
        switch(pbi->mask) {
        case 0x1:
            yesNo = pasynManager->isConnected(pasynUser);
            if(yesNo<0) goto unlock;
            break;
        case 0x2:
            yesNo = pasynManager->isEnabled(pasynUser);
            if(yesNo<0) goto unlock;
            break;
        case 0x4:
            yesNo = pasynManager->isAutoConnect(pasynUser);
            if(yesNo<0) goto unlock;
            break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "illegal asynException\n");
            goto unlock;
        }
        pbi->val = pbi->rval = yesNo;
        pbi->udf = 0;
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s state doesn't allow enable. Try again later.\n",pbi->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    epicsMutexUnlock(pdpvtAsyn->lock);
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s failed %s\n",pbi->name,pasynUser->errorMessage);
bad:
    recGblSetSevr(pbi,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initBoState(boRecord *pbo)
{ return initCommon((dbCommon *)pbo,&pbo->out); }

static long writeState(boRecord *pbo)
{
    dpvtCommon *pdpvtCommon = (dpvtCommon *)pbo->dpvt;
    dpvtAsyn   *pdpvtAsyn = dpvtAsynFind(pdpvtCommon);
    asynUser   *pasynUser;
    asynStatus status = asynSuccess;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    epicsMutexMustLock(pdpvtAsyn->lock);
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->state) {
    case stateInit:
        switch(pbo->mask) {
        case 0x1:
            pdpvtAsyn->pconnect = pbo;
            pbo->val = pasynManager->isConnected(pasynUser);
            break;
        case 0x2:
            pbo->val = pasynManager->isEnabled(pasynUser);
            break;
        case 0x4:
            pbo->val = pasynManager->isAutoConnect(pasynUser);
            break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s illegal mask. Must be 0x1, 0x2, or 0x4\n",pbo->name);
            goto unlock;
        }
        pbo->udf = 0;
        break;
    case stateConnected:
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s %d\n", pbo->name,(int)pbo->val);
        pbo->val = (pbo->val ? 1 : 0); /*make sure val is yesNo*/
        switch(pbo->mask) {
        case 0x1:
            if(pbo->pact) break;
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n", pbo->name);
            status = pasynManager->queueRequest(pasynUser,
                asynQueuePriorityConnect,0.0);
            if(status==asynSuccess) pbo->pact = 1;
            break;
        case 0x2:
            status = pasynManager->enable(pasynUser,(int)pbo->val);
            break;
        case 0x4:
            pbo->val = pasynManager->autoConnect(pasynUser,(int)pbo->val);
            break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "%s illegal mask. Must be 0x1, 0x2, or 0x4\n",pbo->name);
            goto unlock;
        }
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s error %s\n",pbo->name,pasynUser->errorMessage);
        } 
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s state doesn't allow action. Try again later.\n",pbo->name);
        goto unlock;
    }
    epicsMutexUnlock(pdpvtAsyn->lock);
    return 0;
unlock:
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s error %s\n",pbo->name,pasynUser->errorMessage);
    epicsMutexUnlock(pdpvtAsyn->lock);
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initTrace(boRecord *pbo)
{ return initCommon((dbCommon *)pbo,&pbo->out); }

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
{ return initCommon((dbCommon *)pbo,&pbo->out); }

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
{ return initCommon((dbCommon *)plongout,&plongout->out); }

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
{ return initCommon((dbCommon *)pstringout,&pstringout->out); }

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
