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
#include <errno.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynOctet.h>
#include <asynInt32.h>

#define BUFFERSIZE 4096
#define NUM_INTERFACES 2
#define NUM_DEVICES 2

typedef struct deviceBuffer {
    char buffer[BUFFERSIZE];
    int  nchars;
}deviceBuffer;

typedef struct deviceInfo {
    deviceBuffer buffer;
    int          connected;
}deviceInfo;

typedef struct drvUserInfo {
    const char *format;
    char       buffer[20];
}drvUserInfo;
static const char *drvUserType = "echoDriverType";

typedef struct echoPvt {
    deviceInfo    device[NUM_DEVICES];
    const char    *portName;
    int           connected;
    int           multiDevice;
    double        delay;
    asynInterface common;
    asynInterface drvUser;
    asynInterface octet;
    asynInterface int32;
}echoPvt;
    
/* init routine */
static int echoDriverInit(const char *dn, double delay,
    int noAutoConnect,int multiDevice);

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynCommon asyn = { report, connect, disconnect };

/* asynDrvUser methods */
static asynStatus drvUserCreate(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize);
static asynStatus drvUserGetType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize);
static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser);
static asynDrvUser drvUser = {drvUserCreate,drvUserGetType,drvUserDestroy};

/* asynOctet methods */
static asynStatus echoRead(void *drvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus echoWrite(void *drvPvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus echoFlush(void *drvPvt,asynUser *pasynUser);
static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynOctet octet = { echoRead, echoWrite, echoFlush, setEos, getEos };

/* asynInt32 methods */
static asynStatus echoWriteInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value);
static asynStatus echoReadInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value);
static asynStatus getBoundsInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high);
static asynInt32 int32 = { echoWriteInt32, echoReadInt32, getBoundsInt32};

static int echoDriverInit(const char *dn, double delay,
    int noAutoConnect,int multiDevice)
{
    echoPvt    *pechoPvt;
    char       *portName;
    asynStatus status;
    int        nbytes;
    int        attributes;

    nbytes = sizeof(echoPvt) + strlen(dn) + 1;
    pechoPvt = callocMustSucceed(nbytes,sizeof(char),"echoDriverInit");
    portName = (char *)(pechoPvt + 1);
    strcpy(portName,dn);
    pechoPvt->portName = portName;
    pechoPvt->delay = delay;
    pechoPvt->multiDevice = multiDevice;
    pechoPvt->common.interfaceType = asynCommonType;
    pechoPvt->common.pinterface  = (void *)&asyn;
    pechoPvt->common.drvPvt = pechoPvt;
    pechoPvt->drvUser.interfaceType = asynDrvUserType;
    pechoPvt->drvUser.pinterface  = (void *)&drvUser;
    pechoPvt->drvUser.drvPvt = pechoPvt;
    pechoPvt->octet.interfaceType = asynOctetType;
    pechoPvt->octet.pinterface  = (void *)&octet;
    pechoPvt->octet.drvPvt = pechoPvt;
    pechoPvt->int32.interfaceType = asynInt32Type;
    pechoPvt->int32.pinterface  = (void *)&int32;
    pechoPvt->int32.drvPvt = pechoPvt;
    attributes = 0;
    if(multiDevice) attributes |= ASYN_MULTIDEVICE;
    if(delay>0.0) attributes|=ASYN_CANBLOCK;
    status = pasynManager->registerPort(portName,attributes,!noAutoConnect,0,0);
    if(status!=asynSuccess) {
        printf("echoDriverInit registerDriver failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pechoPvt->common);
    if(status!=asynSuccess){
        printf("echoDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pechoPvt->drvUser);
    if(status!=asynSuccess){
        printf("echoDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pechoPvt->octet);
    if(status!=asynSuccess){
        printf("echoDriverInit registerInterface failed\n");
        return 0;
    }
    status = pasynManager->registerInterface(portName,&pechoPvt->int32);
    if(status!=asynSuccess){
        printf("echoDriverInit registerInterface failed\n");
        return 0;
    }
    return(0);
}

/* asynCommon methods */
static void report(void *drvPvt,FILE *fp,int details)
{
    echoPvt *pechoPvt = (echoPvt *)drvPvt;
    int i,n;

    fprintf(fp,"    echoDriver. "
        "multiDevice:%s connected:%s delay = %f\n",
        (pechoPvt->multiDevice ? "Yes" : "No"),
        (pechoPvt->connected ? "Yes" : "No"),
        pechoPvt->delay);
    n = (pechoPvt->multiDevice) ? NUM_DEVICES : 1;
    for(i=0;i<n;i++) {
       fprintf(fp,"        device %d connected:%s nchars = %d\n",
            i,
            (pechoPvt->connected ? "Yes" : "No"),
            pechoPvt->device[i].buffer.nchars);
    }
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    deviceInfo *pdeviceInfo;
    int        addr;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:connect addr %d\n",pechoPvt->portName,addr);
    if(!pechoPvt->multiDevice) {
        if(pechoPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s echoDriver:connect port already connected\n",
               pechoPvt->portName);
            return asynError;
        }
        pechoPvt->connected = 1;
        pechoPvt->device[0].connected = 1;
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr<=-1) {
        if(pechoPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s echoDriver:connect port already connected\n",
               pechoPvt->portName);
            return asynError;
        }
        pechoPvt->connected = 1;
        pasynManager->exceptionConnect(pasynUser);
        return asynSuccess;
    }
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:connect illegal addr %d\n",pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceInfo = &pechoPvt->device[addr];
    if(pdeviceInfo->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:connect device %d already connected\n",
            pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceInfo->connected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return(asynSuccess);
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    deviceInfo *pdeviceInfo;
    int        addr;
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:disconnect addr %d\n",pechoPvt->portName,addr);
    if(!pechoPvt->multiDevice) {
        if(!pechoPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s echoDriver:disconnect port not connected\n",
               pechoPvt->portName);
            return asynError;
        }
        pechoPvt->connected = 0;
        pechoPvt->device[0].connected = 0;
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(addr<=-1) {
        if(!pechoPvt->connected) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
               "%s echoDriver:disconnect port not connected\n",
               pechoPvt->portName);
            return asynError;
        }
        pechoPvt->connected = 0;
        pasynManager->exceptionDisconnect(pasynUser);
        return asynSuccess;
    }
    if(addr>=NUM_DEVICES) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:disconnect illegal addr %d\n",pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceInfo = &pechoPvt->device[addr];
    if(!pdeviceInfo->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:disconnect device %d not connected\n",
            pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceInfo->connected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return(asynSuccess);
}

static asynStatus drvUserCreate(void *drvPvt,asynUser *pasynUser,
    const char *drvInfo, const char **pptypeName,size_t *psize)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    drvUserInfo *pdrvUserInfo;

    pdrvUserInfo = callocMustSucceed(1,sizeof(char),"drvUserCreate");
    pdrvUserInfo->format = drvInfo;
    pasynUser->drvUser = pdrvUserInfo;
    if(pptypeName) *pptypeName = drvUserType;
    if(psize) *psize = sizeof(drvUserInfo);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:drvUserCreate drvInfo %s\n",
        pechoPvt->portName,
        (drvInfo ? drvInfo : "null"));
    return asynSuccess;
}

static asynStatus drvUserGetType(void *drvPvt,asynUser *pasynUser,
    const char **pptypeName,size_t *psize)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    if(pasynUser->drvUser) {
        if(pptypeName) *pptypeName = drvUserType;
        if(psize) *psize = sizeof(drvUserInfo);
    } else {
        if(pptypeName) *pptypeName = 0;
        if(psize) *psize = 0;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:drvUserGetType\n", pechoPvt->portName);
    return asynSuccess;
}

static asynStatus drvUserDestroy(void *drvPvt,asynUser *pasynUser)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    void *drvUser = pasynUser->drvUser;
    if(drvUser) {
        pasynUser->drvUser = 0;
        free(pasynUser->drvUser);
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:drvUserDestroy\n", pechoPvt->portName);
    return asynSuccess;
}

/* asynOctet methods */
static asynStatus echoRead(void *drvPvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason)
{
    echoPvt      *pechoPvt = (echoPvt *)drvPvt;
    deviceInfo   *pdeviceInfo;
    deviceBuffer *pdeviceBuffer;
    int          nchars,prevNchars;
    int          addr;
    asynStatus   status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(!pechoPvt->multiDevice) addr = 0;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:read addr %d\n",pechoPvt->portName,addr);
    if(addr<0 || addr>=NUM_DEVICES) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return(0);
    }
    pdeviceInfo = &pechoPvt->device[addr];
    if(!pdeviceInfo->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:read device %d not connected\n",
            pechoPvt->portName,addr);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s echoDriver:read device %d not connected\n",
            pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceBuffer = &pdeviceInfo->buffer;
    prevNchars = nchars = pdeviceBuffer->nchars;
    if(nchars>maxchars) nchars = maxchars;
    pdeviceBuffer->nchars = 0;
    if(nchars>0) memcpy(data,pdeviceBuffer->buffer,nchars);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,data,nchars,
        "echoRead nchars %d ",nchars);
    if(pechoPvt->delay>0.0) epicsThreadSleep(pechoPvt->delay);
    *nbytesTransfered = nchars;
    if(eomReason) {
        *eomReason = 0;
        if(prevNchars>=maxchars) *eomReason |= ASYN_EOM_CNT;
        *eomReason |= ASYN_EOM_END;
    }
    return status;
}

static asynStatus echoWrite(void *drvPvt,asynUser *pasynUser,
    const char *data,int nchars,int *nbytesTransfered)
{
    echoPvt      *pechoPvt = (echoPvt *)drvPvt;
    deviceInfo   *pdeviceInfo;
    deviceBuffer *pdeviceBuffer;
    int          addr;
    asynStatus   status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(!pechoPvt->multiDevice) addr = 0;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:write addr %d\n",pechoPvt->portName,addr);
    if(addr<0 || addr>=NUM_DEVICES) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return asynError;
    }
    pdeviceInfo = &pechoPvt->device[addr];
    if(!pdeviceInfo->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:write device %d not connected\n",
            pechoPvt->portName,addr);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s echoDriver:write device %d not connected\n",
            pechoPvt->portName,addr);
        return asynError;
    }
    pdeviceBuffer = &pdeviceInfo->buffer;
    if(nchars>BUFFERSIZE) nchars = BUFFERSIZE;
    if(nchars>0) memcpy(pdeviceBuffer->buffer,data,nchars);
    asynPrintIO(pasynUser,ASYN_TRACEIO_DRIVER,data,nchars,
            "echoWrite nchars %d ",nchars);
    pdeviceBuffer->nchars = nchars;
    if(pechoPvt->delay>0.0) epicsThreadSleep(pechoPvt->delay);
    *nbytesTransfered = nchars;
    return status;
}

static asynStatus echoFlush(void *drvPvt,asynUser *pasynUser)
{
    echoPvt *pechoPvt = (echoPvt *)drvPvt;
    deviceInfo *pdeviceInfo;
    deviceBuffer *pdeviceBuffer;
    int          addr;
    asynStatus   status;

    status = pasynManager->getAddr(pasynUser,&addr);
    if(status!=asynSuccess) return status;
    if(!pechoPvt->multiDevice) addr = 0;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:flush addr %d\n",pechoPvt->portName,addr);
    if(addr<0 || addr>=NUM_DEVICES) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "addr %d is illegal. Must be 0 or 1\n",addr);
        return(0);
    }
    pdeviceInfo = &pechoPvt->device[addr];
    if(!pdeviceInfo->connected) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:flush device %d not connected\n",
            pechoPvt->portName,addr);
        return -1;
    }
    pdeviceBuffer = &pdeviceInfo->buffer;
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"echoFlush\n");
    pdeviceBuffer->nchars = 0;
    return(asynSuccess);
}

static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
     const char *eos,int eoslen)
{
    return(asynSuccess);
}

static asynStatus getEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    *eoslen = 0;
    eos[0] = 0;
    return(asynSuccess);
}

static asynStatus echoWriteInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 value)
{
    echoPvt      *pechoPvt = (echoPvt *)drvPvt;
    drvUserInfo  *pdrvUserInfo = (drvUserInfo *)pasynUser->drvUser;
    asynStatus   status;
    int          nbytes;
    int          nbytesTransfered = 0;

    if(!pdrvUserInfo) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynUser.drvUser is null\n");
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:writeInt32 value %d\n",pechoPvt->portName,value);
    errno = 0;
    nbytes = epicsSnprintf(pdrvUserInfo->buffer,sizeof(pdrvUserInfo->buffer),
        pdrvUserInfo->format,value);
    if(nbytes<0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:writeInt32 epicsSnprintf error %s\n",
            pechoPvt->portName,strerror(errno));
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "epicsSnprintf error %s",strerror(errno));
        return asynError;
    }
    if(nbytes==0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:writeInt32 epicsSnprintf converted nothing\n",
            pechoPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "epicsSnprintf converted nothing ");
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "%s echoDriver:writeInt32 value %d\n",pechoPvt->portName,value);
    status = echoWrite(drvPvt,pasynUser,
        pdrvUserInfo->buffer,nbytes,&nbytesTransfered);
    if(status!=asynSuccess) return status;
    if(nbytes!=nbytesTransfered) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "nbytes %d but nbytesTransfered %d",nbytes,nbytesTransfered);
        return asynError;
    }
    return asynSuccess;
}

static asynStatus echoReadInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *value)
{
    echoPvt    *pechoPvt = (echoPvt *)drvPvt;
    drvUserInfo  *pdrvUserInfo = (drvUserInfo *)pasynUser->drvUser;
    asynStatus   status;
    int          nitems;
    int          nbytesTransfered = 0;

    if(!pdrvUserInfo) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "asynUser.drvUser is null\n");
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s echoDriver:readInt32\n",pechoPvt->portName);
    status = echoRead(drvPvt,pasynUser,
         pdrvUserInfo->buffer,sizeof(pdrvUserInfo->buffer),&nbytesTransfered,0);
    if(nbytesTransfered>=sizeof(pdrvUserInfo->buffer))
         nbytesTransfered = sizeof(pdrvUserInfo->buffer) -1;
    pdrvUserInfo->buffer[nbytesTransfered] = 0;
    if(status!=asynSuccess) return status;
    errno = 0;
    nitems = sscanf(pdrvUserInfo->buffer,pdrvUserInfo->format,value);
    if(nitems<0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:readInt32 sscanf error %s\n",
            pechoPvt->portName,strerror(errno));
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "sscanf error %s",strerror(errno));
        return asynError;
    }
    if(nitems==0) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s echoDriver:readInt32 sscanf converted nothing\n",
            pechoPvt->portName);
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "sscanf convert nothing ");
        return asynError;
    }
    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "%s echoDriver:readInt32 value %d\n",pechoPvt->portName,*value);
    return asynSuccess;
}

static asynStatus getBoundsInt32(void *drvPvt,asynUser *pasynUser,
                                 epicsInt32 *low, epicsInt32 *high)
{
    *low = *high = 0;
    return asynSuccess;
}

/* register echoDriverInit*/
static const iocshArg echoDriverInitArg0 = { "portName", iocshArgString };
static const iocshArg echoDriverInitArg1 = { "delay", iocshArgDouble };
static const iocshArg echoDriverInitArg2 = { "disable auto-connect", iocshArgInt };
static const iocshArg echoDriverInitArg3 = { "multiDevice", iocshArgInt };
static const iocshArg *echoDriverInitArgs[] = {
    &echoDriverInitArg0,&echoDriverInitArg1,
    &echoDriverInitArg2,&echoDriverInitArg3};
static const iocshFuncDef echoDriverInitFuncDef = {
    "echoDriverInit", 4, echoDriverInitArgs};
static void echoDriverInitCallFunc(const iocshArgBuf *args)
{
    echoDriverInit(args[0].sval,args[1].dval,args[2].ival,args[3].ival);
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
