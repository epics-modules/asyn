/* devAsynTest.c */
/*
 *      Author: Marty Kraimer
 *      Date:   26FEB2004
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
#include <epicsMutex.h>
#include <epicsStdio.h>
#include <epicsAssert.h>
#include <dbScan.h>
#include <alarm.h>
#include <callback.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <link.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <drvSup.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <dbCommon.h>
#include <asynDriver.h>
#include <registryFunction.h>
#include <epicsExport.h>

typedef enum {
    stateIdle,stateWrite,stateRead
}ioState;

typedef struct dpvtSo{
    CALLBACK     callback;
    char         *portName;
    asynUser     *pasynUser;
    int          addr;
    asynOctet    *pasynOctet;
    void         *octetPvt;
    char         buffer[100];
    ioState      state;
    stringinRecord *psi;
}dpvtSo;

typedef struct dpvtSi{
    CALLBACK  callback;
    dpvtSo   *pdpvtSo;
    dbAddr   dbaddr;
    char     buffer[100];
}dpvtSi;

static void sosiCallback(asynUser *pasynUser);

/* Create the dset for devAsynTest */
typedef struct dsetAsynTest{
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record;
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	read_write;
}dsetAsynTest;

static long initSo();
static long writeSo();
dsetAsynTest devAsynTestOut = {5,0,0,initSo,0,writeSo};
epicsExportAddress(dset,devAsynTestOut);

static long initSi();
static long readSi();
dsetAsynTest devAsynTestInp = {5,0,0,initSi,0,readSi};
epicsExportAddress(dset,devAsynTestInp);

static void sosiCallback(asynUser *pasynUser)
{
    stringoutRecord *pso = (stringoutRecord *)pasynUser->userPvt;
    dpvtSo          *pdpvtSo = (dpvtSo *)pso->dpvt;
    asynOctet       *pasynOctet = pdpvtSo->pasynOctet;
    void            *octetPvt = pdpvtSo->octetPvt;
    ioState         state;
    asynStatus      status;
    int             yesNo;

    state = pdpvtSo->state;
    assert(state==stateWrite || state==stateRead);
    if(pdpvtSo->state==stateWrite) {
        int nout;
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s soCallback\n",pso->name);
        sprintf(pdpvtSo->buffer,"%s",pso->val);
        status = pasynManager->isConnected(pasynUser,&yesNo);
        if(status!=asynSuccess || !yesNo) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not connected\n",pso->name);
            strcpy(pso->val,"not connected");
            recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
            goto writedone;
        }
        status = pasynManager->isEnabled(pasynUser,&yesNo);
        if(status!=asynSuccess || !yesNo) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not enabled\n",pso->name);
            strcpy(pso->val,"disabled");
            recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
            goto writedone;
        }
        status = pasynOctet->write(octetPvt,pasynUser,
            pdpvtSo->buffer,strlen(pdpvtSo->buffer)+1,&nout);
        if(status!=asynSuccess || nout<(strlen(pdpvtSo->buffer)+1)) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pasynOctet->write failed %s\n",
                pso->name,pasynUser->errorMessage);
            recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
        } else {
            asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
                pdpvtSo->buffer,strlen(pdpvtSo->buffer),
                "%s soCallback\n",pso->name);
        }
writedone:
        callbackRequestProcessCallback(&pdpvtSo->callback,pso->prio,(void *)pso);
        return;
    }
    assert(pdpvtSo->state==stateRead);
    {
        stringinRecord *psi = pdpvtSo->psi;
        dpvtSi     *pdpvtSi = (dpvtSi *)psi->dpvt;
        int        nin;
    
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s siCallback\n",psi->name);
        pdpvtSi->buffer[0] = 0;
        status = pasynManager->isConnected(pasynUser,&yesNo);
        if(status!=asynSuccess || !yesNo) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not connected\n",psi->name);
            strcpy(psi->val,"not connected");
            psi->udf = 0;
            recGblSetSevr(psi,WRITE_ALARM,MAJOR_ALARM);
            goto readdone;
        }
        status = pasynManager->isEnabled(pasynUser,&yesNo);
        if(status!=asynSuccess || !yesNo) {
            asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not enabled\n",psi->name);
            strcpy(psi->val,"not enabled");
            psi->udf = 0;
            recGblSetSevr(psi,WRITE_ALARM,MAJOR_ALARM);
            goto readdone;
        }
        pasynOctet->setEos(octetPvt,pasynUser,"",1);
        status = pasynOctet->read(octetPvt,pasynUser,
            pdpvtSi->buffer,sizeof(pdpvtSi->buffer),&nin);
        if(status!=asynSuccess || nin==0) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s pasynOctet->read failed %s\n",
                psi->name,pasynUser->errorMessage);
            recGblSetSevr(psi,READ_ALARM,MAJOR_ALARM);
        } else {
            asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
                pdpvtSo->buffer,nin,"%s siCallback\n",psi->name);
        }
        if(strcmp(pdpvtSi->buffer,pdpvtSo->buffer)==0) {
            sprintf(psi->val,"OK");
            psi->udf = 0;
        } else {
            recGblSetSevr(psi,READ_ALARM,MAJOR_ALARM);
            epicsSnprintf(psi->val,sizeof(psi->val),"%s|but got|%s\n",
                pdpvtSo->buffer,pdpvtSi->buffer);
            psi->udf = 0;
        }
readdone:
        callbackRequestProcessCallback(&pdpvtSi->callback,psi->prio,(void *)psi);
        return;
    }
}

static long initSo(stringoutRecord *pso)
{
    struct link *plink = &pso->out;
    dpvtSo      *pdpvtSo;
    asynUser    *pasynUser;
    asynStatus  status;
    char        *parm;
    char        *comma;
    int         addr = 0;
    int         lenportName;
    int         len;
    asynInterface *pasynInterface;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO",pso->name);
        pso->pact = 1;
        return -1;
    }
    parm = plink->value.instio.string;
    comma = strchr(parm,',');
    if(comma) {
        sscanf(comma+1,"%d",&addr);
        lenportName = comma - parm;
    } else {
        printf("%s OUT is invalid. Must be portName,addr",pso->name);
        pso->pact = 1;
        return -1;
    }
    len = sizeof(dpvtSo) + lenportName + 1;
    pdpvtSo = (dpvtSo *)callocMustSucceed(1,len,"dpvtAsynTest");
    pdpvtSo->portName = (char *)(pdpvtSo + 1);
    strncpy(pdpvtSo->portName,parm,lenportName);
    pasynUser = pasynManager->createAsynUser(sosiCallback,0);
    pasynUser->userPvt = pso;
    pasynUser->timeout = 2.0;
    pdpvtSo->pasynUser = pasynUser;
    pdpvtSo->addr = addr;
    status = pasynManager->connectDevice(pasynUser,pdpvtSo->portName,addr);
    if(status!=asynSuccess) {
        printf("%s connectDevice failed %s\n",
            pso->name,pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        pso->pact = 1;
        free(pdpvtSo);
        return -1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s findInterface failed %s\n",
            pso->name,pasynUser->errorMessage);
        pasynManager->disconnect(pasynUser);
        pasynManager->freeAsynUser(pasynUser);
        pso->pact = 1;
        free(pdpvtSo);
        return -1;
    }
    pdpvtSo->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pdpvtSo->octetPvt = pasynInterface->drvPvt;
    pso->dpvt = pdpvtSo;
    return 0;
} 

static long writeSo(stringoutRecord *pso)
{
    dpvtSo     *pdpvtSo = (dpvtSo *)pso->dpvt;
    asynUser   *pasynUser = pdpvtSo->pasynUser;
    asynStatus status;
    int        yesNo;

    if(!pdpvtSo) {
        recGblSetSevr(pso,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    if(pso->pact) {
        assert(pdpvtSo->state==stateWrite);
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s set stateRead\n",pso->name);
        pdpvtSo->state = stateRead;
        return 0;
    }
    if(pdpvtSo->state!=stateIdle) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s state!=stateIdle\n",pso->name);
        strcpy(pso->val,"not stateIdle");
        return 0;
    }
    status = pasynManager->isConnected(pasynUser,&yesNo);
    if(status==asynSuccess && yesNo)
        status = pasynManager->isAutoConnect(pasynUser,&yesNo);
    if(status!=asynSuccess || !yesNo) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not connected\n",pso->name);
        strcpy(pso->val,"not connected");
        pso->udf = 0;
        goto bad;
    }
    status = pasynManager->isEnabled(pasynUser,&yesNo);
    if(status!=asynSuccess || !yesNo) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s not enabled\n",pso->name);
        strcpy(pso->val,"not enabled");
        pso->udf = 0;
        goto bad;
    }
    status = pasynManager->lock(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s %s\n",pso->name,pasynUser->errorMessage);
        strcpy(pso->val,"lock failed");
        goto bad;
    }
    pdpvtSo->state = stateWrite;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s queueRequest\n",pso->name);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        pasynManager->unlock(pasynUser);
        goto bad;
    }
    pso->pact = 1;
    return 0;
bad:
    recGblSetSevr(pso,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}

static long initSi(stringinRecord *psi)
{
    struct link *plink = &psi->inp;
    dpvtSi      *pdpvtSi;
    char        *parm;
    dbAddr      dbaddr;
    long        status;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO",psi->name);
        psi->pact = 1;
        return -1;
    }
    parm = plink->value.instio.string;
    status = dbNameToAddr(parm,&dbaddr);
    if(status) {
        printf("%s dbNameToAddr failed\n",psi->name);
        psi->pact = 1;
        return 0;
    }
    pdpvtSi = (dpvtSi *)callocMustSucceed(1,sizeof(dpvtSi),"devAsynTest");
    pdpvtSi->dbaddr = dbaddr;
    psi->dpvt = pdpvtSi;
    return 0;
}

static long readSi(stringinRecord *psi)
{
    dpvtSi     *pdpvtSi = (dpvtSi *)psi->dpvt;
    dpvtSo     *pdpvtSo;
    asynUser   *pasynUser = 0;
    asynStatus status;

    if(!pdpvtSi) {
        recGblSetSevr(psi,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    pdpvtSo = pdpvtSi->pdpvtSo;
    if(!pdpvtSo) {
        dbCommon *pdbCommon = (dbCommon *)pdpvtSi->dbaddr.precord;
        pdpvtSo = pdpvtSi->pdpvtSo = (dpvtSo *)pdbCommon->dpvt;
        if(!pdpvtSo) {
            sprintf(psi->val,"%s could not find pdpvtSo",psi->name);
            goto bad;
        }
        pdpvtSo->psi = psi;
    }
    pasynUser = pdpvtSo->pasynUser;
    if(psi->pact) {
        assert(pdpvtSo->state==stateRead);
        status = pasynManager->unlock(pasynUser);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s unlock error %s\n",psi->name,pasynUser->errorMessage);
            sprintf(psi->val,
                "%s unlock error %s\n",psi->name,pasynUser->errorMessage);
            psi->udf = 0;
            goto bad;
        }
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s set stateIdle\n",psi->name);
        pdpvtSo->state = stateIdle;
        return 0;
    }
    if(pdpvtSo->state!=stateRead) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s state!=stateRead\n",psi->name);
        strcpy(psi->val,"not stateRead");
        psi->udf = 0;
        goto bad;
    }
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s queueRequest error %s\n",psi->name,pasynUser->errorMessage);
        sprintf(psi->val,
            "%s queueRequest error %s\n",psi->name,pasynUser->errorMessage);
        goto bad;
    }
    psi->pact = 1;
    return 0;
bad:
    recGblSetSevr(psi,WRITE_ALARM,MAJOR_ALARM);
    return 0;
}
