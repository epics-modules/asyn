/* asynShellCommands.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Author: Marty Kraimer */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <epicsEvent.h>
#include <epicsExport.h>
#include <iocsh.h>
#include "asynDriver.h"

#define epicsExportSharedSymbols

#include "asynShellCommands.h"

typedef struct setOptionArgs {
    const char     *key;
    const char     *val;
    asynCommon     *pasynCommon;
    void           *drvPvt;
    epicsEventId   done;
}setOptionArgs;

typedef struct showOptionArgs {
    const char     *key;
    asynCommon     *pasynCommon;
    void           *drvPvt;
    epicsEventId   done;
}showOptionArgs;

static void setOption(asynUser *pasynUser)
{
    setOptionArgs *poptionargs = (setOptionArgs *)pasynUser->userPvt;
    asynStatus status;

    status = poptionargs->pasynCommon->setOption(poptionargs->drvPvt,
            pasynUser,poptionargs->key,poptionargs->val);
    if(status!=asynSuccess) 
        printf("setOption failed %s\n",pasynUser->errorMessage);
    epicsEventSignal(poptionargs->done);
}

int epicsShareAPI
 asynSetOption(const char *portName, int addr, const char *key, const char *val)
{
    asynInterface *pasynInterface;
    setOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) || (val == NULL)) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(setOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.val = val;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        printf("queueRequest failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

static void showOption(asynUser *pasynUser)
{
    showOptionArgs *poptionargs = (showOptionArgs *)pasynUser->userPvt;
    asynStatus status;
    char val[100];

    pasynUser->errorMessage[0] = '\0';
    status = poptionargs->pasynCommon->getOption(poptionargs->drvPvt,
        pasynUser,poptionargs->key,val,sizeof(val));
    if(status!=asynSuccess) {
        printf("getOption failed %s\n",pasynUser->errorMessage);
    } else {
        printf("%s=%s\n",poptionargs->key,val);
    }
    epicsEventSignal(poptionargs->done);
}

int epicsShareAPI
 asynShowOption(const char *portName, int addr,const char *key)
{
    asynInterface *pasynInterface;
    showOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) ) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(showOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return 1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,0);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,0,0.0);
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

static const iocshArg asynReportArg0 = {"filename", iocshArgString};
static const iocshArg asynReportArg1 = {"level", iocshArgInt};
static const iocshArg *const asynReportArgs[] = {&asynReportArg0,&asynReportArg1};
static const iocshFuncDef asynReportDef = {"asynReport", 2, asynReportArgs};
int epicsShareAPI
 asynReport(const char *filename, int level)
{
    FILE *fp;

    if(!filename || filename[0]==0) {
        fp = stdout;
    } else {
        fp = fopen(filename,"w+");
        if(!fp) {
            printf("fopen failed %s\n",strerror(errno));
            return -1;
        }
    }
    pasynManager->report(fp,level);
    if(fp!=stdout)  {
        int status;

        errno = 0;
        status = fclose(fp);
        if(status) fprintf(stderr,"asynReport fclose error %s\n",strerror(errno));
    }
    return 0;
}
static void asynReportCall(const iocshArgBuf * args) {
    asynReport(args[0].sval,args[1].ival);
}

static const iocshArg asynSetOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynSetOptionArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetOptionArg2 = {"key", iocshArgString};
static const iocshArg asynSetOptionArg3 = {"value", iocshArgString};
static const iocshArg *const asynSetOptionArgs[] = {
              &asynSetOptionArg0, &asynSetOptionArg1,
              &asynSetOptionArg2,&asynSetOptionArg3};
static const iocshFuncDef asynSetOptionDef = {"asynSetOption", 4, asynSetOptionArgs};
static void asynSetOptionCall(const iocshArgBuf * args) {
    asynSetOption(args[0].sval,args[1].ival,args[2].sval,args[3].sval);
}

static const iocshArg asynShowOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynShowOptionArg1 = {"addr", iocshArgString};
static const iocshArg asynShowOptionArg2 = {"key", iocshArgString};
static const iocshArg *const asynShowOptionArgs[] = {
              &asynShowOptionArg0, &asynShowOptionArg1,&asynShowOptionArg2};
static const iocshFuncDef asynShowOptionDef = {"asynShowOption", 3, asynShowOptionArgs};
static void asynShowOptionCall(const iocshArgBuf * args) {
    asynShowOption(args[0].sval,args[1].ival,args[2].sval);
}

static const iocshArg asynSetTraceMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceMaskArgs[] = {
    &asynSetTraceMaskArg0,&asynSetTraceMaskArg1,&asynSetTraceMaskArg2};
static const iocshFuncDef asynSetTraceMaskDef =
    {"asynSetTraceMask", 3, asynSetTraceMaskArgs};
int epicsShareAPI
 asynSetTraceMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynTrace->setTraceMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceMask(portName,addr,mask);
}

static const iocshArg asynSetTraceIOMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceIOMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceIOMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceIOMaskArgs[] = {
    &asynSetTraceIOMaskArg0,&asynSetTraceIOMaskArg1,&asynSetTraceIOMaskArg2};
static const iocshFuncDef asynSetTraceIOMaskDef =
    {"asynSetTraceIOMask", 3, asynSetTraceIOMaskArgs};
int epicsShareAPI
 asynSetTraceIOMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynTrace->setTraceIOMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceIOMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceIOMask(portName,addr,mask);
}

static const iocshArg asynEnableArg0 = {"portName", iocshArgString};
static const iocshArg asynEnableArg1 = {"addr", iocshArgInt};
static const iocshArg asynEnableArg2 = {"yesNo", iocshArgInt};
static const iocshArg *const asynEnableArgs[] = {
    &asynEnableArg0,&asynEnableArg1,&asynEnableArg2};
static const iocshFuncDef asynEnableDef =
    {"asynEnable", 3, asynEnableArgs};
int epicsShareAPI
 asynEnable(const char *portName,int addr,int yesNo)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynManager->enable(pasynUser,yesNo);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynEnableCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int yesNo = args[2].ival;
    asynEnable(portName,addr,yesNo);
}

static const iocshArg asynAutoConnectArg0 = {"portName", iocshArgString};
static const iocshArg asynAutoConnectArg1 = {"addr", iocshArgInt};
static const iocshArg asynAutoConnectArg2 = {"yesNo", iocshArgInt};
static const iocshArg *const asynAutoConnectArgs[] = {
    &asynAutoConnectArg0,&asynAutoConnectArg1,&asynAutoConnectArg2};
static const iocshFuncDef asynAutoConnectDef =
    {"asynAutoConnect", 3, asynAutoConnectArgs};
int epicsShareAPI
 asynAutoConnect(const char *portName,int addr,int yesNo)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynManager->autoConnect(pasynUser,yesNo);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynAutoConnectCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int yesNo = args[2].ival;
    asynAutoConnect(portName,addr,yesNo);
}

static void asyn(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynReportDef,asynReportCall);
    iocshRegister(&asynSetOptionDef,asynSetOptionCall);
    iocshRegister(&asynShowOptionDef,asynShowOptionCall);
    iocshRegister(&asynSetTraceMaskDef,asynSetTraceMaskCall);
    iocshRegister(&asynSetTraceIOMaskDef,asynSetTraceIOMaskCall);
    iocshRegister(&asynEnableDef,asynEnableCall);
    iocshRegister(&asynAutoConnectDef,asynAutoConnectCall);
}
epicsExportRegistrar(asyn);
