/* devAsyn.c */
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
#include <dbScan.h>
#include <alarm.h>
#include <callback.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <link.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <biRecord.h>
#include <boRecord.h>
#include <dbCommon.h>
#include <registryFunction.h>
#include <epicsExport.h>

#include <asynDriver.h>

typedef struct dpvtAsyn {
    CALLBACK     callback;
    char         *portName;
    int          addr;
    asynUser     *pasynUser;
    asynCommon   *pasynCommon;
    void         *commonPvt;
    asynException type;
}dpvtAsyn;

typedef struct dsetAsyn{
	long      number;
	DEVSUPFUN report;
	DEVSUPFUN init;
	DEVSUPFUN init_record;
	DEVSUPFUN get_ioint_info;
	DEVSUPFUN io;
}dsetAsyn;

static long initBiState();
static long readState();
static long initBoState();
static long writeState();
dsetAsyn devAsynBiState = {5,0,0,initBiState,0,readState};
epicsExportAddress(dset,devAsynBiState);
dsetAsyn devAsynBoState = {5,0,0,initBoState,0,writeState};
epicsExportAddress(dset,devAsynBoState);

static void exception(asynUser *pasynUser,asynException exception)
{
    biRecord    *pbi = (biRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbi;
    dpvtAsyn    *pdpvtAsyn = (dpvtAsyn *)pbi->dpvt;

    if(exception!=pdpvtAsyn->type) return;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s exception %d\n",
        pbi->name,(int)exception);
    scanOnce(precord);
}

static void connect(asynUser *pasynUser)
{
    boRecord    *pbo = (boRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbo;
    dpvtAsyn    *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynCommon  *pasynCommon = pdpvtAsyn->pasynCommon;
    void        *drvPvt = pdpvtAsyn->commonPvt;
    int         val = (int)pbo->val;
    asynStatus  status;

    assert(pasynCommon);
    assert(drvPvt);
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

static long initCommon(dbCommon *pdbCommon,struct link *plink,
    unsigned long mask,userCallback queue, exceptionCallback exception)
{
    dpvtAsyn      *pdpvtAsyn = 0;
    asynUser      *pasynUser = 0;
    char          *parm;
    long          status = asynSuccess;
    char          *separator;
    int           addr = 0;
    int           lenportName;
    int           len;
    asynInterface *pasynInterface;
    asynException type = 0;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO\n",pdbCommon->name);
        goto bad;
    }
    switch(mask) {
        case 0x1: type = asynExceptionConnect; break;
        case 0x2: type = asynExceptionEnable; break;
        case 0x4: type = asynExceptionAutoConnect; break;
        default:
             printf("%s illegal mask %lx. Must be 0x1, 0x2, or 0x4\n",
                 pdbCommon->name,mask);
        goto bad;
    }
    parm = plink->value.instio.string;
    separator = strchr(parm,',');
    if(!separator)  separator = strchr(parm,' ');
    if(separator) {
        sscanf(separator+1,"%d",&addr);
        lenportName = separator - parm;
        *separator = 0;
    } else { /*Just let addr = 0*/
        lenportName = strlen(parm);
    }
    if(lenportName<=0) {
       printf("%s can't determine port,addr\n",pdbCommon->name);
       goto bad;
    }
    len = sizeof(dpvtAsyn) + lenportName + 1;
    pdpvtAsyn = (dpvtAsyn *)callocMustSucceed(len,sizeof(char),"devAsyn");
    pdpvtAsyn->portName = (char *)(pdpvtAsyn+1);
    pdpvtAsyn->addr = addr;
    pdpvtAsyn->type = type;
    strncpy(pdpvtAsyn->portName,parm,lenportName);
    pasynUser = pasynManager->createAsynUser(queue,0);
    pasynUser->userPvt = pdbCommon;
    status = pasynManager->connectDevice(pasynUser,pdpvtAsyn->portName,addr);
    if(status!=asynSuccess) goto bad;
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) goto disconnect;
    pdpvtAsyn->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pdpvtAsyn->commonPvt = pasynInterface->drvPvt;
    if(exception) {
        status = pasynManager->exceptionCallbackAdd(pasynUser,exception);
        if(status!=asynSuccess) goto disconnect;
    }
    pdpvtAsyn->pasynUser = pasynUser;
    pdbCommon->dpvt = pdpvtAsyn;
    return 0;
disconnect:
    status = pasynManager->disconnect(pasynUser);
bad:
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynManager error %s\n",
            pdbCommon->name,pasynUser->errorMessage);
    }
    if(pasynUser) pasynManager->freeAsynUser(pasynUser);
    if(pdpvtAsyn) free(pdpvtAsyn);
    pdbCommon->pact = 1;
    return 0;
} 

static long initBiState(biRecord *pbi)
{
    return initCommon((dbCommon *)pbi,&pbi->inp,pbi->mask,0,exception);
}

static long readState(biRecord *pbi)
{
    dpvtAsyn   *pdpvtAsyn = (dpvtAsyn *)pbi->dpvt;
    asynUser   *pasynUser = pdpvtAsyn->pasynUser;
    int        yesNo = 0;
    asynException type;

    if(!pdpvtAsyn) {
        errlogPrintf("%s could not find pdpvtAsyn\n",pbi->name);
        goto bad;
    }
    type = pdpvtAsyn->type;
    switch(type) {
        case asynExceptionConnect:
            yesNo = pasynManager->isConnected(pasynUser);
            if(yesNo<0) goto bad;
            break;
        case asynExceptionEnable:
            yesNo = pasynManager->isEnabled(pasynUser);
            if(yesNo<0) goto bad;
            break;
        case asynExceptionAutoConnect:
            yesNo = pasynManager->isAutoConnect(pasynUser);
            if(yesNo<0) goto bad;
            break;
        default:
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "illegal asynException\n");
            goto bad;
    }
    pbi->val = pbi->rval = yesNo;
    pbi->udf = 0;
    return 0;
bad:
    asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s failed %s\n",pbi->name,pasynUser->errorMessage);
    recGblSetSevr(pbi,READ_ALARM,MAJOR_ALARM);
    return 0;
}

static long initBoState(boRecord *pbo)
{
    userCallback queue = 0;
    if(pbo->mask==0x1) queue = connect;
    return initCommon((dbCommon *)pbo,&pbo->out,pbo->mask,queue,0);
}

static long writeState(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser   *pasynUser = 0;
    asynStatus status = asynSuccess;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    pasynUser = pdpvtAsyn->pasynUser;
    switch(pdpvtAsyn->type) {
    case asynExceptionConnect:
        if(pbo->pact) return 0;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n", pbo->name);
        status = pasynManager->queueRequest(pasynUser,
            asynQueuePriorityConnect,0.0);
        if(status!=asynSuccess) goto bad;
        pbo->pact = 1;
        break;
    case asynExceptionEnable:
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s enable %d\n",
            pbo->name,pbo->val);
        status = pasynManager->enable(pasynUser,(int)(pbo->val&1));
        if(status!=asynSuccess) goto bad;
        break;
    case asynExceptionAutoConnect:
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s autoConnect %d\n",
            pbo->name,pbo->val);
        status = pasynManager->autoConnect(pasynUser,(int)(pbo->val&1));
        if(status!=asynSuccess) goto bad;
    }
    return 0;
bad:
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
        "%s error %s\n",pbo->name,pasynUser->errorMessage);
    }
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}
