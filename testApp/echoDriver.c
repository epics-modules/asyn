/*echoDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*test driver for asyn support*/
/* 
 * Author: Marty Kraimer
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

#define BUFSIZE 4096
#define NUM_INTERFACES 2
typedef struct echoPvt {
    char buffer[BUFSIZE];
    int  nchars;
    double delay;
    peekHandler peek_handler;
    void * peekHandlerPvt;
    deviceDriver *padeviceDriver;
}echoPvt;
    
/* init routine */
static int echoDriverInit(const char *deviceName, double delay);

/* asynDriver methods */
static void report(void *ppvt,int details);
static asynStatus connect(void *ppvt,asynUser *pasynUser);
static asynStatus disconnect(void *ppvt,asynUser *pasynUser);
static asynDriver asyn = {report,connect,disconnect};

/* octetDriver methods */
static int echoRead(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars);
static int echoWrite(void *ppvt,asynUser *pasynUser,
                int addr,const char *data,int numchars);
static asynStatus echoFlush(void *ppvt,asynUser *pasynUser,int addr);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    int addr, const char *eos,int eoslen);
static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,
    peekHandler handler, void *peekHandlerPvt);
static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser);
static octetDriver octet = {
    echoRead,echoWrite,echoFlush,setEos,
    installPeekHandler, removePeekHandler
};

static driverInterface mydriverInterface[NUM_INTERFACES] = {
    {asynDriverType,&asyn},
    {octetDriverType,&octet}
};

static int echoDriverInit(const char *dn, double delay)
{
    echoPvt *pechoPvt;
    char *deviceName;
    asynStatus status;
    deviceDriver *padeviceDriver;
    int i;

    deviceName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "echoDriverInit");
    strcpy(deviceName,dn);
    pechoPvt = callocMustSucceed(1,sizeof(echoPvt),"echoDriverInit");
    pechoPvt->delay = delay;
    padeviceDriver = callocMustSucceed(NUM_INTERFACES,sizeof(deviceDriver),
        "echoDriverInit");
    for(i=0; i<NUM_INTERFACES; i++) {
        padeviceDriver[i].pdriverInterface = &mydriverInterface[i];
        padeviceDriver[i].drvPvt = pechoPvt;
    }
    pechoPvt->padeviceDriver = padeviceDriver;
    status = pasynQueueManager->registerDevice(
        deviceName,padeviceDriver,NUM_INTERFACES,
        epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall));
    if(status!=asynSuccess) {
        printf("echoDriverInit registerDriver failed\n");
    }
    return(0);
}

/* asynDriver methods */
static void report(void *ppvt,int details)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;

    printf("echoDriver. nchars = %d delay = %f\n",
        pechoPvt->nchars,pechoPvt->delay);
}

static asynStatus connect(void *ppvt,asynUser *pasynUser)
{
    return(asynSuccess);
}
static asynStatus disconnect(void *ppvt,asynUser *pasynUser)
{
    return(asynSuccess);
}

/* octetDriver methods */
static int echoRead(void *ppvt,asynUser *pasynUser,int addr,char *data,int maxchars)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    int nchars = pechoPvt->nchars;
    int i;

    if(nchars>maxchars) nchars = maxchars;
    pechoPvt->nchars -= nchars;
    if(nchars>0) {
        memcpy(data,pechoPvt->buffer,nchars);
        if(pechoPvt->peek_handler) for(i=0; i<nchars; i++) {
           pechoPvt->peek_handler(pechoPvt->peekHandlerPvt,
               pechoPvt->buffer[i],1,(i==(nchars-1) ? 1 : 0));
        }
    }
    epicsThreadSleep(pechoPvt->delay);
printf("echoRead maxchars %d nchars %d\n",maxchars,nchars);
    return(nchars);
}

static int echoWrite(void *ppvt,asynUser *pasynUser, int addr,const char *data,int numchars)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    int nchars = numchars;
    int i;

printf("echoWrite data %s numchars %d\n",data,numchars);
    if(nchars>BUFSIZE) nchars = BUFSIZE;
    if(nchars>0) {
        memcpy(pechoPvt->buffer,data,nchars);
        if(pechoPvt->peek_handler) for(i=0; i<nchars; i++) {
           pechoPvt->peek_handler(pechoPvt->peekHandlerPvt,
               pechoPvt->buffer[i],0,(i==(nchars-1) ? 1 : 0));
        }
    }
    pechoPvt->nchars = nchars;
    epicsThreadSleep(pechoPvt->delay);
    return(nchars);
}

static asynStatus echoFlush(void *ppvt,asynUser *pasynUser,int addr)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;

    pechoPvt->nchars = 0;
    return(asynSuccess);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    int addr, const char *eos,int eoslen)
{
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "echoDriver:setEos not implemented\n");
    return(asynError);
}

static asynStatus installPeekHandler(void *ppvt,asynUser *pasynUser,
    peekHandler handler, void *peekHandlerPvt)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    pechoPvt->peek_handler = handler;
    pechoPvt->peekHandlerPvt = peekHandlerPvt;
    return(asynSuccess);
}

static asynStatus removePeekHandler(void *ppvt,asynUser *pasynUser)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    pechoPvt->peek_handler = 0;
    return(asynSuccess);
}

/* register echoDriverInit*/
static const iocshArg echoDriverInitArg0 = { "deviceName", iocshArgString };
static const iocshArg echoDriverInitArg1 = { "delay", iocshArgDouble };
static const iocshArg *echoDriverInitArgs[] = {
    &echoDriverInitArg0,&echoDriverInitArg1};
static const iocshFuncDef echoDriverInitFuncDef = {
    "echoDriverInit", 2, echoDriverInitArgs};
static void echoDriverInitCallFunc(const iocshArgBuf *args)
{
    echoDriverInit(args[0].sval,args[1].dval);
}

static void echoDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&echoDriverInitFuncDef, echoDriverInitCallFunc);
    }
}
epicsExportRegistrar(echoDriverRegister);
