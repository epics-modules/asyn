/* devAsynTrace.c */
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
#include <dbDefs.h>
#include <dbAccess.h>
#include <recGbl.h>
#include <recSup.h>
#include <devSup.h>
#include <drvSup.h>
#include <longoutRecord.h>
#include <stringoutRecord.h>
#include <epicsExport.h>

#include <asynDriver.h>

typedef struct devTrace{
    asynUser *pasynUser;
    char     *portName;
}devTrace;

/* Create the dset for devAsynTrace */
typedef struct dsetTrace{
	long		number;
	DEVSUPFUN	report;
	DEVSUPFUN	init;
	DEVSUPFUN	init_record;
	DEVSUPFUN	get_ioint_info;
	DEVSUPFUN	write_longout;
}dsetTrace;

static long initCommon();

static long initTrace();
static long writeTrace();
static long initTraceIO();
static long writeTraceIO();
static long initTraceIOTruncateSize();
static long writeTraceIOTruncateSize();
static long initStringout();
static long writeTraceFile();

dsetTrace devAsynTrace = {5,0,0,initTrace,0,writeTrace};
epicsExportAddress(dset,devAsynTrace);

dsetTrace devAsynTraceIO = {5,0,0,initTraceIO,0,writeTraceIO};
epicsExportAddress(dset,devAsynTraceIO);

dsetTrace devAsynTraceIOTruncateSize =
{5,0,0,initTraceIOTruncateSize,0,writeTraceIOTruncateSize};
epicsExportAddress(dset,devAsynTraceIOTruncateSize);

dsetTrace devAsynTraceFile = {5,0,0,initStringout,0,writeTraceFile};
epicsExportAddress(dset,devAsynTraceFile);

static long initCommon(dbCommon *pdbCommon,struct link *plink)
{
    devTrace *pdevTrace;
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
    len = sizeof(devTrace) + lenportName + 1;
    pdevTrace = (devTrace *)callocMustSucceed(1,len,"devAsynTrace");
    pdevTrace->portName = (char *)(pdevTrace + 1);
    strncpy(pdevTrace->portName,parm,lenportName);
    pasynUser = pasynManager->createAsynUser(0,0);
    pdevTrace->pasynUser = pasynUser;
    status = pasynManager->connectDevice(pasynUser,pdevTrace->portName,addr);
    if(status!=asynSuccess) {
        printf("%s connectDevice failed %s\n",
            pdbCommon->name,pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        pdbCommon->pact = 1;
        free(pdevTrace);
        return -1;
    }
    pdbCommon->dpvt = pdevTrace;
    return 0;
} 

static long initTrace(longoutRecord *plongout)
{
    long status = initCommon((dbCommon *)plongout,&plongout->out);
    devTrace *pdevTrace;
    asynUser *pasynUser;
    int mask;

    if(status!=asynSuccess) return(status);
    pdevTrace = (devTrace *)plongout->dpvt;
    pasynUser = pdevTrace->pasynUser;
    mask = pasynTrace->getTraceMask(pasynUser);
    plongout->val = mask;
    plongout->udf = 0;
    return 0;
}

static long writeTrace(longoutRecord	*plongout)
{
    devTrace *pdevTrace = (devTrace *)plongout->dpvt;
    asynUser *pasynUser = pdevTrace->pasynUser;
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
    long status = initCommon((dbCommon *)plongout,&plongout->out);
    devTrace *pdevTrace;
    asynUser *pasynUser;
    int mask;

    if(status!=asynSuccess) return(status);
    pdevTrace = (devTrace *)plongout->dpvt;
    pasynUser = pdevTrace->pasynUser;
    mask = pasynTrace->getTraceIOMask(pasynUser);
    plongout->val = mask;
    plongout->udf = 0;
    return 0;
}

static long writeTraceIO(longoutRecord *plongout)
{
    devTrace *pdevTrace = (devTrace *)plongout->dpvt;
    asynUser *pasynUser = pdevTrace->pasynUser;
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
    long initStatus = initCommon((dbCommon *)plongout,&plongout->out);
    devTrace *pdevTrace;
    asynUser *pasynUser;
    int size;

    if(initStatus!=asynSuccess) return(initStatus);
    pdevTrace = (devTrace *)plongout->dpvt;
    pasynUser = pdevTrace->pasynUser;
    size = pasynTrace->getTraceIOTruncateSize(pasynUser);
    plongout->val = size;
    plongout->udf = 0;
    return 0;
}

static long writeTraceIOTruncateSize(longoutRecord *plongout)
{
    devTrace *pdevTrace = (devTrace *)plongout->dpvt;
    asynUser *pasynUser = pdevTrace->pasynUser;
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
    return initCommon((dbCommon *)pstringout,&(pstringout->out));
}

static long writeTraceFile(stringoutRecord *pstringout)
{
    devTrace *pdevTrace = (devTrace *)pstringout->dpvt;
    asynUser *pasynUser = pdevTrace->pasynUser;
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
