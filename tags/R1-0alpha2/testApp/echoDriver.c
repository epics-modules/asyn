/*echoDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
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

#define BUFFERSIZE 4096
#define NUM_INTERFACES 2
#define NUM_DEVICES 2

typedef struct deviceBuffer {
    char buffer[BUFFERSIZE];
    int  nchars;
}deviceBuffer;

typedef struct echoPvt {
    deviceBuffer buffer[NUM_DEVICES];
    double delay;
    asynInterface *paasynInterface;
}echoPvt;
    
/* init routine */
static int echoDriverInit(const char *portName, double delay);

/* asynCommon methods */
static void report(void *ppvt,FILE *fp,int details);
static asynStatus connect(void *ppvt,asynUser *pasynUser);
static asynStatus disconnect(void *ppvt,asynUser *pasynUser);
static asynCommon asyn = {report,connect,disconnect};

/* asynOctet methods */
static int echoRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars);
static int echoWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars);
static asynStatus echoFlush(void *ppvt,asynUser *pasynUser);
static asynStatus setEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynOctet octet = {
    echoRead,echoWrite,echoFlush,setEos
};

static int echoDriverInit(const char *dn, double delay)
{
    echoPvt *pechoPvt;
    char *portName;
    asynStatus status;
    asynInterface *paasynInterface;

    portName = callocMustSucceed(strlen(dn)+1,sizeof(char),
        "echoDriverInit");
    strcpy(portName,dn);
    pechoPvt = callocMustSucceed(1,sizeof(echoPvt),"echoDriverInit");
    pechoPvt->delay = delay;
    paasynInterface = callocMustSucceed(NUM_INTERFACES,sizeof(asynInterface),
        "echoDriverInit");
    paasynInterface[0].interfaceType = asynCommonType;
    paasynInterface[0].pinterface = &asyn;
    paasynInterface[0].drvPvt = pechoPvt;
    paasynInterface[1].interfaceType = asynOctetType;
    paasynInterface[1].pinterface = &octet;
    paasynInterface[1].drvPvt = pechoPvt;
    pechoPvt->paasynInterface = paasynInterface;
    status = pasynManager->registerPort(
        portName,paasynInterface,NUM_INTERFACES,
        epicsThreadPriorityLow,
        epicsThreadGetStackSize(epicsThreadStackSmall));
    if(status!=asynSuccess) {
        printf("echoDriverInit registerDriver failed\n");
    }
    return(0);
}

/* asynCommon methods */
static void report(void *ppvt,FILE *fp,int details)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;

    fprintf(fp,"echoDriver. nchars = %d %d delay = %f\n",
        pechoPvt->buffer[0].nchars,pechoPvt->buffer[1].nchars,pechoPvt->delay);
}

static asynStatus connect(void *ppvt,asynUser *pasynUser)
{
    return(asynSuccess);
}
static asynStatus disconnect(void *ppvt,asynUser *pasynUser)
{
    return(asynSuccess);
}

/* asynOctet methods */
static int echoRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    deviceBuffer *pdeviceBuffer;
    int addr,nchars;

    addr = pasynManager->getAddr(pasynUser);
    if(addr<0 || addr >1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return(0);
    }
    pdeviceBuffer = &pechoPvt->buffer[addr];
    nchars = pdeviceBuffer->nchars;
    if(nchars>maxchars) nchars = maxchars;
    pdeviceBuffer->nchars -= nchars;
    if(nchars>0) memcpy(data,pdeviceBuffer->buffer,nchars);
    epicsThreadSleep(pechoPvt->delay);
    return(nchars);
}

static int echoWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    deviceBuffer *pdeviceBuffer;
    int addr;
    int nchars = numchars;

    addr = pasynManager->getAddr(pasynUser);
    if(addr<0 || addr >1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return(0);
    }
    pdeviceBuffer = &pechoPvt->buffer[addr];
    if(nchars>BUFFERSIZE) nchars = BUFFERSIZE;
    if(nchars>0) memcpy(pdeviceBuffer->buffer,data,nchars);
    pdeviceBuffer->nchars = nchars;
    epicsThreadSleep(pechoPvt->delay);
    return(nchars);
}

static asynStatus echoFlush(void *ppvt,asynUser *pasynUser)
{
    echoPvt *pechoPvt = (echoPvt *)ppvt;
    deviceBuffer *pdeviceBuffer;
    int addr;

    addr = pasynManager->getAddr(pasynUser);
    if(addr<0 || addr >1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return(0);
    }
    pdeviceBuffer = &pechoPvt->buffer[addr];
    pdeviceBuffer->nchars = 0;
    return(asynSuccess);
}

static asynStatus setEos(void *ppvt,asynUser *pasynUser,
     const char *eos,int eoslen)
{
    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
        "echoDriver:setEos not implemented\n");
    return(asynError);
}

/* register echoDriverInit*/
static const iocshArg echoDriverInitArg0 = { "portName", iocshArgString };
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
