/*testManagerDriver.c */
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
#include <iocsh.h>

#include <asynDriver.h>
#include <epicsExport.h>

#define NUM_DEVICES 2

typedef struct testManagerPvt {
    int	          deviceConnected[NUM_DEVICES];
    const char    *portName;
    int           connected;
    int           multiDevice;
    asynInterface common;
}testManagerPvt;
    
/* init routine */
static int testManagerDriverInit(const char *dn, int canBlock,
    int noAutoConnect,int multiDevice);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

static int testManagerDriverInit(const char *dn, int canBlock,
    int noAutoConnect,int multiDevice)
{
    testManagerPvt    *ptestManagerPvt;
    char       *portName;
    asynStatus status;
    size_t     nbytes;
    int        attributes;

    nbytes = sizeof(testManagerPvt) + strlen(dn) + 1;
    ptestManagerPvt = callocMustSucceed(nbytes,sizeof(char),"testManagerDriverInit");
    portName = (char *)(ptestManagerPvt + 1);
    strcpy(portName,dn);
    ptestManagerPvt->portName = portName;
    ptestManagerPvt->multiDevice = multiDevice;
    ptestManagerPvt->common.interfaceType = asynCommonType;
    ptestManagerPvt->common.pinterface  = (void *)&asyn;
    ptestManagerPvt->common.drvPvt = ptestManagerPvt;
    attributes = 0;
    if(multiDevice) attributes |= ASYN_MULTIDEVICE;
    if(canBlock) attributes|=ASYN_CANBLOCK;
    status = pasynManager->registerPort(portName,attributes,!noAutoConnect,0,0);
    if(status!=asynSuccess) {
        printf("testManagerDriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&ptestManagerPvt->common);
    if(status!=asynSuccess){
        printf("testManagerDriverInit registerInterface failed\n");
        return 0;
    }
    return(0);
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    testManagerPvt *ptestManagerPvt = (testManagerPvt *)drvPvt;
    int i,n;

    fprintf(fp,"    testManagerDriver. "
        "multiDevice:%s connected:%s\n",
        (ptestManagerPvt->multiDevice ? "Yes" : "No"),
        (ptestManagerPvt->connected ? "Yes" : "No"));
    n = (ptestManagerPvt->multiDevice) ? NUM_DEVICES : 1;
    for(i=0;i<n;i++) {
       fprintf(fp,"        device %d connected:%s\n",
            i,
            (ptestManagerPvt->deviceConnected[i] ? "Yes" : "No"));
    }
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    testManagerPvt    *ptestManagerPvt = (testManagerPvt *)drvPvt;
    int        addr;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s testManagerDriver:connect addr %d\n",ptestManagerPvt->portName,addr);
    if(!ptestManagerPvt->multiDevice) {
        if(ptestManagerPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s testManagerDriver:connect port already connected\n",
               ptestManagerPvt->portName);
            return asynError;
        }
        ptestManagerPvt->connected = 1;
        ptestManagerPvt->deviceConnected[0] = 1;
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr<=-1) {
        if(ptestManagerPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s testManagerDriver:connect port already connected\n",
               ptestManagerPvt->portName);
            return asynError;
        }
        ptestManagerPvt->connected = 1;
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s testManagerDriver:connect illegal addr %d\n",ptestManagerPvt->portName,addr);
        return asynError;
    }
    if(ptestManagerPvt->deviceConnected[addr]) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s testManagerDriver:connect device %d already connected\n",
            ptestManagerPvt->portName,addr);
        return asynError;
    }
    ptestManagerPvt->deviceConnected[addr] = 1;
    pasynManager->exceptionConnect(pasynUser);
    return(asynSuccess);
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    testManagerPvt    *ptestManagerPvt = (testManagerPvt *)drvPvt;
    int        addr;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s testManagerDriver:disconnect addr %d\n",ptestManagerPvt->portName,addr);
    if(!ptestManagerPvt->multiDevice) {
        if(!ptestManagerPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s testManagerDriver:disconnect port not connected\n",
               ptestManagerPvt->portName);
            return asynError;
        }
        ptestManagerPvt->connected = 0;
        ptestManagerPvt->deviceConnected[0] = 0;
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(addr<=-1) {
        if(!ptestManagerPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s testManagerDriver:disconnect port not connected\n",
               ptestManagerPvt->portName);
            return asynError;
        }
        ptestManagerPvt->connected = 0;
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s testManagerDriver:disconnect illegal addr %d\n",ptestManagerPvt->portName,addr);
        return asynError;
    }
    if(!ptestManagerPvt->deviceConnected[addr]) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s testManagerDriver:disconnect device %d not connected\n",
            ptestManagerPvt->portName,addr);
        return asynError;
    }
    ptestManagerPvt->deviceConnected[addr] = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return(asynSuccess);
}

/* register testManagerDriverInit*/
static const iocshArg testManagerDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg testManagerDriverInitArg1 = { "canBlock", iocshArgInt };
static const iocshArg testManagerDriverInitArg2 = { "disable auto-connect", iocshArgInt };
static const iocshArg testManagerDriverInitArg3 = { "multiDevice", iocshArgInt };
static const iocshArg *testManagerDriverInitArgs[] = {
    &testManagerDriverInitArg0,&testManagerDriverInitArg1,
    &testManagerDriverInitArg2,&testManagerDriverInitArg3};
static const iocshFuncDef testManagerDriverInitFuncDef = {
    "testManagerDriverInit", 4, testManagerDriverInitArgs};
static void testManagerDriverInitCallFunc(const iocshArgBuf *args)
{
    testManagerDriverInit(args[0].sval,args[1].ival,args[2].ival,args[3].ival);
}

static void testManagerDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&testManagerDriverInitFuncDef, testManagerDriverInitCallFunc);
    }
}
epicsExportRegistrar(testManagerDriverRegister);
