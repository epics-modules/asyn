/* testManager.c */
/*
 *      Author: Marty Kraimer
 *      Date:   17SEP2004
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
#include <epicsThread.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsStdio.h>
#include <epicsAssert.h>
#include <asynDriver.h>
#include <asynOctet.h>
#include <iocsh.h>
#include <registryFunction.h>
#include <epicsExport.h>

typedef enum {connect,testLock,testCancelRequest,quit}testType;

typedef struct threadInfo threadInfo;
typedef struct cmdInfo {
    asynUser   *pasynUser;
    asynCommon *pasynCommon;
    void       *asynCommonPvt;
    threadInfo *pthreadInfo;
    testType   test;
    epicsEventId callbackDone;
    char       message[80];
    asynUser   *pasynUserBusy;
}cmdInfo;

struct threadInfo {
    char          *threadName;
    cmdInfo       *pcmdInfo;
    epicsEventId  work;
    epicsEventId  done;
    epicsThreadId tid;
};

int checkStatus(asynStatus status,threadInfo *pthreadInfo,const char *message);
static void workCallback(asynUser *pasynUser);
static void timeoutCallback(asynUser *pasynUser);
static void busyCallback(asynUser *pasynUser);
static void workThread(threadInfo *pthreadInfo);

int checkStatus(asynStatus status,threadInfo *pthreadInfo,const char *message)
{
    cmdInfo *pcmdInfo = pthreadInfo->pcmdInfo;

    if(status==asynSuccess) return 0;
    printf("%s failure %s error %s\n",
        pthreadInfo->threadName,message,pcmdInfo->pasynUser->errorMessage);
    return 1;
}

static void connectCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;
    int        isConnected = 0;

    printf("%s connectCallback calling connect\n",pthreadInfo->threadName);
    status = pcmdInfo->pasynCommon->connect(pcmdInfo->asynCommonPvt,pasynUser);
    if(status!=asynSuccess) {
        printf("%s connect failed %s\n",
            pthreadInfo->threadName,pasynUser->errorMessage);
    }
    epicsEventSignal(pcmdInfo->callbackDone);
}

static void connectTest(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;

    printf("%s connect queueRequest\n", pthreadInfo->threadName);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0);
    if(checkStatus(status,pthreadInfo,"connect")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
}

static void lockCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    printf("%s %s lockCallback\n",threadName,pcmdInfo->message);
    epicsEventSignal(pcmdInfo->callbackDone);
    epicsThreadSleep(.01);
}

static void lockTest(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;

    status = pasynManager->lock(pasynUser);
    if(checkStatus(status,pthreadInfo,"testLock")) return;
    printf("%s %s first queueRequest\n",
        pthreadInfo->threadName,pcmdInfo->message);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testLock")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
    epicsThreadSleep(.1);
    printf("%s %s second queueRequest\n",
        pthreadInfo->threadName,pcmdInfo->message);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testLock")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
    status = pasynManager->unlock(pasynUser);
    if(checkStatus(status,pthreadInfo,"testLock")) return;
}

static void cancelCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    printf("%s %s cancelCallback\n",threadName,pcmdInfo->message);
    epicsThreadSleep(.04);
    epicsEventSignal(pcmdInfo->callbackDone);
}

static void cancelTest(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;
    int        wasQueued;

    printf("%s %s queueRequest expect cancelRequest returns wasQueued\n",
        pthreadInfo->threadName,pcmdInfo->message);
    status = pasynManager->queueRequest(pcmdInfo->pasynUserBusy,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    status = pasynManager->cancelRequest(pasynUser,&wasQueued);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    printf("%s %s cancelRequest wasQueued %d\n",
        pthreadInfo->threadName,pcmdInfo->message,wasQueued);
    epicsThreadSleep(.01);
    if(!wasQueued) epicsEventMustWait(pcmdInfo->callbackDone);
    printf("%s %s queueRequest expect cancelRequest finds callback active\n",
        pthreadInfo->threadName,pcmdInfo->message);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.02);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    epicsThreadSleep(.03);
    status = pasynManager->cancelRequest(pasynUser,&wasQueued);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    printf("%s %s cancelRequest wasQueued %d\n",
        pthreadInfo->threadName,pcmdInfo->message,wasQueued);
    if(!wasQueued) epicsEventMustWait(pcmdInfo->callbackDone);
    epicsThreadSleep(.01);
}

static void timeoutCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;

    printf("%s %s timeoutCallback\n",pthreadInfo->threadName,pcmdInfo->message);
    epicsThreadSleep(.01);
    epicsEventSignal(pcmdInfo->callbackDone);
}

static void busyCallback(asynUser *pasynUser)
{
    printf("busy callback\n");
    epicsThreadSleep(.01);
}

static void workCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    switch(pcmdInfo->test) {
    case connect:            connectCallback(pasynUser); break;
    case testLock:           lockCallback(pasynUser);    break;
    case testCancelRequest:  cancelCallback(pasynUser);  break;
    default:
        printf("%s workCallback illegal test %d\n",threadName,pcmdInfo->test);
    }
}

static void workThread(threadInfo *pthreadInfo)
{
    cmdInfo    *pcmdInfo;
    asynUser   *pasynUser;

    while(1) {
        epicsEventMustWait(pthreadInfo->work);
        pcmdInfo = pthreadInfo->pcmdInfo;
        pasynUser = pcmdInfo->pasynUser;
        if(pcmdInfo->test==quit) break;
        switch(pcmdInfo->test) {
        case connect:            connectTest(pasynUser);break;
        case testLock:           lockTest(pasynUser);   break;
        case testCancelRequest:  cancelTest(pasynUser); break;
        default:
            printf("%s workThread illegal test %d\n",
                pthreadInfo->threadName,pcmdInfo->test);
        }
        printf("%s %s all done\n",
            pthreadInfo->threadName,pcmdInfo->message);
        epicsEventSignal(pthreadInfo->done);
    }
    epicsEventSignal(pthreadInfo->done);
}

static int testInit(const char *port,int addr,
    cmdInfo **ppcmdInfo, threadInfo **ppthreadInfo,int ind)
{
    cmdInfo    *pcmdInfo;
    threadInfo *pthreadInfo;
    asynUser   *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;

    pcmdInfo = (cmdInfo *)pasynManager->memMalloc(sizeof(cmdInfo));
    memset(pcmdInfo,0,sizeof(cmdInfo));
    *ppcmdInfo = pcmdInfo;
    pthreadInfo = (threadInfo *)pasynManager->memMalloc(sizeof(threadInfo)+5);
    memset(pthreadInfo,0,sizeof(threadInfo));
    *ppthreadInfo = pthreadInfo;
    pthreadInfo->threadName = (char *)(pthreadInfo + 1);
    sprintf(pthreadInfo->threadName,"thread%1.1d",ind);
    pcmdInfo->callbackDone = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->work = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->done = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->tid = epicsThreadCreate(pthreadInfo->threadName,
        epicsThreadPriorityHigh,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        (EPICSTHREADFUNC)workThread,pthreadInfo);
    if(!pthreadInfo->tid) {
        printf("epicsThreadCreate failed\n");
        return -1;
    }
    pcmdInfo->pasynUser = pasynManager->createAsynUser(
        workCallback,timeoutCallback);
    pasynUser = pcmdInfo->pasynUser;
    pasynUser->userPvt = pcmdInfo;
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        return -1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if(!pasynInterface) {
        printf("can't find asynCommon\n");
        return -1;
    }
    pcmdInfo->pasynCommon = (asynCommon *) pasynInterface->pinterface;
    pcmdInfo->asynCommonPvt = pasynInterface->drvPvt;
    pcmdInfo->pasynUserBusy = pasynManager->createAsynUser(busyCallback,0);
    pasynUser = pcmdInfo->pasynUserBusy;
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        return -1;
    }
    return 0;
}

static void testManager(const char *port,int addr)
{
    cmdInfo    *pacmdInfo[2],*pcmdInfo;
    threadInfo *pathreadInfo[2],*pthreadInfo;
    asynUser   *pasynUser;
    asynStatus status;
    int        isConnected = 0;
    int        isEnabled = 0;
    int        yesNo;
    int        i;

    pasynUser = pasynManager->createAsynUser(workCallback,0);
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        return;
    }
    if((testInit(port,addr,&pacmdInfo[0],&pathreadInfo[0],0))) return;
    if((testInit(port,addr,&pacmdInfo[1],&pathreadInfo[1],1))) return;
    /* if port is not connected try to connect*/
    status = pasynManager->isConnected(pasynUser,&isConnected);
    if(status!=asynSuccess) {
        printf("isConnected failed %s\n",
            pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return;
    }
    if(!isConnected) {
        asynUser *pasynUserSave;

        pcmdInfo = pacmdInfo[0];
        pthreadInfo = pathreadInfo[0];
        pcmdInfo->pthreadInfo = pthreadInfo;
        pthreadInfo->pcmdInfo = pcmdInfo;
        pcmdInfo->test = connect;
        pasynUserSave = pcmdInfo->pasynUser;
        pcmdInfo->pasynUser = pasynUser;
        pasynUser->userPvt = pcmdInfo;
        printf("connect queueRequest\n");
        status = pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0);
        if(status!=asynSuccess) {
            printf("port connect queueRequest failed\n");
            return;
        }
        epicsEventMustWait(pcmdInfo->callbackDone);
        status = pasynManager->isConnected(pasynUser,&isConnected);
        if(status!=asynSuccess) {
            printf("isConnected failed %s\n",
                pasynUser->errorMessage);
            return;
        }
        if(!isConnected) {
            printf("not connected\n");
            return;
        }
        status = pasynManager->isEnabled(pasynUser,&isEnabled);
        if(status!=asynSuccess) {
            printf("isEnabled failed %s\n",
                pasynUser->errorMessage);
            return;
        }
        if(!isEnabled) {
            printf("not enabled\n");
            return;
        }
        pcmdInfo->pasynUser = pasynUserSave;
        
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status) {
        printf("freeAsynUser failed %s\n",pasynUser->errorMessage);
        return;
    }

    /* test lock */
    printf("test lock/unlock. thread0 first\n");
    for(i=0; i<2; i++) {
        pcmdInfo = pacmdInfo[i];
        pthreadInfo = pathreadInfo[i];
        pcmdInfo->pthreadInfo = pthreadInfo;
        pthreadInfo->pcmdInfo = pcmdInfo;
        pcmdInfo->test = testLock;
        strcpy(pcmdInfo->message,"lock/unlock");
        epicsEventSignal(pthreadInfo->work);
        epicsThreadSleep(.01);
    }
    for(i=0; i<2; i++) {
        epicsEventMustWait(pathreadInfo[i]->done);
    }
    printf("test lock/unlock. thread1 first\n");
    for(i=1; i>=0; i--) {
        epicsEventSignal(pathreadInfo[i]->work);
        epicsThreadSleep(.01);
    }
    for(i=0; i<2; i++) {
        epicsEventMustWait(pathreadInfo[i]->done);
    }
    pasynUser = pacmdInfo[0]->pasynUser;
    pasynManager->canBlock(pasynUser,&yesNo);
    if(yesNo) {
        printf("test cancelRequest\n");
        for(i=0; i<2; i++) {
            pacmdInfo[i]->test = testCancelRequest;
            strcpy(pacmdInfo[i]->message,"cancelRequest");
            epicsEventSignal(pathreadInfo[i]->work);
        }
        for(i=0; i<2; i++) {
            epicsEventMustWait(pathreadInfo[i]->done);
        }
    }
    /*now terminate and clean up*/
    for(i=0; i<2; i++) {
        pacmdInfo[i]->test = quit;
        strcpy(pacmdInfo[i]->message,"quit");
        epicsEventSignal(pathreadInfo[i]->work);
    }
    for(i=0; i<2; i++) {
        epicsEventMustWait(pathreadInfo[i]->done);
    }
    for(i=0; i<2; i++) {
        pthreadInfo = pathreadInfo[i];
        pcmdInfo = pacmdInfo[i];
        pasynUser = pacmdInfo[i]->pasynUser;
        epicsEventDestroy(pthreadInfo->done);
        epicsEventDestroy(pthreadInfo->work);
        epicsEventDestroy(pcmdInfo->callbackDone);
        status = pasynManager->freeAsynUser(pasynUser);
        if(status) {
            printf("freeAsynUser failed %s\n",pasynUser->errorMessage);
            return;
        }
        status = pasynManager->freeAsynUser(pcmdInfo->pasynUserBusy);
        if(status) {
            printf("freeAsynUserBusy failed %s\n",
                pcmdInfo->pasynUserBusy->errorMessage);
            return;
        }
        pasynManager->memFree(pthreadInfo,sizeof(threadInfo));
        pasynManager->memFree(pcmdInfo,sizeof(cmdInfo));
    }
}

static const iocshArg testManagerArg0 = {"port", iocshArgString};
static const iocshArg testManagerArg1 = {"addr", iocshArgInt};
static const iocshArg *const testManagerArgs[] = {
    &testManagerArg0,&testManagerArg1};
static const iocshFuncDef testManagerDef = {"testManager", 2, testManagerArgs};
static void testManagerCall(const iocshArgBuf * args) {
    testManager(args[0].sval,args[1].ival);
}

static void testManagerRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&testManagerDef,testManagerCall);
}
epicsExportRegistrar(testManagerRegister);
