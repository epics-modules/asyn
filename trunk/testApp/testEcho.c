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

typedef struct userPvt {
    asynUser *pasynUser;
    char *prefix;
    int ntimes;
    int itimes;
    int isRead;
    double queueTimeout;
    char buffer[80];
    octetDriver *poctetDriver;
    void *poctetDriverPvt;
    epicsEventId done;
}userPvt;

void queueCallback(void *ppvt)
{
    userPvt *puserPvt = (userPvt *)ppvt;
    asynUser *pasynUser = puserPvt->pasynUser;
    octetDriver *poctetDriver = puserPvt->poctetDriver;
    void *poctetDriverPvt = puserPvt->poctetDriverPvt;
    int nchars;
    asynStatus status;

    if(puserPvt->isRead) {
        nchars = poctetDriver->read(poctetDriverPvt,
             pasynUser,0,puserPvt->buffer,80);
        printf("%s received %d chars |%s|\n",
            puserPvt->prefix,nchars,puserPvt->buffer);
        status = pasynQueueManager->unlock(pasynUser);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
        }
    } else {
        sprintf(puserPvt->buffer,"%s%d",puserPvt->prefix,puserPvt->itimes);
        nchars = poctetDriver->write(poctetDriverPvt,
            pasynUser,0,puserPvt->buffer,strlen(puserPvt->buffer));
        printf("%s send %d chars |%s|\n",
            puserPvt->prefix,nchars,puserPvt->buffer);
    }
    epicsEventSignal(puserPvt->done);
}

void timeoutCallback(void *ppvt)
{
    userPvt *puserPvt = (userPvt *)ppvt;
    asynUser *pasynUser = puserPvt->pasynUser;
    asynStatus status;

    printf("timeoutCallback. Will cancel request\n");
    status = pasynQueueManager->cancelRequest(pasynUser);
    if(status!=asynSuccess) printf("%s\n",pasynUser->errorMessage);
    epicsEventSignal(puserPvt->done);
}

static void writeReadThread(userPvt *puserPvt)
{
    asynUser *pasynUser = puserPvt->pasynUser;
    asynStatus status;

    puserPvt->isRead = 1;
    while(1) {
        if(puserPvt->isRead) {
            puserPvt->itimes++;
            if(puserPvt->ntimes 
            && puserPvt->ntimes<puserPvt->itimes) break;
            puserPvt->isRead = 0;
            status = pasynQueueManager->lock(pasynUser);
            if(status!=asynSuccess) {
                printf("%s\n",pasynUser->errorMessage);
            }
        } else {
            puserPvt->isRead = 1;
        }
        status = pasynQueueManager->queueRequest(pasynUser,
            asynQueuePriorityLow,puserPvt->queueTimeout);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynQueueManager->freeAsynUser(pasynUser);
            break;
        }
        epicsEventMustWait(puserPvt->done);
    }
    status = pasynQueueManager->disconnectDevice(pasynUser);
    if(status!=asynSuccess) printf("%s\n",pasynUser->errorMessage);
    status = pasynQueueManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) printf("%s\n",pasynUser->errorMessage);
    epicsEventDestroy(puserPvt->done);
    free(puserPvt->prefix);
    free(puserPvt);
}

static int testEcho(const char *deviceName,const char *pre,
    int ntimes,double queueTimeout)
{
    asynUser *pasynUser;
    userPvt *puserPvt;
    char *prefix;
    deviceDriver *pdeviceDriver;
    asynDriver *pasynDriver;
    void *pasynDriverPvt;
    asynStatus status;

    prefix = calloc(strlen(pre)+1,sizeof(char));
    strcpy(prefix,pre);
    puserPvt = calloc(1,sizeof(userPvt));
    puserPvt->prefix = prefix;
    puserPvt->queueTimeout = queueTimeout;
    puserPvt->ntimes = ntimes;
    puserPvt->done = epicsEventMustCreate(epicsEventEmpty);
    pasynUser = pasynQueueManager->createAsynUser(
        queueCallback,timeoutCallback,puserPvt);
    puserPvt->pasynUser = pasynUser;
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
    pasynDriverPvt = pdeviceDriver->pdrvPvt;
    pasynDriver->report(pasynDriverPvt,pasynUser,3);
    pdeviceDriver = pasynQueueManager->findDriver(
        pasynUser,octetDriverType,1);
    if(!pdeviceDriver) {
        printf("%s %s\n",octetDriverType,pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    puserPvt->poctetDriver = (octetDriver *)
        pdeviceDriver->pdriverInterface->pinterface;
    puserPvt->poctetDriverPvt = pdeviceDriver->pdrvPvt;
    epicsThreadCreate(prefix,epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)writeReadThread,(void *)puserPvt);
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
