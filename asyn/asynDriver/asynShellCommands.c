/* asynShellCommands.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Author: Marty Kraimer */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>

#include <epicsEvent.h>
#include <epicsExport.h>
#include <epicsString.h>
#include <iocsh.h>
#include <gpHash.h>
#include "asynDriver.h"
#include "asynSyncIO.h"

#define epicsExportSharedSymbols

#include "asynShellCommands.h"

#define MAX_EOS_LEN 10
typedef struct asynIOPvt {
   asynUser *pasynUser;
   char ieos[MAX_EOS_LEN];
   int  ieos_len;
   char oeos[MAX_EOS_LEN];
   int  oeos_len;
   double timeout;
   char *write_buffer;
   int write_buffer_len;
   char *read_buffer;
   int read_buffer_len;
} asynIOPvt;

typedef struct setOptionArgs {
    const char     *key;
    const char     *val;
    asynCommon     *pasynCommon;
    void           *drvPvt;
    epicsEventId   done;
}setOptionArgs;

typedef struct showOptionArgs {
    const char     *key;
    asynCommon     *pasynCommon;
    void           *drvPvt;
    epicsEventId   done;
}showOptionArgs;

static void setOption(asynUser *pasynUser)
{
    setOptionArgs *poptionargs = (setOptionArgs *)pasynUser->userPvt;
    asynStatus status;

    status = poptionargs->pasynCommon->setOption(poptionargs->drvPvt,
            pasynUser,poptionargs->key,poptionargs->val);
    if(status!=asynSuccess) 
        printf("setOption failed %s\n",pasynUser->errorMessage);
    epicsEventSignal(poptionargs->done);
}

int epicsShareAPI
 asynSetOption(const char *portName, int addr, const char *key, const char *val)
{
    asynInterface *pasynInterface;
    setOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) || (val == NULL)) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(setOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.val = val;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        printf("queueRequest failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

static void showOption(asynUser *pasynUser)
{
    showOptionArgs *poptionargs = (showOptionArgs *)pasynUser->userPvt;
    asynStatus status;
    char val[100];

    pasynUser->errorMessage[0] = '\0';
    status = poptionargs->pasynCommon->getOption(poptionargs->drvPvt,
        pasynUser,poptionargs->key,val,sizeof(val));
    if(status!=asynSuccess) {
        printf("getOption failed %s\n",pasynUser->errorMessage);
    } else {
        printf("%s=%s\n",poptionargs->key,val);
    }
    epicsEventSignal(poptionargs->done);
}

int epicsShareAPI
 asynShowOption(const char *portName, int addr,const char *key)
{
    asynInterface *pasynInterface;
    showOptionArgs optionargs;
    asynUser *pasynUser;
    asynStatus status;

    if ((portName == NULL) || (key == NULL) ) {
        printf("Missing argument\n");
        return asynError;
    }
    pasynUser = pasynManager->createAsynUser(showOption,0);
    pasynUser->userPvt = &optionargs;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("connectDevice failed %s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return 1;
    }
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,0);
    if(!pasynInterface) {
        printf("port %s not found\n",portName);
        return asynError;
    }
    optionargs.pasynCommon = (asynCommon *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    status = pasynManager->queueRequest(pasynUser,0,0.0);
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

/* Default timeout for reading */
#define READ_TIMEOUT 1.0
/* Default buffer size for reads */
#define BUFFER_SIZE 80

static void* asynHash=NULL;

static asynIOPvt* asynFindEntry(const char *name)
{
    GPHENTRY *hashEntry = gphFind(asynHash, name, NULL);
    if (hashEntry == NULL) return (NULL);
    return((asynIOPvt *)hashEntry->userPvt);
}

epicsShareFunc int epicsShareAPI
    asynConnect(const char *entry, const char *port, int addr,
             const char *oeos, const char *ieos, int timeout, int buffer_len)
{
    asynIOPvt *pPvt;
    asynUser *pasynUser;
    asynStatus status;
    GPHENTRY *hashEntry;

    status = pasynSyncIO->connect(port, addr, &pasynUser);
    if (status) {
       printf("Error calling pasynSyncIO->connect, status=%d\n", status);
       return(-1);
    }

    /* Create hash table if it does not exist */
    if (asynHash == NULL) gphInitPvt(&asynHash, 256);
    hashEntry = gphAdd(asynHash, epicsStrDup(entry), NULL);

    pPvt = (asynIOPvt *)calloc(1, sizeof(asynIOPvt)); 
    hashEntry->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    if (ieos == NULL) ieos =  "\r";
    pPvt->ieos_len = dbTranslateEscape(pPvt->ieos, ieos);
    if (oeos == NULL) oeos =  "\r";
    pPvt->oeos_len = dbTranslateEscape(pPvt->oeos, oeos);
    pPvt->timeout = timeout ? (double)timeout : READ_TIMEOUT;
    pPvt->write_buffer_len = buffer_len ? buffer_len : BUFFER_SIZE;
    pPvt->write_buffer = calloc(1, pPvt->write_buffer_len);
    pPvt->read_buffer_len = pPvt->write_buffer_len;
    pPvt->read_buffer = calloc(1, pPvt->read_buffer_len);
    return(0);
}

epicsShareFunc int epicsShareAPI
    asynRead(const char *entry, int nread, int flush)
{
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    int ninp;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if (nread == 0) nread = pPvt->read_buffer_len;
    if (nread > pPvt->read_buffer_len) nread = pPvt->read_buffer_len;
    ninp = pasynSyncIO->read(pasynUser, pPvt->read_buffer, nread,
                   pPvt->ieos, pPvt->ieos_len, flush, pPvt->timeout);
    if (ninp <= 0) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error reading, ninp=%d\n", ninp);
       return(-1);
    }
    epicsStrPrintEscaped(stdout, pPvt->read_buffer, ninp);
    fprintf(stdout,"\n");
    return(ninp);
}

epicsShareFunc int epicsShareAPI
    asynWrite(const char *entry, const char *output)
{
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    int nout;
    int len;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if ((strlen(output) + pPvt->oeos_len) > pPvt->write_buffer_len) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error writing, buffer too small\n");
       return(-1);
    }
    len = dbTranslateEscape(pPvt->write_buffer, output);
    strcat(pPvt->write_buffer, pPvt->oeos);
    len += pPvt->oeos_len;
    nout = pasynSyncIO->write(pasynUser, pPvt->write_buffer, len, pPvt->timeout);
    if (nout != len) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error in asynWrite, nout=%d, len=%d\n", nout, len);
       return(-1);
    }
    return(nout);
}

epicsShareFunc int epicsShareAPI
    asynWriteRead(const char *entry, const char *output, int nread)
{
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    int ninp;
    int len;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if ((strlen(output) + pPvt->oeos_len) > pPvt->write_buffer_len) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error writing, buffer too small\n");
       return(-1);
    }
    len = dbTranslateEscape(pPvt->write_buffer, output);
    strcat(pPvt->write_buffer, pPvt->oeos);
    len += pPvt->oeos_len;
    if (nread == 0) nread = pPvt->read_buffer_len;
    if (nread > pPvt->read_buffer_len) nread = pPvt->read_buffer_len;
    ninp = pasynSyncIO->writeRead(pasynUser, pPvt->write_buffer, len,
                                  pPvt->read_buffer, nread,
                                  pPvt->ieos, pPvt->ieos_len, pPvt->timeout);
    if (ninp <= 0) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error in WriteRead, ninp=%d\n", ninp);
       return(-1);
    }
    epicsStrPrintEscaped(stdout, pPvt->read_buffer, ninp);
    fprintf(stdout,"\n");
    return(ninp);
}

epicsShareFunc int epicsShareAPI
    asynFlush(const char *entry)
{
    asynIOPvt *pPvt;
    asynUser *pasynUser;
    asynStatus status;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    status = pasynSyncIO->flush(pasynUser);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error in asynFlush, status=%d\n", status);
       return(-1);
    }
    return(0);
}


static const iocshArg asynReportArg0 = {"filename", iocshArgString};
static const iocshArg asynReportArg1 = {"level", iocshArgInt};
static const iocshArg *const asynReportArgs[] = {&asynReportArg0,&asynReportArg1};
static const iocshFuncDef asynReportDef = {"asynReport", 2, asynReportArgs};
int epicsShareAPI
 asynReport(const char *filename, int level)
{
    FILE *fp;

    if(!filename || filename[0]==0) {
        fp = stdout;
    } else {
        fp = fopen(filename,"w+");
        if(!fp) {
            printf("fopen failed %s\n",strerror(errno));
            return -1;
        }
    }
    pasynManager->report(fp,level);
    if(fp!=stdout)  {
        int status;

        errno = 0;
        status = fclose(fp);
        if(status) fprintf(stderr,"asynReport fclose error %s\n",strerror(errno));
    }
    return 0;
}
static void asynReportCall(const iocshArgBuf * args) {
    asynReport(args[0].sval,args[1].ival);
}

static const iocshArg asynSetOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynSetOptionArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetOptionArg2 = {"key", iocshArgString};
static const iocshArg asynSetOptionArg3 = {"value", iocshArgString};
static const iocshArg *const asynSetOptionArgs[] = {
              &asynSetOptionArg0, &asynSetOptionArg1,
              &asynSetOptionArg2,&asynSetOptionArg3};
static const iocshFuncDef asynSetOptionDef = {"asynSetOption", 4, asynSetOptionArgs};
static void asynSetOptionCall(const iocshArgBuf * args) {
    asynSetOption(args[0].sval,args[1].ival,args[2].sval,args[3].sval);
}

static const iocshArg asynShowOptionArg0 = {"portName", iocshArgString};
static const iocshArg asynShowOptionArg1 = {"addr", iocshArgString};
static const iocshArg asynShowOptionArg2 = {"key", iocshArgString};
static const iocshArg *const asynShowOptionArgs[] = {
              &asynShowOptionArg0, &asynShowOptionArg1,&asynShowOptionArg2};
static const iocshFuncDef asynShowOptionDef = {"asynShowOption", 3, asynShowOptionArgs};
static void asynShowOptionCall(const iocshArgBuf * args) {
    asynShowOption(args[0].sval,args[1].ival,args[2].sval);
}

static const iocshArg asynSetTraceMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceMaskArgs[] = {
    &asynSetTraceMaskArg0,&asynSetTraceMaskArg1,&asynSetTraceMaskArg2};
static const iocshFuncDef asynSetTraceMaskDef =
    {"asynSetTraceMask", 3, asynSetTraceMaskArgs};
int epicsShareAPI
 asynSetTraceMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if((status!=asynSuccess) && (strlen(portName)!=0)) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynTrace->setTraceMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceMask(portName,addr,mask);
}

static const iocshArg asynSetTraceIOMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceIOMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceIOMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceIOMaskArgs[] = {
    &asynSetTraceIOMaskArg0,&asynSetTraceIOMaskArg1,&asynSetTraceIOMaskArg2};
static const iocshFuncDef asynSetTraceIOMaskDef =
    {"asynSetTraceIOMask", 3, asynSetTraceIOMaskArgs};
int epicsShareAPI
 asynSetTraceIOMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynTrace->setTraceIOMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceIOMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceIOMask(portName,addr,mask);
}

static const iocshArg asynEnableArg0 = {"portName", iocshArgString};
static const iocshArg asynEnableArg1 = {"addr", iocshArgInt};
static const iocshArg asynEnableArg2 = {"yesNo", iocshArgInt};
static const iocshArg *const asynEnableArgs[] = {
    &asynEnableArg0,&asynEnableArg1,&asynEnableArg2};
static const iocshFuncDef asynEnableDef =
    {"asynEnable", 3, asynEnableArgs};
int epicsShareAPI
 asynEnable(const char *portName,int addr,int yesNo)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynManager->enable(pasynUser,yesNo);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynEnableCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int yesNo = args[2].ival;
    asynEnable(portName,addr,yesNo);
}

static const iocshArg asynAutoConnectArg0 = {"portName", iocshArgString};
static const iocshArg asynAutoConnectArg1 = {"addr", iocshArgInt};
static const iocshArg asynAutoConnectArg2 = {"yesNo", iocshArgInt};
static const iocshArg *const asynAutoConnectArgs[] = {
    &asynAutoConnectArg0,&asynAutoConnectArg1,&asynAutoConnectArg2};
static const iocshFuncDef asynAutoConnectDef =
    {"asynAutoConnect", 3, asynAutoConnectArgs};
int epicsShareAPI
 asynAutoConnect(const char *portName,int addr,int yesNo)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynManager->autoConnect(pasynUser,yesNo);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynAutoConnectCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int yesNo = args[2].ival;
    asynAutoConnect(portName,addr,yesNo);
}

static const iocshArg asynConnectArg0 = {"device name", iocshArgString};
static const iocshArg asynConnectArg1 = {"asyn portName", iocshArgString};
static const iocshArg asynConnectArg2 = {"asyn addr (default=0)", iocshArgInt};
static const iocshArg asynConnectArg3 = {"output eos (default=\\r)", iocshArgString};
static const iocshArg asynConnectArg4 = {"input eos (default=\\r)", iocshArgString};
static const iocshArg asynConnectArg5 = {"timeout (sec) (default=1)", iocshArgInt};
static const iocshArg asynConnectArg6 = {"buffer length (default=80)", iocshArgInt};
static const iocshArg *const asynConnectArgs[] = {
    &asynConnectArg0, &asynConnectArg1, &asynConnectArg2, &asynConnectArg3,
    &asynConnectArg4, &asynConnectArg5, &asynConnectArg6};
static const iocshFuncDef asynConnectDef =
    {"asynConnect", 7, asynConnectArgs};
static void asynConnectCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *portName   = args[1].sval;
    int addr               = args[2].ival;
    const char *oeos       = args[3].sval;
    const char *ieos       = args[4].sval;
    int timeout            = args[5].ival;
    int buffer_len         = args[6].ival;
    asynConnect(deviceName, portName, addr, oeos, ieos, timeout, buffer_len);
}

static const iocshArg asynReadArg0 = {"device name", iocshArgString};
static const iocshArg asynReadArg1 = {"max. bytes", iocshArgInt};
static const iocshArg asynReadArg2 = {"flush (1=yes)", iocshArgInt};
static const iocshArg *const asynReadArgs[] = {
    &asynReadArg0, &asynReadArg1, &asynReadArg2};
static const iocshFuncDef asynReadDef =
    {"asynRead", 3, asynReadArgs};
static void asynReadCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    int nread              = args[1].ival;
    int flush              = args[2].ival;
    asynRead(deviceName, nread, flush);
}

static const iocshArg asynWriteArg0 = {"device name", iocshArgString};
static const iocshArg asynWriteArg1 = {"output string", iocshArgString};
static const iocshArg *const asynWriteArgs[] = {
    &asynWriteArg0, &asynWriteArg1};
static const iocshFuncDef asynWriteDef =
    {"asynWrite", 2, asynWriteArgs};
static void asynWriteCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *output     = args[1].sval;
    asynWrite(deviceName, output);
}

static const iocshArg asynWriteReadArg0 = {"device name", iocshArgString};
static const iocshArg asynWriteReadArg1 = {"output string", iocshArgString};
static const iocshArg asynWriteReadArg2 = {"max. bytes", iocshArgInt};
static const iocshArg *const asynWriteReadArgs[] = {
    &asynWriteReadArg0, &asynWriteReadArg1, &asynWriteReadArg2};
static const iocshFuncDef asynWriteReadDef =
    {"asynWriteRead", 3, asynWriteReadArgs};
static void asynWriteReadCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *output     = args[1].sval;
    int nread              = args[2].ival;
    asynWriteRead(deviceName, output, nread);
}

static const iocshArg asynFlushArg0 = {"device name", iocshArgString};
static const iocshArg *const asynFlushArgs[] = {&asynFlushArg0};
static const iocshFuncDef asynFlushDef =
    {"asynFlush", 1, asynFlushArgs};
static void asynFlushCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    asynFlush(deviceName);
}

static void asyn(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynReportDef,asynReportCall);
    iocshRegister(&asynSetOptionDef,asynSetOptionCall);
    iocshRegister(&asynShowOptionDef,asynShowOptionCall);
    iocshRegister(&asynSetTraceMaskDef,asynSetTraceMaskCall);
    iocshRegister(&asynSetTraceIOMaskDef,asynSetTraceIOMaskCall);
    iocshRegister(&asynEnableDef,asynEnableCall);
    iocshRegister(&asynAutoConnectDef,asynAutoConnectCall);
    iocshRegister(&asynConnectDef,asynConnectCall);
    iocshRegister(&asynReadDef,asynReadCall);
    iocshRegister(&asynWriteDef,asynWriteCall);
    iocshRegister(&asynWriteReadDef,asynWriteReadCall);
    iocshRegister(&asynFlushDef,asynFlushCall);
}
epicsExportRegistrar(asyn);
