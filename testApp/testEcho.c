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

struct userPvt {
    asynUser *pasynUser;
    char *prefix;
    int ntimes;
    int itimes;
    int isRead;
    char buffer[80];
    octetDriver *poctetDriver;
    epicsEventId done;
};

void myCallback(userPvt *puserPvt)
{
    int nchars;
    asynUser *pasynUser = puserPvt->pasynUser;
    asynStatus status;

    if(puserPvt->isRead) {
        nchars = puserPvt->poctetDriver->read(pasynUser,0,puserPvt->buffer,80);
        printf("%s received %d chars |%s|\n",
            puserPvt->prefix,nchars,puserPvt->buffer);
        status = pasynQueueManager->unlock(pasynUser);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
        }
    } else {
        sprintf(puserPvt->buffer,"%s%d",puserPvt->prefix,puserPvt->itimes);
        nchars = puserPvt->poctetDriver->write(
            pasynUser,0,puserPvt->buffer,strlen(puserPvt->buffer));
        printf("%s send %d chars |%s|\n",
            puserPvt->prefix,nchars,puserPvt->buffer);
    }
    epicsEventSignal(puserPvt->done);
}

static void writeRead(userPvt *puserPvt)
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
            status = pasynQueueManager->lock(pasynUser,10);
            if(status!=asynSuccess) {
                printf("%s\n",pasynUser->errorMessage);
            }
        } else {
            puserPvt->isRead = 1;
        }
        status = pasynQueueManager->queueRequest(pasynUser,asynQueuePriorityLow);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynQueueManager->freeAsynUser(pasynUser);
            break;
        }
        epicsEventMustWait(puserPvt->done);
    }
    pasynQueueManager->freeAsynUser(pasynUser);
    epicsEventDestroy(puserPvt->done);
    free(puserPvt->prefix);
    free(puserPvt);
}

static int testEcho(const char *name,const char *pre,int ntimes)
{
    asynUser *pasynUser;
    userPvt *puserPvt;
    asynStatus status;
    char *prefix;
    asynDriver *pasynDriver;

    prefix = calloc(strlen(pre)+1,sizeof(char));
    strcpy(prefix,pre);
    puserPvt = calloc(1,sizeof(userPvt));
    puserPvt->prefix = prefix;
    puserPvt->ntimes = ntimes;
    puserPvt->done = epicsEventMustCreate(epicsEventEmpty);
    pasynUser = pasynQueueManager->createAsynUser(myCallback,puserPvt);
    puserPvt->pasynUser = pasynUser;
    status = pasynQueueManager->connectDevice(pasynUser,name);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    pasynQueueManager->report(3);
    pasynDriver = pasynQueueManager->findDriver(
        pasynUser,asynDriverType);
    if(!pasynDriver) {
        printf("%s %s\n",asynDriverType,pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    pasynDriver->report(pasynUser,3);
    puserPvt->poctetDriver = pasynQueueManager->findDriver(
        pasynUser,octetDriverType);
    if(!puserPvt->poctetDriver) {
        printf("%s %s\n",octetDriverType,pasynUser->errorMessage);
        pasynQueueManager->freeAsynUser(pasynUser);
        return(-1);
    }
    epicsThreadCreate(prefix,epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall),
        (EPICSTHREADFUNC)writeRead,(void *)puserPvt);
    return(0);
}

/* register testEcho*/
static const iocshArg testEchoArg0 = { "name", iocshArgString };
static const iocshArg testEchoArg1 = { "prefix", iocshArgString };
static const iocshArg testEchoArg2 = { "ntimes", iocshArgInt };
static const iocshArg *testEchoArgs[] = {
    &testEchoArg0,&testEchoArg1,&testEchoArg2 };
static const iocshFuncDef testEchoFuncDef = {
    "testEcho", 3, testEchoArgs};
static void testEchoCallFunc(const iocshArgBuf *args)
{
    testEcho(args[0].sval,args[1].sval,args[2].ival);
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
