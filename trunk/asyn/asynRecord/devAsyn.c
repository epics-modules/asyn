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
#include <alarm.h>
#include <callback.h>
#include <dbDefs.h>
#include <dbAccess.h>
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

typedef struct dpvtAsyn{
    CALLBACK callback;
    asynUser *pasynUser;
    char     *portName;
    int      addr;
}dpvtAsyn;

/* Create the dset for devAsynTrace */
typedef struct dsetAsyn{
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record;
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	write_longout;
}dsetAsyn;

static long initCommon();

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
static long initStringout();
static long writeTraceFile();

dsetAsyn devAsynEnable = {5,0,0,initEnable,0,writeEnable};
epicsExportAddress(dset,devAsynEnable);

dsetAsyn devAsynAutoConnect = {5,0,0,initAutoConnect,0,writeAutoConnect};
epicsExportAddress(dset,devAsynAutoConnect);

dsetAsyn devAsynConnect = {5,0,0,initConnect,0,writeConnect};
epicsExportAddress(dset,devAsynConnect);

dsetAsyn devAsynTrace = {5,0,0,initTrace,0,writeTrace};
epicsExportAddress(dset,devAsynTrace);

dsetAsyn devAsynTraceIO = {5,0,0,initTraceIO,0,writeTraceIO};
epicsExportAddress(dset,devAsynTraceIO);

dsetAsyn devAsynTraceIOTruncateSize =
{5,0,0,initTraceIOTruncateSize,0,writeTraceIOTruncateSize};
epicsExportAddress(dset,devAsynTraceIOTruncateSize);

dsetAsyn devAsynTraceFile = {5,0,0,initStringout,0,writeTraceFile};
epicsExportAddress(dset,devAsynTraceFile);

static long initCommon(dbCommon *pdbCommon,struct link *plink,userCallback queue)
{
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    asynStatus status;
    char *parm;
    char *comma;
    int addr=0;
    int lenportName;
    int len;
    
    if(plink->type!=INST_IO) {
        printf("%s link type invalid. Must be INST_IO",pdbCommon->name);
        pdbCommon->pact = 1;
        return -1;
    }
    parm = plink->value.instio.string;
    comma = strchr(parm,',');
    if(comma) {
        sscanf(comma+1,"%d",&addr);
        lenportName = comma - parm;
    } else {
        lenportName = strlen(parm);
    }
    len = sizeof(dpvtAsyn) + lenportName + 1;
    pdpvtAsyn = (dpvtAsyn *)callocMustSucceed(1,len,"devAsynTrace");
    pdpvtAsyn->portName = (char *)(pdpvtAsyn + 1);
    strncpy(pdpvtAsyn->portName,parm,lenportName);
    pasynUser = pasynManager->createAsynUser(queue,0);
    pdpvtAsyn->pasynUser = pasynUser;
    pdpvtAsyn->addr = addr;
    status = pasynManager->connectDevice(pasynUser,pdpvtAsyn->portName,addr);
    if(status!=asynSuccess) {
        printf("%s connectDevice failed %s\n",
            pdbCommon->name,pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        pdbCommon->pact = 1;
        free(pdpvtAsyn);
        return -1;
    }
    pdbCommon->dpvt = pdpvtAsyn;
    return 0;
} 

static long initEnable(boRecord *pbo)
{
    long status = initCommon((dbCommon *)pbo,&pbo->out,0);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int yesNo;

    if(status!=asynSuccess) return(status);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    yesNo = pasynManager->isEnabled(pasynUser);
    pbo->val = yesNo;
    pbo->udf = 0;
    return 0;
}

static long writeEnable(boRecord *pbo)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynManager->enable(pasynUser,(int)pbo->val);
    if(status!=asynSuccess) {
        printf("%s enable failed %s\n",
            pbo->name,pasynUser->errorMessage);
        recGblSetSevr(pbo,WRITE_ALARM,INVALID_ALARM);
    }
    return 0;
}

static long initAutoConnect(boRecord *pbo)
{
    long status = initCommon((dbCommon *)pbo,&pbo->out,0);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int yesNo;

    if(status!=asynSuccess) return(status);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    yesNo = pasynManager->isAutoConnectEnabled(pasynUser);
    pbo->val = yesNo;
    pbo->udf = 0;
    return 0;
}

static long writeAutoConnect(boRecord *pbo)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynManager->enableAutoConnect(pasynUser,(int)pbo->val);
    if(status!=asynSuccess) {
        printf("%s enable failed %s\n",
            pbo->name,pasynUser->errorMessage);
        recGblSetSevr(pbo,WRITE_ALARM,INVALID_ALARM);
    }
    return 0;
}

static void connectCallback(asynUser *pasynUser)
{
    boRecord *pbo = (boRecord *)pasynUser->userPvt;
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynStatus status = asynSuccess;
    asynInterface *pasynInterface;
    asynCommon *pasynCommon;
    void      *drvPvt;

    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("%s findInterface failed %s\n",
            pbo->name,pasynUser->errorMessage);
        goto done;
    }
    pasynCommon = (asynCommon *)pasynInterface->pinterface;
    drvPvt = pasynInterface->drvPvt;
    if(pbo->val==0) {
        status = pasynCommon->disconnect(drvPvt,pasynUser);
    } else {
        status = pasynCommon->connect(drvPvt,pasynUser);
    }
done:
    if(status!=asynSuccess) recGblSetSevr(pbo,WRITE_ALARM,INVALID_ALARM);
    callbackRequestProcessCallback(&pdpvtAsyn->callback,pbo->prio,(void *)pbo);
}

static long initConnect(boRecord *pbo)
{
    long status = initCommon((dbCommon *)pbo,&pbo->out,connectCallback);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int yesNo;

    if(status!=asynSuccess) return(status);
    pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    pasynUser->userPvt = (void *)pbo;
    yesNo = pasynManager->isConnected(pasynUser);
    pbo->val = yesNo;
    pbo->udf = 0;
    return 0;
}

static long writeConnect(boRecord *pbo)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)pbo->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0);
    if(status!=asynSuccess) {
        printf("%s enable failed %s\n",
            pbo->name,pasynUser->errorMessage);
        recGblSetSevr(pbo,WRITE_ALARM,INVALID_ALARM);
    } else {
        pbo->pact = 1;
    }
    return 0;
}


static long initTrace(longoutRecord *plongout)
{
    long status = initCommon((dbCommon *)plongout,&plongout->out,0);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int mask;

    if(status!=asynSuccess) return(status);
    pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    mask = pasynTrace->getTraceMask(pasynUser);
    plongout->val = mask;
    plongout->udf = 0;
    return 0;
}

static long writeTrace(longoutRecord	*plongout)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynTrace->setTraceMask(pasynUser,(int)plongout->val);
    if(status!=asynSuccess) {
        printf("%s setTraceMask failed %s\n",
            plongout->name,pasynUser->errorMessage);
        recGblSetSevr(plongout,WRITE_ALARM,INVALID_ALARM);
    }
    return 0;
}

static long initTraceIO(longoutRecord *plongout)
{
    long status = initCommon((dbCommon *)plongout,&plongout->out,0);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int mask;

    if(status!=asynSuccess) return(status);
    pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    mask = pasynTrace->getTraceIOMask(pasynUser);
    plongout->val = mask;
    plongout->udf = 0;
    return 0;
}

static long writeTraceIO(longoutRecord *plongout)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynTrace->setTraceIOMask(pasynUser,(int)plongout->val);
    if(status!=asynSuccess) {
        printf("%s setTraceIOMask failed %s\n",
            plongout->name,pasynUser->errorMessage);
        recGblSetSevr(plongout,WRITE_ALARM,INVALID_ALARM);
    }
    return 0;
}

static long initTraceIOTruncateSize(longoutRecord *plongout)
{
    long initStatus = initCommon((dbCommon *)plongout,&plongout->out,0);
    dpvtAsyn *pdpvtAsyn;
    asynUser *pasynUser;
    int size;

    if(initStatus!=asynSuccess) return(initStatus);
    pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    pasynUser = pdpvtAsyn->pasynUser;
    size = pasynTrace->getTraceIOTruncateSize(pasynUser);
    plongout->val = size;
    plongout->udf = 0;
    return 0;
}

static long writeTraceIOTruncateSize(longoutRecord *plongout)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)plongout->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;

    status = pasynTrace->setTraceIOTruncateSize(pasynUser,(int)plongout->val);
    if(status!=asynSuccess) {
        printf("%s setTraceIOTruncateSize failed %s\n",
            plongout->name,pasynUser->errorMessage);
        recGblSetSevr(plongout,WRITE_ALARM,INVALID_ALARM);
    }
    return 0;
}

static long initStringout(stringoutRecord *pstringout)
{
    return initCommon((dbCommon *)pstringout,&(pstringout->out),0);
}

static long writeTraceFile(stringoutRecord *pstringout)
{
    dpvtAsyn *pdpvtAsyn = (dpvtAsyn *)pstringout->dpvt;
    asynUser *pasynUser = pdpvtAsyn->pasynUser;
    asynStatus status;
    FILE *fd;

    status = pasynTrace->lock(pasynUser);
    if(status!=asynSuccess) {
        printf("%s lock failed %s WHY\n",
            pstringout->name,pasynUser->errorMessage);
        recGblSetSevr(pstringout,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    fd = pasynTrace->getTraceFILE(pasynUser);
    if(fd) fclose(fd);
    fd = fopen(pstringout->val,"a+");
    if(!fd) {
        printf("%s fopen failed\n",pstringout->name);
        recGblSetSevr(pstringout,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    status = pasynTrace->setTraceFILE(pasynUser,fd);
    if(status!=asynSuccess) {
        printf("%s setTraceFILE failed %s\n",
            pstringout->name,pasynUser->errorMessage);
        recGblSetSevr(pstringout,WRITE_ALARM,INVALID_ALARM);
    }
    status = pasynTrace->unlock(pasynUser);
    if(status!=asynSuccess) {
        printf("%s unlock failed %s WHY\n",
            pstringout->name,pasynUser->errorMessage);
        recGblSetSevr(pstringout,WRITE_ALARM,INVALID_ALARM);
        return 0;
    }
    return 0;
}

/*The subroutines for initializing the trace and traceIO fields*/
#define nTracePVs 5
static long asynTraceInit(subRecord *psub,void *unused)
{ return 0;}

static long asynTraceWrite(subRecord *psub)
{
    int mask = (int)psub->l;
    short value;
    struct link *plink = &psub->inpa;
    int  n;
    dbCommon *pdbCommon;

    if(psub->inpl.type!=DB_LINK) goto bad;
    for(n=0; n<nTracePVs; n++) {
        if(plink->type!=DB_LINK) goto bad;
        pdbCommon = (dbCommon *)plink->value.pv_link.precord;
        value = mask&1; /* get low order bit*/
        mask = mask>>1;
        dbPutLinkValue(plink,DBR_SHORT,&value,1);
        pdbCommon->udf = 0;
        plink++;
    }
    return(0);
bad:
    recGblSetSevr(psub,WRITE_ALARM,INVALID_ALARM);
    printf("%s asynTraceWrite: input links not properly defined\n",psub->name);
    return 0;
}

#define nTraceIOPVs 3
static long asynTraceIOInit(subRecord *psub,void *unused)
{ return 0;}

static long asynTraceIOWrite(subRecord *psub)
{
    int mask = (int)psub->l;
    short value;
    struct link *plink = &psub->inpa;
    int  n;
    dbCommon *pdbCommon;

    if(psub->inpl.type!=DB_LINK) goto bad;
    for(n=0; n<nTraceIOPVs; n++) {
        if(plink->type!=DB_LINK) goto bad;
        pdbCommon = (dbCommon *)plink->value.pv_link.precord;
        value = mask&1; /* get low order bit*/
        mask = mask>>1;
        dbPutLinkValue(plink,DBR_SHORT,&value,1);
        pdbCommon->udf = 0;
        plink++;
    }
    return(0);
bad:
    recGblSetSevr(psub,WRITE_ALARM,INVALID_ALARM);
    printf("%s asynTraceInit: input links not properly defined\n",psub->name);
    return 0;
}

static registryFunctionRef ref[] = {
    {"asynTraceInit",(REGISTRYFUNCTION)&asynTraceInit},
    {"asynTraceWrite",(REGISTRYFUNCTION)&asynTraceWrite},
    {"asynTraceIOInit",(REGISTRYFUNCTION)&asynTraceIOInit},
    {"asynTraceIOWrite",(REGISTRYFUNCTION)&asynTraceIOWrite}
};

static void asynSubRegistrar(void)
{
    registryFunctionRefAdd(ref,NELEMENTS(ref));
}
epicsExportRegistrar(asynSubRegistrar);

/* add support so that dbior generates asynDriver reports */
static long drvAsynReport(int level);
struct {
        long    number;
        DRVSUPFUN       report;
        DRVSUPFUN       init;
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
