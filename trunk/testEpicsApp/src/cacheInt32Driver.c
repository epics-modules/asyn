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

#define NCHANNELS 16

typedef struct cachePvt {
    const char    *portName;
    int           connected;
    double        delay;
    asynInterface common;
    asynInterface int32;
    epicsInt32    value[NCHANNELS];
}cachePvt;

/*Utility routine */
static asynStatus getAddr(cachePvt *pcachePvt,asynUser *pasynUser,
    int *paddr,int portOK);
/* init routine */
static int cacheInt32DriverInit(const char *dn, double delay,
    int noAutoConnect);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

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
    pcachePvt->int32.interfaceType = asynInt32Type;
    pcachePvt->int32.pinterface  = (void *)&int32;
    pcachePvt->int32.drvPvt = pcachePvt;
    attributes = ASYN_MULTIDEVICE;
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
    status = pasynManager->registerInterface(portName,&pcachePvt->int32);
    if(status!=asynSuccess){
        printf("cacheInt32DriverInit registerInterface failed\n");
        return 0;
    }
    return(0);
}

static asynStatus getAddr(cachePvt *pcachePvt,asynUser *pasynUser,
    int *paddr,int portOK)
{
    asynStatus status;  

    status = pasynManager->getAddr(pasynUser,paddr);
    if(status!=asynSuccess) return status;
    if(*paddr>=-1 && *paddr<NCHANNELS) return asynSuccess;
    if(!portOK && *paddr>=0) return asynSuccess;
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "%s addr %d is illegal; Must be >= %d and < %d\n",
        pcachePvt->portName,*paddr,
        (portOK ? -1 : 0),NCHANNELS);
    return asynError;
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
    cachePvt   *pcachePvt = (cachePvt *)drvPvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pcachePvt,pasynUser,&addr,1);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:connect addr %d\n",pcachePvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
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
    int        addr;
    asynStatus status;  

    status = getAddr(pcachePvt,pasynUser,&addr,1);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s cacheDriver:disconnect addr %d\n",pcachePvt->portName,addr);
    if(addr>=0) {
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
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

static asynStatus cacheWrite(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value)
{
    cachePvt   *pcachePvt = (cachePvt *)drvPvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pcachePvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
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
    pcachePvt->value[addr] = value;
    asynPrint(pasynUser,ASYN_TRACEIO_DRIVER,
        "%s addr %d write %d\n",pcachePvt->portName,addr,value);
    return asynSuccess;
}

static asynStatus cacheRead(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value)
{
    cachePvt *pcachePvt = (cachePvt *)drvPvt;
    int        addr;
    asynStatus status;  

    status = getAddr(pcachePvt,pasynUser,&addr,0);
    if(status!=asynSuccess) return status;
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
    *value = pcachePvt->value[addr];
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
