/* asynExample.c */
/*
 *      Author: Marty Kraimer
 *      Date:   25JUN2004
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
#include <epicsAssert.h>
#include <asynDriver.h>
#include <asynOctet.h>
#include <iocsh.h>
#include <registryFunction.h>
#include <epicsExport.h>

#define BUFFER_SIZE 80
typedef struct myData {
    epicsEventId done;
    asynOctet    *pasynOctet;
    void         *drvPvt;
    char         buffer[BUFFER_SIZE];
}myData;

static void queueCallback(asynUser *pasynUser) {
    myData     *pmydata = (myData *)pasynUser->userPvt;
    asynOctet  *pasynOctet = pmydata->pasynOctet;
    void       *drvPvt = pmydata->drvPvt;
    asynStatus status;
    size_t     writeBytes,readBytes;
    int        eomReason;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"queueCallback entered\n");
    status = pasynOctet->write(drvPvt,pasynUser,pmydata->buffer,
              strlen(pmydata->buffer),&writeBytes);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "queueCallback write failed %s\n",pasynUser->errorMessage);
    } else {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
            pmydata->buffer,strlen(pmydata->buffer),
            "queueCallback write sent %lu bytes\n",(unsigned long)writeBytes);
    }
    status = pasynOctet->read(drvPvt,pasynUser,pmydata->buffer,
         BUFFER_SIZE,&readBytes,&eomReason);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "queueCallback read failed %s\n",pasynUser->errorMessage);
    } else {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
            pmydata->buffer,BUFFER_SIZE,
            "queueCallback read returned: retlen %lu eomReason 0x%x data %s\n",
            (unsigned long)readBytes,eomReason,pmydata->buffer);
    }
    if(pmydata->done) {
        epicsEventSignal(pmydata->done);
    } else {
        pasynManager->memFree(pasynUser->userPvt,sizeof(myData));
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "freeAsynUser failed %s\n",pasynUser->errorMessage);
    }
}
static void asynExample(const char *port,int addr,const char *message)
{
    myData        *pmyData1,*pmyData2;
    asynUser      *pasynUser,*pasynUserDuplicate;
    asynStatus    status;
    asynInterface *pasynInterface;
    int           canBlock;

    pmyData1 = (myData *)pasynManager->memMalloc(sizeof(myData));
    memset(pmyData1,0,sizeof(myData));
    pmyData2 = (myData *)pasynManager->memMalloc(sizeof(myData));
    memset(pmyData2,0,sizeof(myData));
    strcpy(pmyData1->buffer,message);
    pasynUser = pasynManager->createAsynUser(queueCallback,0);
    pasynUser->userPvt = pmyData1;
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        printf("can't connect to port %s\n",pasynUser->errorMessage);
        return;
    }
    pasynInterface = pasynManager->findInterface(
        pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynOctetType);
        return;
    }
    pmyData1->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pmyData1->drvPvt = pasynInterface->drvPvt;
    *pmyData2 = *pmyData1; /*structure copy*/
    strcat(pmyData2->buffer," repeated");
    canBlock = 0;
    pasynManager->canBlock(pasynUser,&canBlock);
    if(canBlock) pmyData2->done = epicsEventCreate(epicsEventEmpty);
    pasynUserDuplicate = pasynManager->duplicateAsynUser(
        pasynUser,queueCallback,0);
    pasynUserDuplicate->userPvt = pmyData2;
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow, 0.0);
    if(status) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "queueRequest failed %s\n",pasynUser->errorMessage);
    }
    status = pasynManager->queueRequest(pasynUserDuplicate,asynQueuePriorityLow, 0.0);
    if(status) {
        asynPrint(pasynUserDuplicate,ASYN_TRACE_ERROR,
            "queueRequest failed %s\n",pasynUserDuplicate->errorMessage);
    }
    if(canBlock) {
        epicsEventWait(pmyData2->done);
        epicsEventDestroy(pmyData2->done);
        pasynManager->memFree(pmyData2,sizeof(myData));
    }
}

static const iocshArg asynExampleArg0 = {"port", iocshArgString};
static const iocshArg asynExampleArg1 = {"addr", iocshArgInt};
static const iocshArg asynExampleArg2 = {"message", iocshArgString};
static const iocshArg *const asynExampleArgs[] = {
    &asynExampleArg0,&asynExampleArg1,&asynExampleArg2};
static const iocshFuncDef asynExampleDef = {"asynExample", 3, asynExampleArgs};
static void asynExampleCall(const iocshArgBuf * args) {
    asynExample(args[0].sval,args[1].ival,args[2].sval);
}

static void asynExampleRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynExampleDef,asynExampleCall);
}
epicsExportRegistrar(asynExampleRegister);
