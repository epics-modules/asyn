/* testIPServer.c */
/*
 *      Author: Mark Rivers
 *      Date:   24FEB2006
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
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsAssert.h>
#include <asynDriver.h>
#include <asynInt32.h>
#include <asynOctet.h>
#include <asynOctetSyncIO.h>
#include <drvAsynIPPort.h>
#include <iocsh.h>
#include <registryFunction.h>
#include <epicsExport.h>

#define BUFFER_SIZE 80
typedef struct myData {
    epicsMutexId mutexId;
    char         *portName;
    int          numConnections;
    asynOctet    *pasynOctet;
    asynInt32    *pasynInt32;
    void         *octetPvt;
    void         *int32Pvt;
    void         *registrarPvt;
    asynUser     *pasynUser;
    int          fileDescriptor;
}myData;

static void echoHandler(myData *pPvt)
{
    asynUser *pasynUser;
    char portName[100];
    char buffer[BUFFER_SIZE];
    int nread, nwrite, eomReason;
    asynStatus status;

    /* Create a new asyn port with a unique name */
    epicsSnprintf(portName, sizeof(portName), "%s:%d", pPvt->portName, pPvt->numConnections);

    status = drvAsynIPPortFdConfigure(portName,
                         portName,
                         pPvt->fileDescriptor,
                         0, 0, 0);
    if (status) {
        printf("echoHandler: unable to create port %s\n", portName);
        return;
    }
    status = pasynOctetSyncIO->connect(portName, 0, &pasynUser, NULL);
    if (status) {
        printf("echoHandler: unable to connect to port %s\n", portName);
        return;
    }
    status - pasynOctetSyncIO->setInputEos(pasynUser, "\r\n", 2);
    status = pasynOctetSyncIO->setOutputEos(pasynUser, "\r\n", 2);
    while(1) {
        status = pasynOctetSyncIO->read(pasynUser, buffer, BUFFER_SIZE, 
                                        0.0, &nread, &eomReason);
        if (status) {
            printf("echoHandler: read error %s\n", pasynUser->errorMessage);
            goto disconnect;
        }
        status = pasynOctetSyncIO->write(pasynUser, buffer, strlen(buffer), 
                                         2.0, &nwrite);
        if (status) {
            printf("echoHandler: write error %s\n", pasynUser->errorMessage);
            goto disconnect;
        }
        /* NEED A WAY TO DETECT REMOTE HOST DISCONNECTED, KILL THREAD */
    }
    disconnect:
    pasynOctetSyncIO->disconnect(pasynUser);
    return(-1);
}

                         
static void connectionCallback(void *drvPvt, asynUser *pasynUser, epicsInt32 fd)
{
    myData     *pPvt = (myData *)drvPvt;
    myData     *newPvt = calloc(1, sizeof(myData));

    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
              "testIPServer: connectionCallback, file descriptor=%d\n", fd);
    epicsMutexLock(pPvt->mutexId);
    pPvt->numConnections++;
    /* Make a copy of myData, with new fileDescriptor */
    *newPvt = *pPvt;
    epicsMutexUnlock(pPvt->mutexId);
    newPvt->fileDescriptor = fd;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
              "testIPServer: connectionCallback, file descriptor=%d, numConnections=%d\n", 
              newPvt->fileDescriptor, newPvt->numConnections);
    /* Create a new thread to communicate with this port */
    epicsThreadCreate(pPvt->portName,
                      epicsThreadPriorityLow,
                      epicsThreadGetStackSize(epicsThreadStackSmall),
                      (EPICSTHREADFUNC)echoHandler, newPvt);
}

static void testIPServer(const char *portName)
{
    myData        *pPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    int           addr=0;
    asynInterface *pasynInterface;

    pPvt = (myData *)callocMustSucceed(1, sizeof(myData), "testIPServer");
    pPvt->mutexId = epicsMutexCreate();
    pPvt->portName = epicsStrDup(portName);
    pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = pPvt;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("can't connect to port %s: %s\n", portName, pasynUser->errorMessage);
        return;
    }
    pasynInterface = pasynManager->findInterface(
        pasynUser,asynInt32Type,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynInt32Type);
        return;
    }
    pPvt->pasynInt32 = (asynInt32 *)pasynInterface->pinterface;
    pPvt->int32Pvt = pasynInterface->drvPvt;
    status = pPvt->pasynInt32->registerInterruptUser(
                 pPvt->int32Pvt, pasynUser,
                 connectionCallback,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("testIPServer devAsynInt32 registerInterruptUser %s\n",
               pPvt->pasynUser->errorMessage);
    }
 }

static const iocshArg testIPServerArg0 = {"port", iocshArgString};
static const iocshArg *const testIPServerArgs[] = {
    &testIPServerArg0};
static const iocshFuncDef testIPServerDef = {"testIPServer", 1, testIPServerArgs};
static void testIPServerCall(const iocshArgBuf * args) 
{ 
    testIPServer(args[0].sval);
}

static void testIPServerRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&testIPServerDef,testIPServerCall);
}
epicsExportRegistrar(testIPServerRegister);
