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
#include <boRecord.h>
#include <dbCommon.h>
#include <registryFunction.h>
#include <epicsExport.h>

#include <asynDriver.h>

typedef struct dpvtAsyn {
    CALLBACK     callback;
    asynUser     *pasynUser;
    char         *portName;
    int          addr;
    asynCommon   *pasynCommon;
    void         *commonPvt;
}dpvtAsyn;

static long initCommon(dbCommon *pdbCommon,struct link *plink,userCallback queue);

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

dsetAsyn devAsynEnable = {5,0,0,initEnable,0,writeEnable};
epicsExportAddress(dset,devAsynEnable);

dsetAsyn devAsynAutoConnect = {5,0,0,initAutoConnect,0,writeAutoConnect};
epicsExportAddress(dset,devAsynAutoConnect);

dsetAsyn devAsynConnect = {5,0,0,initConnect,0,writeConnect};
epicsExportAddress(dset,devAsynConnect);


static long initCommon(dbCommon *pdbCommon,struct link *plink,
    userCallback queue)
{
    dpvtAsyn      *pdpvtAsyn;
    asynUser      *pasynUser;
    char          *parm;
    long          status;
    char          *separator;
    int           addr = 0;
    int           lenportName;
    int           len;
    asynInterface *pasynInterface;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO\n",pdbCommon->name);
        pdbCommon->pact = 1;
        return 0;
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
    strncpy(pdpvtAsyn->portName,parm,lenportName);
    pasynUser = pasynManager->createAsynUser(queue,0);
    pasynUser->userPvt = pdbCommon;
    status = pasynManager->connectDevice(pasynUser,pdpvtAsyn->portName,addr);
    if(status==asynSuccess) {
        pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
        if(pasynInterface) {
            pdpvtAsyn->pasynCommon = (asynCommon *)pasynInterface->pinterface;
            pdpvtAsyn->commonPvt = pasynInterface->drvPvt;
        }
    }
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynManager error %s\n",pdbCommon->name,pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        free(pdpvtAsyn);
        goto bad;
    } else {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connectDevice\n",
            pdbCommon->name);
    }
    pdpvtAsyn->pasynUser = pasynUser;
    pdbCommon->dpvt = pdpvtAsyn;
    return 0;
bad:
    pdbCommon->pact = 1;
    return 0;
} 

static void enableException(asynUser *pasynUser,asynException exception)
{
    boRecord    *pbo = (boRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbo;
    int         yesNo,valueChanged;

    if(exception!=asynExceptionEnable) return;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s enableException %d\n",
        pbo->name,(int)exception);
    dbScanLock(precord);
    yesNo = pasynManager->isEnabled(pasynUser);
    valueChanged = (pbo->val&1) ^ yesNo;
    if(valueChanged) pbo->val = pbo->rval = yesNo;
    dbScanUnlock(precord);
    if(valueChanged) scanOnce(precord);
}

static long initEnable(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn;
    int        yesNo;
    asynUser   *pasynUser;
    asynStatus status;

    initCommon((dbCommon *)pbo,&pbo->out,0);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    if(!pdpvtAsyn) return 0;
    pasynUser = pdpvtAsyn->pasynUser;
    status = pasynManager->exceptionCallbackAdd(pasynUser,enableException);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s exceptionCallbackAdd failed  %s\n",
            pbo->name,pasynUser->errorMessage);
    }
    yesNo = pasynManager->isEnabled(pasynUser);
    if(yesNo<0) {
        printf("%s isEnabled failed %s\n",pbo->name,pasynUser->errorMessage);
    } else {
        pbo->val = pbo->rval = yesNo;
        pbo->udf = 0;
    }
    return 0;
}

static long writeEnable(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser   *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;
    int        yesNo;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    yesNo = pasynManager->isEnabled(pasynUser);
    if(yesNo==(pbo->val&1)) return 0;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s enable %d\n",
        pbo->name,(int)pbo->val);
    status = pasynManager->enable(pasynUser,(int)(pbo->val&1));
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s enable failed  %s\n",
            pbo->name,pasynUser->errorMessage);
    }
    return 0;
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static void autoConnectException(asynUser *pasynUser,asynException exception)
{
    boRecord    *pbo = (boRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbo;
    int         yesNo,valueChanged;

    if(exception!=asynExceptionAutoConnect) return;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s autoConnectException %d\n",
        pbo->name,(int)exception);
    dbScanLock(precord);
    yesNo = pasynManager->isAutoConnect(pasynUser);
    valueChanged = (pbo->val&1) ^ yesNo;
    if(valueChanged) pbo->val = yesNo;
    dbScanUnlock(precord);
    if(valueChanged) scanOnce(precord);
}

static long initAutoConnect(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn;
    int        yesNo;
    asynUser   *pasynUser;
    asynStatus status;

    initCommon((dbCommon *)pbo,&pbo->out,0);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    if(!pdpvtAsyn) return 0;
    pasynUser = pdpvtAsyn->pasynUser;
    status = pasynManager->exceptionCallbackAdd(pasynUser,autoConnectException);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s exceptionCallbackAdd failed  %s\n",
            pbo->name,pasynUser->errorMessage);
    }
    yesNo = pasynManager->isAutoConnect(pasynUser);
    if(yesNo<0) {
        printf("%s isAutoConnectd failed %s\n",pbo->name,pasynUser->errorMessage);
    } else {
        pbo->val = pbo->rval = yesNo;
        pbo->udf = 0;
    }
    return 0;
}

static long writeAutoConnect(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser   *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;
    int        yesNo;

    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    yesNo = pasynManager->isAutoConnect(pasynUser);
    if(yesNo==(pbo->val&1)) return 0;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s autoConnect %d\n",
        pbo->name,(int)pbo->val);
    status = pasynManager->autoConnect(pasynUser,(int)(pbo->val&1));
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s autoConnect failed  %s\n",
            pbo->name,pasynUser->errorMessage);
    }
    return 0;
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

/*callback to issue disconnect/connect to low level driver*/
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

static void connectException(asynUser *pasynUser,asynException exception)
{
    boRecord    *pbo = (boRecord *)pasynUser->userPvt;
    dbCommon    *precord = (dbCommon *)pbo;
    int         yesNo,valueChanged;

    if(exception!=asynExceptionConnect) return;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s connectException %d\n",
        pbo->name,(int)exception);
    dbScanLock(precord);
    yesNo = pasynManager->isConnected(pasynUser);
    valueChanged = (pbo->val&1) ^ yesNo;
    if(valueChanged) pbo->val =yesNo;
    dbScanUnlock(precord);
    if(valueChanged) scanOnce(precord);
}

static long initConnect(boRecord *pbo)
{
    dpvtAsyn    *pdpvtAsyn;
    int         yesNo;
    asynUser    *pasynUser;
    asynStatus  status;

    initCommon((dbCommon *)pbo,&pbo->out,connect);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    if(!pdpvtAsyn) return 0;
    pasynUser = pdpvtAsyn->pasynUser;
    yesNo = pasynManager->isConnected(pasynUser);
    if(yesNo<0) {
        printf("%s isConnected failed %s\n",pbo->name,pasynUser->errorMessage);
    } else {
        pbo->val = pbo->rval = yesNo;
        pbo->udf = 0;
    }
    status = pasynManager->exceptionCallbackAdd(pasynUser,connectException);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s exceptionCallbackAdd error %s\n",
             pbo->name,pasynUser->errorMessage);
    }
    return 0;
}

static long writeConnect(boRecord *pbo)
{
    dpvtAsyn   *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser   *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;
    int        yesNo;

    
    if(!pdpvtAsyn) {
        printf("%s could not find pdpvtAsyn\n",pbo->name);
        goto bad;
    }
    yesNo = pasynManager->isConnected(pasynUser);
    if(yesNo==(pbo->val&1)) return 0;
    if(pbo->pact) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s connect/disconnect failed %s\n",pbo->name,pasynUser->errorMessage);
        goto bad;
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n", pbo->name);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s enable failed %s\n",pbo->name,pasynUser->errorMessage);
        goto bad;
    } else {
        pbo->pact = 1;
    }
    return 0;
bad:
    recGblSetSevr(pbo,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}
