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
    asynOctet *pasynOctet;
    void *drvPvt;
    char buffer[BUFFER_SIZE];
}myData;

static void queueCallback(asynUser *pasynUser) {
    myData     *pmydata = (myData *)pasynUser->userPvt;
    asynOctet  *pasynOctet = pmydata->pasynOctet;
    void       *drvPvt = pmydata->drvPvt;
    asynStatus status;
    int        writeBytes,readBytes;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,"queueCallback entered\n");
    status = pasynOctet->write(drvPvt,pasynUser,pmydata->buffer,
              strlen(pmydata->buffer),&writeBytes);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "queueCallback write failed %s\n",pasynUser->errorMessage);
    } else {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
            pmydata->buffer,strlen(pmydata->buffer),
            "queueCallback write sent %d bytes\n",writeBytes);
    }
    status = pasynOctet->read(drvPvt,pasynUser,pmydata->buffer,
         BUFFER_SIZE,&readBytes);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "queueCallback read failed %s\n",pasynUser->errorMessage);
    } else {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,
            pmydata->buffer,BUFFER_SIZE,
            "queueCallback read returned: retlen %d data %s\n",
            readBytes,pmydata->buffer);
    }
    if(strcmp(pmydata->buffer,"Duplicate")==0) {
        epicsEventSignal(pmydata->done);
    } else {
        strcpy(pmydata->buffer,"Duplicate");
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "freeAsynUser failed %s\n",pasynUser->errorMessage);
    }
}
static void asynExample(const char *port,int addr,const char *message)
{
    myData        *pmyData;
    asynUser      *pasynUser;
    asynUser      *pasynUserDuplicate;
    asynStatus    status;
    asynInterface *pasynInterface;

    pmyData = calloc(1,sizeof(myData));
    strcpy(pmyData->buffer,message);
    pasynUser = pasynManager->createAsynUser(queueCallback,0);
printf("pasynUser %p\n",pasynUser);
    pasynUser->userPvt = pmyData;
    status = pasynManager->connectDevice(pasynUser,port,addr);
    if(status!=asynSuccess) {
        printf("can't connect to serialPort1 %s\n",pasynUser->errorMessage);
        exit(1);
    }
    pasynInterface = pasynManager->findInterface(
        pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        printf("%s driver not supported\n",asynOctetType);
        exit(-1);
    }
    pmyData->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pmyData->drvPvt = pasynInterface->drvPvt;
    pmyData->done = epicsEventCreate(epicsEventEmpty);
    pasynUserDuplicate = pasynManager->duplicateAsynUser(pasynUser,queueCallback,0);
printf("pasynUserDuplicate %p\n",pasynUserDuplicate);
    pasynUserDuplicate->userPvt = pmyData;
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
    epicsEventWait(pmyData->done);
    epicsEventDestroy(pmyData->done);
    free(pmyData);
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
