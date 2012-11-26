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
#include <errno.h>

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

typedef enum {connect,testBlock,testCancelRequest,quit}testType;

typedef struct threadInfo threadInfo;
typedef struct cmdInfo {
    FILE       *file;
    asynUser   *pasynUser;
    asynCommon *pasynCommon;
    void       *asynCommonPvt;
    threadInfo *pthreadInfo;
    testType   test;
    epicsEventId callbackDone;
    char       message[80];
}cmdInfo;

struct threadInfo {
    char          *portName;
    int           addr;
    size_t        size;
    char          *threadName;
    cmdInfo       *pcmdInfo;
    epicsEventId  work;
    epicsEventId  done;
    epicsThreadId tid;
};

int checkStatus(asynStatus status,threadInfo *pthreadInfo,const char *message)
{
    cmdInfo *pcmdInfo = pthreadInfo->pcmdInfo;

    if(status==asynSuccess) return 0;
    fprintf(pcmdInfo->file,"%s failure %s error %s\n",
        pthreadInfo->threadName,message,pcmdInfo->pasynUser->errorMessage);
    return 1;
}

static void connectCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;

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

    fprintf(pcmdInfo->file,"%s connect queueRequest\n", pthreadInfo->threadName);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0);
    if(checkStatus(status,pthreadInfo,"connect")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
}

static void blockCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    fprintf(pcmdInfo->file,"%s %s blockCallback\n",threadName,pcmdInfo->message);
    epicsEventSignal(pcmdInfo->callbackDone);
    epicsThreadSleep(.01);
}

static void blockTest(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;

    status = pasynManager->blockProcessCallback(pasynUser,0);
    if(checkStatus(status,pthreadInfo,"testBlock")) return;
    fprintf(pcmdInfo->file,"%s %s first queueRequest\n",
        pthreadInfo->threadName,pcmdInfo->message);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testBlock")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
    epicsThreadSleep(.1);
    fprintf(pcmdInfo->file,"%s %s second queueRequest\n",
        pthreadInfo->threadName,pcmdInfo->message);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testBlock")) return;
    epicsEventMustWait(pcmdInfo->callbackDone);
    status = pasynManager->unblockProcessCallback(pasynUser,0);
    if(checkStatus(status,pthreadInfo,"testBlock")) return;
}

static void busyCallback(asynUser *pasynUser)
{
    asynStatus status;
    status = pasynManager->freeAsynUser(pasynUser);
    if(status) {
        printf("freeAsynUserBusy failed %s\n",
            pasynUser->errorMessage);
    }
    epicsThreadSleep(.03);
}

static void startBusy(threadInfo *pthreadInfo)
{
    asynUser   *pasynUserBusy;
    asynStatus status;

    pasynUserBusy = pasynManager->createAsynUser(busyCallback,0);
    status = pasynManager->connectDevice(
        pasynUserBusy,pthreadInfo->portName,pthreadInfo->addr);
    if(status!=asynSuccess) {
        printf("%s busy failure connectDevice %s\n",
            pthreadInfo->threadName,pasynUserBusy->errorMessage);
    }
    status = pasynManager->queueRequest(pasynUserBusy,asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        printf("%s busy failure queueRequest %s\n",
            pthreadInfo->threadName,pasynUserBusy->errorMessage);
    }
}

static void cancelCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    fprintf(pcmdInfo->file,"%s %s cancelCallback\n",threadName,pcmdInfo->message);
    epicsThreadSleep(.04);
    epicsEventSignal(pcmdInfo->callbackDone);
}

static void cancelTest(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    asynStatus status;
    int        wasQueued;

    fprintf(pcmdInfo->file,"%s %s  cancelRequest should remove\n",
        pthreadInfo->threadName,pcmdInfo->message);
    startBusy(pthreadInfo);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    epicsThreadSleep(.01);
    status = pasynManager->cancelRequest(pasynUser,&wasQueued);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    fprintf(pcmdInfo->file,"%s %s cancelRequest wasQueued %d\n",
        pthreadInfo->threadName,pcmdInfo->message,wasQueued);
    if(!wasQueued) epicsEventMustWait(pcmdInfo->callbackDone);
    epicsThreadSleep(.04); /*wait for busy to complete*/

    fprintf(pcmdInfo->file,"%s %s should find callback active\n",
        pthreadInfo->threadName,pcmdInfo->message);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.05);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    epicsThreadSleep(.01);
    status = pasynManager->cancelRequest(pasynUser,&wasQueued);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    fprintf(pcmdInfo->file,"%s %s cancelRequest wasQueued %d\n",
        pthreadInfo->threadName,pcmdInfo->message,wasQueued);
    if(!wasQueued) epicsEventMustWait(pcmdInfo->callbackDone);

    fprintf(pcmdInfo->file,"%s %s should find timeout active\n",
        pthreadInfo->threadName,pcmdInfo->message);
    startBusy(pthreadInfo);
    epicsEventTryWait(pcmdInfo->callbackDone);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.02);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    epicsThreadSleep(.03);
    status = pasynManager->cancelRequest(pasynUser,&wasQueued);
    if(checkStatus(status,pthreadInfo,"testCancelRequest")) return;
    fprintf(pcmdInfo->file,"%s %s cancelRequest wasQueued %d\n",
        pthreadInfo->threadName,pcmdInfo->message,wasQueued);
    if(!wasQueued) epicsEventMustWait(pcmdInfo->callbackDone);
}

static void timeoutCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;

    fprintf(pcmdInfo->file,"%s %s timeoutCallback\n",pthreadInfo->threadName,pcmdInfo->message);
    epicsThreadSleep(.04);
    epicsEventSignal(pcmdInfo->callbackDone);
}

static void workCallback(asynUser *pasynUser)
{
    cmdInfo    *pcmdInfo = (cmdInfo *)pasynUser->userPvt;
    threadInfo *pthreadInfo = pcmdInfo->pthreadInfo;
    char       *threadName = pthreadInfo->threadName;

    switch(pcmdInfo->test) {
    case connect:            connectCallback(pasynUser); break;
    case testBlock:           blockCallback(pasynUser);    break;
    case testCancelRequest:  cancelCallback(pasynUser);  break;
    default:
        fprintf(pcmdInfo->file,"%s workCallback illegal test %d\n",threadName,pcmdInfo->test);
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
        case testBlock:           blockTest(pasynUser);   break;
        case testCancelRequest:  cancelTest(pasynUser); break;
        default:
            fprintf(pcmdInfo->file,"%s workThread illegal test %d\n",
                pthreadInfo->threadName,pcmdInfo->test);
        }
        fprintf(pcmdInfo->file,"%s %s all done\n",
            pthreadInfo->threadName,pcmdInfo->message);
        epicsEventSignal(pthreadInfo->done);
    }
    epicsEventSignal(pthreadInfo->done);
}

static const char *thread = "thread";
static int testInit(const char *port,int addr,
    cmdInfo **ppcmdInfo, threadInfo **ppthreadInfo,int ind,FILE *file)
{
    cmdInfo    *pcmdInfo;
    threadInfo *pthreadInfo;
    asynUser   *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;
    size_t     size;

    pcmdInfo = (cmdInfo *)pasynManager->memMalloc(sizeof(cmdInfo));
    memset(pcmdInfo,0,sizeof(cmdInfo));
    *ppcmdInfo = pcmdInfo;
    size = sizeof(threadInfo) + (strlen(port)+ 1) + (strlen(thread) + 2);
    pthreadInfo = (threadInfo *)pasynManager->memMalloc(size);
    memset(pthreadInfo,0,sizeof(threadInfo));
    pthreadInfo->size = size;
    *ppthreadInfo = pthreadInfo;
    pthreadInfo->portName = (char *)(pthreadInfo +1);
    strcpy(pthreadInfo->portName,port);
    pthreadInfo->threadName = pthreadInfo->portName
                              + strlen(pthreadInfo->portName) + 1;
    sprintf(pthreadInfo->threadName,"%s%1.1d",thread,ind);
    pcmdInfo->callbackDone = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->work = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->done = epicsEventMustCreate(epicsEventEmpty);
    pthreadInfo->tid = epicsThreadCreate(pthreadInfo->threadName,
        epicsThreadPriorityHigh,
        epicsThreadGetStackSize(epicsThreadStackMedium),
        (EPICSTHREADFUNC)workThread,pthreadInfo);
    if(!pthreadInfo->tid) {
        fprintf(pcmdInfo->file,"epicsThreadCreate failed\n");
        return -1;
    }
    pcmdInfo->pasynUser = pasynManager->createAsynUser(
        workCallback,timeoutCallback);
    pasynUser = pcmdInfo->pasynUser;
    pasynUser->userPvt = pcmdInfo;
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        fprintf(pcmdInfo->file,"connectDevice failed %s\n",pasynUser->errorMessage);
        return -1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if(!pasynInterface) {
        fprintf(pcmdInfo->file,"can't find asynCommon\n");
        return -1;
    }
    pcmdInfo->pasynCommon = (asynCommon *) pasynInterface->pinterface;
    pcmdInfo->asynCommonPvt = pasynInterface->drvPvt;
    pcmdInfo->file = file;
    pcmdInfo->pthreadInfo = pthreadInfo;
    pthreadInfo->pcmdInfo = pcmdInfo;
    return 0;
}

static void testManager(const char *port,int addr,FILE *file)
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
    if((testInit(port,addr,&pacmdInfo[0],&pathreadInfo[0],0,file))) return;
    if((testInit(port,addr,&pacmdInfo[1],&pathreadInfo[1],1,file))) return;
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

    pasynUser = pacmdInfo[0]->pasynUser;
    pasynManager->canBlock(pasynUser,&yesNo);
    if(!yesNo) {
        fprintf(file,"\nport %s addr %d cant block so no block or cancel test\n",port,addr);
    } else {
        fprintf(file,"\nport %s addr %d\n",port,addr);
        /* test lock */
        fprintf(file,"test blockProcessCallback/unblockProcessCallback. thread0 first\n");
        for(i=0; i<2; i++) {
            pcmdInfo = pacmdInfo[i];
            pthreadInfo = pathreadInfo[i];
            pcmdInfo->test = testBlock;
            strcpy(pcmdInfo->message,"blockProcessCallback/unblockProcessCallback");
            epicsEventSignal(pthreadInfo->work);
            epicsThreadSleep(.01);
        }
        for(i=0; i<2; i++) {
            epicsEventMustWait(pathreadInfo[i]->done);
        }
        fprintf(file,"test unblockProcessCallback/unblockProcessCallback. thread1 first\n");
        for(i=1; i>=0; i--) {
            epicsEventSignal(pathreadInfo[i]->work);
            epicsThreadSleep(.01);
        }
        for(i=0; i<2; i++) {
            epicsEventMustWait(pathreadInfo[i]->done);
        }
        fprintf(file,"test cancelRequest\n");
        for(i=0; i<1; i++) {
            pacmdInfo[i]->test = testCancelRequest;
            strcpy(pacmdInfo[i]->message,"cancelRequest");
            epicsEventSignal(pathreadInfo[i]->work);
        }
        for(i=0; i<1; i++) {
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
        pasynManager->memFree(pthreadInfo,pthreadInfo->size);
        pasynManager->memFree(pcmdInfo,sizeof(cmdInfo));
    }
}

static const iocshArg testManagerArg0 = {"port", iocshArgString};
static const iocshArg testManagerArg1 = {"addr", iocshArgInt};
static const iocshArg testManagerArg2 = {"reportFile", iocshArgString};
static const iocshArg *const testManagerArgs[] = {
    &testManagerArg0,&testManagerArg1,&testManagerArg2};
static const iocshFuncDef testManagerDef = {"testManager", 3, testManagerArgs};
static void testManagerCall(const iocshArgBuf * args)
{
    char *portName = args[0].sval;
    int   addr = args[1].ival;
    char *filename = args[2].sval;
    FILE *file = stdout;

    if(filename && strlen(filename)>0) {
        file = fopen(filename,"w");
        if(!file) {
            printf("could not open %s %s\n",filename,strerror(errno));
            return;
        }
    }
    testManager(portName,addr,file);
    if(file!=stdout) fclose(file);
}

static const iocshArg testManagerAllPortsArg0 = {"reportFile", iocshArgString};
static const iocshArg *const testManagerAllPortsArgs[] = {
    &testManagerAllPortsArg0};
static const iocshFuncDef testManagerAllPortsDef = {"testManagerAllPorts", 1, testManagerAllPortsArgs};
static void testManagerAllPortsCall(const iocshArgBuf * args)
{
    char *filename = args[0].sval;
    FILE       *file = stdout;

    if(filename && strlen(filename)>0) {
        /*create empty file*/
        file = fopen(filename,"w");
        if(!file) {
            printf("could not open %s %s\n",filename,strerror(errno));
            return;
        }
    } 
    testManager("cantBlockSingle",0,file);
    testManager("cantBlockMulti",0,file);
    testManager("canBlockSingle",0,file);
    testManager("canBlockMulti",0,file);
    if(file!=stdout) fclose(file);
}

static void testManagerRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&testManagerDef,testManagerCall);
    iocshRegister(&testManagerAllPortsDef,testManagerAllPortsCall);
}
epicsExportRegistrar(testManagerRegister);
