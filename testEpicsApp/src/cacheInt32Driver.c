/*cacheDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*test driver for asyn support*/
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
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynInt32.h>

typedef struct drvUserInfo {
    const char *drvInfo;
}drvUserInfo;

static const char *drvUserType = "cacheDriverType";

typedef struct cachePvt {
    const char    *portName;
    int           connected;
    int           multiDevice;
    double        delay;
    asynInterface common;
    asynInterface drvUser;
    asynInterface int32;
    epicsInt32    value;
}cachePvt;
    
/* init routine */
static int cacheInt32DriverInit(const char *dn, double delay,
    int noAutoConnect);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

/* asynDrvUser methods */
static asynStatus drvUserCreate(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize);
static asynStatus drvUserGetType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize);
static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser);
static asynDrvUser drvUser = {drvUserCreate,drvUserGetType,drvUserDestroy};


/* asynInt32 methods */
static asynStatus cacheWrite(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value);
static asynStatus cacheRead(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value);
static asynStatus getBounds(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high);
static asynInt32 int32 = { cacheWrite, cacheRead, getBounds};

static int cacheInt32DriverInit(const char *dn, double delay,
    int noAutoConnect)
{
    cachePvt    *pcachePvt;
    char       *portName;
    asynStatus status;
    int        nbytes;
    int        attributes;

    nbytes = sizeof(cachePvt) + strlen(dn) + 1;
    pcachePvt = callocMustSucceed(nbytes,sizeof(char),"cacheInt32DriverInit");
    portName = (char *)(pcachePvt + 1);
    strcpy(portName,dn);
    pcachePvt->portName = portName;
    pcachePvt->delay = delay;
    pcachePvt->common.interfaceType = asynCommonType;
    pcachePvt->common.pinterface  = (void *)&asyn;
    pcachePvt->common.drvPvt = pcachePvt;
    pcachePvt->drvUser.interfaceType = asynDrvUserType;
    pcachePvt->drvUser.pinterface  = (void *)&drvUser;
    pcachePvt->drvUser.drvPvt = pcachePvt;
    pcachePvt->int32.interfaceType = asynInt32Type;
    pcachePvt->int32.pinterface  = (void *)&int32;
    pcachePvt->int32.drvPvt = pcachePvt;
    attributes = 0;
    if(delay>0.0) attributes|=ASYN_CANBLOCK;
    status = pasynManager->registerPort(portName,attributes,!noAutoConnect,0,0);
    if(status!=asynSuccess) {
        printf("cacheInt32DriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pcachePvt->common);
    if(status!=asynSuccess){
        printf("cacheInt32DriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pcachePvt->drvUser);
    if(status!=asynSuccess){
        printf("cacheInt32DriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pcachePvt->int32);
    if(status!=asynSuccess){
        printf("cacheInt32DriverInit registerInterface failed\n");
        return 0;
    }
    return(0);
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    cachePvt *pcachePvt = (cachePvt *)drvPvt;

    fprintf(fp,"    cacheDriver: connected:%s delay = %f\n",
        (pcachePvt->connected ? "Yes" : "No"),
        pcachePvt->delay);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    cachePvt    *pcachePvt = (cachePvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:connect\n",pcachePvt->portName);
    if(pcachePvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s cacheDriver:connect port already connected\n",
           pcachePvt->portName);
        return asynError;
    }
    pcachePvt->connected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    cachePvt    *pcachePvt = (cachePvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:disconnect\n",pcachePvt->portName);
    if(!pcachePvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
           "%s cacheDriver:disconnect port not connected\n",
           pcachePvt->portName);
        return asynError;
    }
    pcachePvt->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

static asynStatus drvUserCreate(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize)
{
    cachePvt    *pcachePvt = (cachePvt *)drvPvt;
    drvUserInfo *pdrvUserInfo;

    pdrvUserInfo = callocMustSucceed(1,sizeof(drvUserInfo),"drvUserCreate");
    pdrvUserInfo->drvInfo = drvInfo;
    pasynUser->drvUser = pdrvUserInfo;
    if(pptypeName) *pptypeName = drvUserType;
    if(psize) *psize = sizeof(drvUserInfo);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:drvUserCreate drvInfo %s\n",
        pcachePvt->portName,
        (drvInfo ? drvInfo : "null"));
    return asynSuccess;
}

static asynStatus drvUserGetType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize)
{
    cachePvt    *pcachePvt = (cachePvt *)drvPvt;
    if(pasynUser->drvUser) {
        if(pptypeName) *pptypeName = drvUserType;
        if(psize) *psize = sizeof(drvUserInfo);
    } else {
        if(pptypeName) *pptypeName = 0;
        if(psize) *psize = 0;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:drvUserGetType\n", pcachePvt->portName);
    return asynSuccess;
}

static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser)
{
    cachePvt    *pcachePvt = (cachePvt *)drvPvt;
    void *drvUser = pasynUser->drvUser;
    if(drvUser) {
        pasynUser->drvUser = 0;
        free(pasynUser->drvUser);
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:drvUserDestroy\n", pcachePvt->portName);
    return asynSuccess;
}

static asynStatus cacheWrite(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value)
{
    cachePvt   *pcachePvt = (cachePvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:writeInt32 value %d\n",
        pcachePvt->portName,value);
    if(!pcachePvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s cacheDriver:read not connected\n",
            pcachePvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s cacheDriver:read not connected\n",
            pcachePvt->portName);
        return asynError;
    }
    pcachePvt->value = value;
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s write %d\n",pcachePvt->portName,value);
    return asynSuccess;
}

static asynStatus cacheRead(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value)
{
    cachePvt *pcachePvt = (cachePvt *)drvPvt;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:readInt32 value %d\n",
        pcachePvt->portName,value);
    if(!pcachePvt->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s cacheDriver:read  not connected\n",
            pcachePvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s cacheDriver:read not connected\n",
            pcachePvt->portName);
        return asynError;
    }
    *value = pcachePvt->value;
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s read %d\n",pcachePvt->portName,value);
    return asynSuccess;
}

static asynStatus getBounds(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high)
{
    *low = *high = 0;
    return asynSuccess;
}

/* register cacheInt32DriverInit*/
static const iocshArg cacheDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg cacheDriverInitArg1 = { "delay", iocshArgDouble };
static const iocshArg cacheDriverInitArg2 = { "disable auto-connect", iocshArgInt };
static const iocshArg *cacheDriverInitArgs[] = {
    &cacheDriverInitArg0,&cacheDriverInitArg1,
    &cacheDriverInitArg2};
static const iocshFuncDef cacheDriverInitFuncDef = {
    "cacheInt32DriverInit", 3, cacheDriverInitArgs};
static void cacheDriverInitCallFunc(const iocshArgBuf *args)
{
    cacheInt32DriverInit(args[0].sval,args[1].dval,args[2].ival);
}

static void cacheInt32DriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&cacheDriverInitFuncDef, cacheDriverInitCallFunc);
    }
}
epicsExportRegistrar(cacheInt32DriverRegister);
