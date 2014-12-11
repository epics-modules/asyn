/* ipEchoServer.c */
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
#include <asynOctet.h>
#include <asynOctetSyncIO.h>
#include <drvAsynIPPort.h>
#include <iocsh.h>
#include <registryFunction.h>
#include <epicsExport.h>

#define MESSAGE_SIZE 80
#define READ_TIMEOUT -1.0
#define WRITE_TIMEOUT 2.0

typedef struct myData {
    epicsMutexId mutexId;
    char         *portName;
    double       readTimeout;
    asynOctet    *pasynOctet;
    void         *octetPvt;
    void         *registrarPvt;
}myData;

static void echoListener(myData *pPvt)
{
    asynUser *pasynUser;
    char buffer[MESSAGE_SIZE];
    size_t nread, nwrite;
    int eomReason;
    asynStatus status;

    status = pasynOctetSyncIO->connect(pPvt->portName, 0, &pasynUser, NULL);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "echoListener: unable to connect to port %s\n", 
                  pPvt->portName);
        return;
    }
    status = pasynOctetSyncIO->setInputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "echoListener: unable to set input EOS on %s: %s\n", 
                  pPvt->portName, pasynUser->errorMessage);
        return;
    }
    status = pasynOctetSyncIO->setOutputEos(pasynUser, "\r\n", 2);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                  "echoListener: unable to set output EOS on %s: %s\n", 
                  pPvt->portName, pasynUser->errorMessage);
        return;
    }
    while(1) {
        /* Erase buffer */
        buffer[0] = 0;
        status = pasynOctetSyncIO->read(pasynUser, buffer, MESSAGE_SIZE, 
                                        pPvt->readTimeout, &nread, &eomReason);
        switch (status) {
        case asynSuccess:
            asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                      "echoListener: %s read %lu: %s\n", 
                      pPvt->portName, (unsigned long)nread, buffer);
            status = pasynOctetSyncIO->write(pasynUser, buffer, strlen(buffer),
                                             WRITE_TIMEOUT, &nwrite);
            if (status) {
                asynPrint(pasynUser, ASYN_TRACE_ERROR,
                          "echoListener: write error on: %s: %s\n",
                          pPvt->portName, pasynUser->errorMessage);
                goto done;
            }
            asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                      "echoListener: %s wrote %lu: %s\n",
                      pPvt->portName, (unsigned long)nwrite, buffer);
            break;

        case asynTimeout:
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "echoListener: timeout on: %s read %lu: %s\n", 
                      pPvt->portName, (unsigned long)nread, buffer);
            break;

        default:
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                      "echoListener: read error on: %s: status=%d error=%s\n", 
                      pPvt->portName, status, pasynUser->errorMessage);
            goto done;
        }
    }
    done:
    status = pasynManager->freeAsynUser(pasynUser);
    if (status != asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                              "echoListener: Can't free port %s asynUser\n",
                                                               pPvt->portName);
    }
    free(pPvt->portName);
    free(pPvt);
}

                         
static void connectionCallback(void *drvPvt, asynUser *pasynUser, char *portName, 
                               size_t len, int eomReason)
{
    myData     *pPvt = (myData *)drvPvt;
    myData     *newPvt = calloc(1, sizeof(myData));

    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
              "ipEchoServer: connectionCallback, portName=%s\n", portName);
    epicsMutexLock(pPvt->mutexId);
    /* Make a copy of myData, with new portName */
    *newPvt = *pPvt;
    epicsMutexUnlock(pPvt->mutexId);
    newPvt->portName = epicsStrDup(portName);
    /* Create a new thread to communicate with this port */
    epicsThreadCreate(pPvt->portName,
                      epicsThreadPriorityLow,
                      epicsThreadGetStackSize(epicsThreadStackSmall),
                      (EPICSTHREADFUNC)echoListener, newPvt);
}

static void ipEchoServer(const char *portName, int readTimeout)
{
    myData        *pPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    int           addr=0;
    asynInterface *pasynInterface;

    pPvt = (myData *)callocMustSucceed(1, sizeof(myData), "ipEchoServer");
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
        pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynOctetType);
        return;
    }
    if (readTimeout == 0) 
        pPvt->readTimeout = READ_TIMEOUT;
    else 
        pPvt->readTimeout = readTimeout/1000.;
    pPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pPvt->octetPvt = pasynInterface->drvPvt;
    status = pPvt->pasynOctet->registerInterruptUser(
                 pPvt->octetPvt, pasynUser,
                 connectionCallback,pPvt,&pPvt->registrarPvt);
    if(status!=asynSuccess) {
        printf("ipEchoServer devAsynOctet registerInterruptUser %s\n",
               pasynUser->errorMessage);
    }
}

static const iocshArg ipEchoServerArg0 = {"port", iocshArgString};
static const iocshArg ipEchoServerArg1 = {"read timeout (msec)", iocshArgInt};
static const iocshArg *const ipEchoServerArgs[] = {
    &ipEchoServerArg0,
    &ipEchoServerArg1};
static const iocshFuncDef ipEchoServerDef = {"ipEchoServer", 2, ipEchoServerArgs};
static void ipEchoServerCall(const iocshArgBuf * args) 
{ 
    ipEchoServer(args[0].sval, args[1].ival);
}

static void ipEchoServerRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&ipEchoServerDef,ipEchoServerCall);
}
epicsExportRegistrar(ipEchoServerRegister);
