/*testEcho.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* client to test testEcho
 *
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

typedef struct testPvt {
    asynUser *pasynUser;
    char *prefix;
    int ntimes;
    int itimes;
    int isRead;
    double queueTimeout;
    char buffer[80];
    octetDriver *poctetDriver;
    void *octetDriverPvt;
    epicsEventId done;
}testPvt;

void queueCallback(void *ppvt)
{
    testPvt *ptestPvt = (testPvt *)ppvt;
    asynUser *pasynUser = ptestPvt->pasynUser;
    octetDriver *poctetDriver = ptestPvt->poctetDriver;
    void *octetDriverPvt = ptestPvt->octetDriverPvt;
    int nchars;
    asynStatus status;

    if(ptestPvt->isRead) {
        nchars = poctetDriver->read(octetDriverPvt,
             pasynUser,0,ptestPvt->buffer,80);
        printf("%s received %d chars |%s|\n",
            ptestPvt->prefix,nchars,ptestPvt->buffer);
        status = pasynQueueManager->unlock(pasynUser);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
        }
    } else {
        sprintf(ptestPvt->buffer,"%s%d",ptestPvt->prefix,ptestPvt->itimes);
        nchars = poctetDriver->write(octetDriverPvt,
            pasynUser,0,ptestPvt->buffer,strlen(ptestPvt->buffer));
        printf("%s send %d chars |%s|\n",
            ptestPvt->prefix,nchars,ptestPvt->buffer);
    }
    epicsEventSignal(ptestPvt->done);
}

void timeoutCallback(void *ppvt)
{
    testPvt *ptestPvt = (testPvt *)ppvt;
    asynUser *pasynUser = ptestPvt->pasynUser;

    printf("timeoutCallback. Will cancel request\n");
    pasynQueueManager->cancelRequest(pasynUser);
    epicsEventSignal(ptestPvt->done);
}

static void writeReadThread(testPvt *ptestPvt)
{
    asynUser *pasynUser = ptestPvt->pasynUser;
    asynStatus status;

    ptestPvt->isRead = 1;
    while(1) {
        if(ptestPvt->isRead) {
            ptestPvt->itimes++;
            if(ptestPvt->ntimes 
            && ptestPvt->ntimes<ptestPvt->itimes) break;
            ptestPvt->isRead = 0;
            status = pasynQueueManager->lock(pasynUser);
            if(status!=asynSuccess) {
                printf("%s\n",pasynUser->errorMessage);
            }
        } else {
            ptestPvt->isRead = 1;
        }
        status = pasynQueueManager->queueRequest(pasynUser,
            asynQueuePriorityLow,ptestPvt->queueTimeout);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynQueueManager->freeAsynUser(pasynUser);
            break;
        }
        epicsEventMustWait(ptestPvt->done);
    }
    status = pasynQueueManager->disconnectDevice(pasynUser);
    if(status!=asynSuccess) printf("%s\n",pasynUser->errorMessage);
    status = pasynQueueManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) printf("%s\n",pasynUser->errorMessage);
    epicsEventDestroy(ptestPvt->done);
    free(ptestPvt->prefix);
    free(ptestPvt);
}

static int testEcho(const char *deviceName,const char *pre,
    int ntimes,double queueTimeout)
{
    asynUser *pasynUser;
    testPvt *ptestPvt;
    char *prefix;
    deviceDriver *pdeviceDriver;
    asynDriver *pasynDriver;
    void *asynDriverPvt;
    asynStatus status;

    prefix = calloc(strlen(pre)+1,sizeof(char));
    strcpy(prefix,pre);
    ptestPvt = calloc(1,sizeof(testPvt));
    ptestPvt->prefix = prefix;
    ptestPvt->queueTimeout = queueTimeout;
    ptestPvt->ntimes = ntimes;
    ptestPvt->done = epicsEventMustCreate(epicsEventEmpty);
    pasynUser = pasynQueueManager->createAsynUser(
        queueCallback,timeoutCallback,ptestPvt);
    ptestPvt->pasynUser = pasynUser;
    status = pasynQueueManager->connectDevice(pasynUser,deviceName);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    pasynQueueManager->report(3);
    pdeviceDriver = pasynQueueManager->findDriver(
        pasynUser,asynDriverType,1);
    if(!pdeviceDriver) {
        printf("%s %s\n",asynDriverType,pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    pasynDriver = (asynDriver *)pdeviceDriver->pdriverInterface->pinterface;
    asynDriverPvt = pdeviceDriver->drvPvt;
    pasynDriver->report(asynDriverPvt,3);
    pdeviceDriver = pasynQueueManager->findDriver(
        pasynUser,octetDriverType,1);
    if(!pdeviceDriver) {
        printf("%s %s\n",octetDriverType,pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    ptestPvt->poctetDriver = (octetDriver *)
        pdeviceDriver->pdriverInterface->pinterface;
    ptestPvt->octetDriverPvt = pdeviceDriver->drvPvt;
    epicsThreadCreate(prefix,epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)writeReadThread,(void *)ptestPvt);
    return(0);
}

/* register testEcho*/
static const iocshArg testEchoArg0 = { "deviceName", iocshArgString };
static const iocshArg testEchoArg1 = { "prefix", iocshArgString };
static const iocshArg testEchoArg2 = { "ntimes", iocshArgInt };
static const iocshArg testEchoArg3 = { "queueTimeout", iocshArgDouble };
static const iocshArg *testEchoArgs[] = {
    &testEchoArg0,&testEchoArg1,&testEchoArg2,&testEchoArg3 };
static const iocshFuncDef testEchoFuncDef = {
    "testEcho", 4, testEchoArgs};
static void testEchoCallFunc(const iocshArgBuf *args)
{
    testEcho(args[0].sval,args[1].sval,args[2].ival,args[3].dval);
}

static void testEchoRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&testEchoFuncDef, testEchoCallFunc);
    }
}
epicsExportRegistrar(testEchoRegister);
