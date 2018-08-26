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

#include <epicsExit.h>
#include <epicsThread.h>
#include <epicsEvent.h>
#include <epicsString.h>
#include <epicsStdioRedirect.h>
#include <iocsh.h>
#include <gpHash.h>
#include <registryFunction.h>

#ifdef _WIN32
  #include <windows.h>
#endif

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynOctetSyncIO.h"
#include "asynShellCommands.h"
#include <epicsExport.h>

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
    asynOption     *pasynOption;
    void           *drvPvt;
    epicsEventId   done;
}setOptionArgs;

typedef struct showOptionArgs {
    const char     *key;
    char            val[100];
    asynOption     *pasynOption;
    void           *drvPvt;
    epicsEventId   done;
}showOptionArgs;

static asynStatus
findInterface(const char *portName, int addr, const char *interfaceType,
                        void (*handler)(asynUser *),
                        asynUser **ppasynUser, asynInterface **ppasynInterface)
{
    if (portName == NULL) {
        printf("Missing portName argument\n");
        return asynError;
    }
    *ppasynUser = pasynManager->createAsynUser(handler, 0);
    if (pasynManager->connectDevice(*ppasynUser, portName, addr) != asynSuccess) {
        printf("Port %s connectDevice failed: %s\n", portName, (*ppasynUser)->errorMessage);
        pasynManager->freeAsynUser(*ppasynUser);
        return asynError;
    }
    *ppasynInterface = pasynManager->findInterface(*ppasynUser, interfaceType, 1);
    if (!*ppasynInterface) {
        printf("Port %s does not provide required %s interface\n", portName, interfaceType);
        pasynManager->freeAsynUser(*ppasynUser);
        return asynError;
    }
    return asynSuccess;
}

static void setOption(asynUser *pasynUser)
{
    setOptionArgs *poptionargs = (setOptionArgs *)pasynUser->userPvt;
    asynStatus status;

    status = poptionargs->pasynOption->setOption(poptionargs->drvPvt,
                                pasynUser, poptionargs->key, poptionargs->val);
    if(status!=asynSuccess) 
        printf("setOption failed %s\n", pasynUser->errorMessage);
    epicsEventSignal(poptionargs->done);
}

epicsShareFunc int
 asynSetOption(const char *portName, int addr, const char *key, const char *val)
{
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    setOptionArgs optionargs;

    if ((key == NULL) || (val == NULL)) {
        printf("Missing key/value argument\n");
        return asynError;
    }
    if (findInterface(portName, addr, asynOptionType, setOption,
                                    &pasynUser, &pasynInterface) != asynSuccess)
        return asynError;
    pasynUser->timeout = 2;
    pasynUser->userPvt = &optionargs;
    pasynUser->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
    optionargs.pasynOption = (asynOption *)pasynInterface->pinterface;
    optionargs. drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.val = val;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    if (pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0) != asynSuccess) {
        printf("queueRequest failed: %s\n", pasynUser->errorMessage);
        epicsEventDestroy(optionargs.done);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return asynSuccess;
}

static void showOption(asynUser *pasynUser)
{
    showOptionArgs *poptionargs = (showOptionArgs *)pasynUser->userPvt;
    asynStatus status;

    status = poptionargs->pasynOption->getOption(poptionargs->drvPvt,
        pasynUser, poptionargs->key, poptionargs->val, sizeof(poptionargs->val));
    if(status != asynSuccess)
        printf("getOption failed %s\n", pasynUser->errorMessage);
    else
        printf("%s=%s\n", poptionargs->key, poptionargs->val);
    epicsEventSignal(poptionargs->done);
}

epicsShareFunc int
 asynShowOption(const char *portName, int addr,const char *key)
{
    asynInterface *pasynInterface;
    showOptionArgs optionargs;
    asynUser *pasynUser;

    if (key == NULL) {
        printf("Missing key argument\n");
        return asynError;
    }
    if (findInterface(portName, addr, asynOptionType, showOption,
                                    &pasynUser, &pasynInterface) != asynSuccess)
        return asynError;
    pasynUser->timeout = 2;
    pasynUser->userPvt = &optionargs;
    pasynUser->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
    optionargs.pasynOption = (asynOption *)pasynInterface->pinterface;
    optionargs.drvPvt = pasynInterface->drvPvt;
    optionargs.key = key;
    optionargs.done = epicsEventMustCreate(epicsEventEmpty);
    if (pasynManager->queueRequest(pasynUser,asynQueuePriorityConnect,0.0) != asynSuccess) {
        printf("queueRequest failed: %s\n", pasynUser->errorMessage);
        epicsEventDestroy(optionargs.done);
        pasynManager->freeAsynUser(pasynUser);
        return asynError;
    }
    epicsEventWait(optionargs.done);
    epicsEventDestroy(optionargs.done);
    pasynManager->freeAsynUser(pasynUser);
    return asynSuccess;
}

enum eosType { eosIn, eosOut };
typedef struct eosArgs {
    enum eosType    type;
    char            eos[10];
    size_t          eosLen;
    asynOctet      *pasynOctet;
    void           *drvPvt;
    asynStatus      status;
    epicsEventId    done;
} eosArgs;

static void
setEos(asynUser *pasynUser)
{
    eosArgs *peosargs = (eosArgs *)pasynUser->userPvt;
    asynStatus (*setFunc)(void *drvPvt, asynUser *pasynUser, const char *eos, int eoslen);

    switch(peosargs->type) {
    case eosIn:  setFunc = peosargs->pasynOctet->setInputEos;    break;
    case eosOut: setFunc = peosargs->pasynOctet->setOutputEos;   break;
    default:     setFunc = NULL;                                 break;
    }
    if (setFunc) {
        peosargs->status = (*setFunc)(peosargs->drvPvt, pasynUser, peosargs->eos, (int)peosargs->eosLen);
    }
    else {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                            "No way to set EOS");
        peosargs->status = asynError;
    }
    epicsEventSignal(peosargs->done);
}

static asynStatus
asynSetEos(const char *portName, int addr, enum eosType type, const char *eos)
{
    asynUser *pasynUser;
    asynInterface *pasynInterface;
    eosArgs eosargs;

    if (eos == NULL) {
        printf("Missing EOS argument\n");
        return asynError;
    }
    eosargs.eosLen = epicsStrnRawFromEscaped(eosargs.eos, sizeof eosargs.eos, eos, strlen(eos));
    if (eosargs.eosLen >= sizeof eosargs.eos) {
        printf("End of string argument \"%s\" too long.\n", eos);
        return asynError;
    }
    if (findInterface(portName, addr, asynOctetType, setEos,
                                        &pasynUser, &pasynInterface) != asynSuccess)
        return asynError;
    pasynUser->timeout = 2;
    pasynUser->userPvt = &eosargs;
    pasynUser->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
    eosargs.pasynOctet = (asynOctet *)pasynInterface->pinterface;
    eosargs. drvPvt = pasynInterface->drvPvt;
    eosargs.type = type;
    eosargs.done = epicsEventMustCreate(epicsEventEmpty);
    eosargs.status = pasynManager->queueRequest(pasynUser, asynQueuePriorityConnect, 0.0);
    if (eosargs.status == asynSuccess)
        epicsEventWait(eosargs.done);
    epicsEventDestroy(eosargs.done);
    if (eosargs.status != asynSuccess)
        printf("Set EOS failed: %s\n", pasynUser->errorMessage);
    pasynManager->freeAsynUser(pasynUser);
    return eosargs.status;
}

static void
getEos(asynUser *pasynUser)
{
    eosArgs *peosargs = (eosArgs *)pasynUser->userPvt;
    asynStatus (*getFunc)(void *drvPvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen);

    switch(peosargs->type) {
    case eosIn:  getFunc = peosargs->pasynOctet->getInputEos;     break;
    case eosOut: getFunc = peosargs->pasynOctet->getOutputEos;    break;
    default:     getFunc = NULL;                                  break;
    }
    if (getFunc) {
        int l;
        peosargs->status = (*getFunc)(peosargs->drvPvt, pasynUser, peosargs->eos, sizeof peosargs->eos, &l);
        peosargs->eosLen = l;
    }
    else {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                            "No way to get EOS");
        peosargs->status = asynError;
    }
    epicsEventSignal(peosargs->done);
}

static int
asynShowEos(const char *portName, int addr, enum eosType type)
{
    asynInterface *pasynInterface;
    eosArgs eosargs;
    asynUser *pasynUser;

    if (findInterface(portName, addr, asynOctetType, getEos,
                                        &pasynUser, &pasynInterface) != asynSuccess)
        return asynError;
    pasynUser->timeout = 2;
    pasynUser->userPvt = &eosargs;
    pasynUser->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
    eosargs.pasynOctet = (asynOctet *)pasynInterface->pinterface;
    eosargs. drvPvt = pasynInterface->drvPvt;
    eosargs.type = type;
    eosargs.done = epicsEventMustCreate(epicsEventEmpty);
    eosargs.status = pasynManager->queueRequest(pasynUser, asynQueuePriorityConnect, 0.0);
    if (eosargs.status == asynSuccess)
        epicsEventWait(eosargs.done);
    epicsEventDestroy(eosargs.done);
    if (eosargs.status != asynSuccess)
        printf("Set EOS failed: %s\n", pasynUser->errorMessage);
    pasynManager->freeAsynUser(pasynUser);
    if (eosargs.status == asynSuccess) {
        char cbuf[4*sizeof eosargs.eos + 2];
        epicsStrnEscapedFromRaw(cbuf, sizeof cbuf, eosargs.eos, eosargs.eosLen);
        printf("\"%s\"\n", cbuf);
    }
    return eosargs.status;
}

/* Default timeout for reading */
#define READ_TIMEOUT 1.0
/* Default buffer size  */
#define BUFFER_SIZE 160

static struct gphPvt* asynHash=NULL;

static asynIOPvt* asynFindEntry(const char *name)
{
    GPHENTRY *hashEntry;

    /* Create hash table if it does not exist */
    if (asynHash == NULL) gphInitPvt(&asynHash, 256);
    if(name==0) return NULL;
    hashEntry = gphFind(asynHash, name, NULL);
    if (hashEntry == NULL) return (NULL);
    return((asynIOPvt *)hashEntry->userPvt);
}

epicsShareFunc int
    asynOctetConnect(const char *entry, const char *port, int addr,
             int timeout, int buffer_len, const char *drvInfo)
{
    asynIOPvt *pPvt;
    asynUser *pasynUser;
    asynStatus status;
    GPHENTRY *hashEntry;

    pPvt = asynFindEntry(entry);
    if (pPvt) {
       printf("Entry already connected\n");
       return(-1);
    }

    status = pasynOctetSyncIO->connect(port, addr, &pasynUser,drvInfo);
    if (status) {
        printf("connect failed %s\n",pasynUser->errorMessage);
        pasynOctetSyncIO->disconnect(pasynUser);
        return(-1);
    }
    hashEntry = gphAdd(asynHash, epicsStrDup(entry), NULL);
    pPvt = (asynIOPvt *)calloc(1, sizeof(asynIOPvt)); 
    hashEntry->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;
    pPvt->timeout = timeout ? (double)timeout : READ_TIMEOUT;
    pPvt->write_buffer_len = buffer_len ? buffer_len : BUFFER_SIZE;
    pPvt->write_buffer = calloc(1, pPvt->write_buffer_len);
    pPvt->read_buffer_len = pPvt->write_buffer_len;
    pPvt->read_buffer = calloc(1, pPvt->read_buffer_len);
    return(0);
}

epicsShareFunc int
    asynOctetDisconnect(const char *entry)
{
    asynIOPvt *pPvt;
    asynUser *pasynUser;
    asynStatus status;
    GPHENTRY *hashEntry;

    /* Create hash table if it does not exist */
    if (asynHash == NULL) gphInitPvt(&asynHash, 256);
    if(entry==0) {
        printf("device name not specified\n");
        return -1;
    }
    hashEntry = gphFind(asynHash, entry, NULL);
    if (hashEntry == NULL) {
        printf("device name not found\n");
        return -1;
    }
    pPvt = (asynIOPvt *)hashEntry->userPvt;
    pasynUser = pPvt->pasynUser;
    status = pasynOctetSyncIO->disconnect(pasynUser);
    if (status) {
        printf("disconnect failed %s\n",pasynUser->errorMessage);
        return(-1);
    }
    gphDelete(asynHash,entry,NULL);
    free(pPvt->write_buffer);
    free(pPvt->read_buffer);
    free(pPvt);
    return(0);
}

epicsShareFunc int
    asynOctetRead(const char *entry, int nread)
{
    asynStatus status;
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    size_t ninp = 0;
    int eomReason;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if (nread == 0) nread = pPvt->read_buffer_len;
    if (nread > pPvt->read_buffer_len) nread = pPvt->read_buffer_len;
    status = pasynOctetSyncIO->read(pasynUser, pPvt->read_buffer, (size_t)nread,
        pPvt->timeout,&ninp,&eomReason);
    if (status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error reading, ninp=%lu error %s\n", (unsigned long)ninp,pasynUser->errorMessage);
       return(-1);
    }
    fprintf(stdout,"eomReason 0x%x\n",eomReason);
    epicsStrPrintEscaped(stdout, pPvt->read_buffer, ninp);
    fprintf(stdout,"\n");
    return (int)ninp;
}

epicsShareFunc int
    asynOctetWrite(const char *entry, const char *output)
{
    asynStatus status;
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    size_t nout = 0;
    size_t len;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if (strlen(output) > (size_t)pPvt->write_buffer_len) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error writing, buffer too small\n");
       return(-1);
    }
    len = dbTranslateEscape(pPvt->write_buffer, output);
    status = pasynOctetSyncIO->write(pasynUser, pPvt->write_buffer,
        len, pPvt->timeout,&nout);
    if (status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error in asynOctetWrite, nout=%lu, len=%lu error %s\n",
                 (unsigned long)nout, (unsigned long)len, pasynUser->errorMessage);
       return(-1);
    }
    return (int)nout;
}

epicsShareFunc int
    asynOctetWriteRead(const char *entry, const char *output, int nread)
{
    asynStatus status;
    asynUser *pasynUser;
    asynIOPvt *pPvt;
    size_t nout = 0;
    size_t ninp = 0;
    size_t len;
    int eomReason;

    pPvt = asynFindEntry(entry);
    if (!pPvt) {
       printf("Entry not found\n");
       return(-1);
    }
    pasynUser = pPvt->pasynUser;

    if (strlen(output) > (size_t)pPvt->write_buffer_len) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error writing, buffer too small\n");
       return(-1);
    }
    len = dbTranslateEscape(pPvt->write_buffer, output);
    if (nread == 0) nread = pPvt->read_buffer_len;
    if (nread > pPvt->read_buffer_len) nread = pPvt->read_buffer_len;
    status = pasynOctetSyncIO->writeRead(pasynUser, pPvt->write_buffer, len,
                                  pPvt->read_buffer, (size_t)nread,
                                  pPvt->timeout,
                                  &nout,&ninp,&eomReason);
    if (status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "Error in WriteRead, nout %lu ninp=%lu error %s\n",
           (unsigned long)nout, (unsigned long)ninp, pasynUser->errorMessage);
       return(-1);
    }
    fprintf(stdout,"eomReason 0x%x\n",eomReason);
    epicsStrPrintEscaped(stdout, pPvt->read_buffer, ninp);
    fprintf(stdout,"\n");
    return (int)ninp;
}

epicsShareFunc int
    asynOctetFlush(const char *entry)
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

    status = pasynOctetSyncIO->flush(pasynUser);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
                 "Error in asynFlush, status=%d\n", status);
       return(-1);
    }
    return(0);
}

epicsShareFunc int
    asynOctetSetInputEos(const char *portName, int addr, const char *eosin)
{
   return asynSetEos(portName, addr, eosIn, eosin) == asynSuccess ? 0 : -1;
}

epicsShareFunc int
    asynOctetGetInputEos(const char *portName, int addr)
{
   return asynShowEos(portName, addr, eosIn) == asynSuccess ? 0 : -1;
}

epicsShareFunc int
    asynOctetSetOutputEos(const char *portName, int addr, const char *eosout)
{
   return asynSetEos(portName, addr, eosOut, eosout)==asynSuccess ? 0 : -1;
}

epicsShareFunc int
    asynOctetGetOutputEos(const char *portName, int addr)
{
   return asynShowEos(portName, addr, eosOut) == asynSuccess ? 0 : -1;
}

epicsShareFunc int
    asynWaitConnect(const char *portName, double timeout)
{
    asynUser      *pasynUser=NULL;
    int           isConnected=0;
    asynStatus    status;
 
    pasynUser = pasynManager->createAsynUser(0,0);
    if (!pasynUser) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
            "waitConnect: port=%s error calling createAsynUser\n", 
            portName);
        return asynError;
    }
    status = pasynManager->connectDevice(pasynUser, portName, -1);
    if (status) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
            "waitConnect: port=%s error calling connectDevice\n",
            portName);
        return asynError;
    }
    status = pasynManager->waitConnect(pasynUser, timeout);
    if (status == asynSuccess) isConnected = 1;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, 
        "waitConnect: port=%s exit, isConnected=%d\n", 
        portName, isConnected);
    pasynManager->freeAsynUser(pasynUser);
    if (isConnected) return asynSuccess; 
    return asynError;
}


static const iocshArg asynReportArg0 = {"level", iocshArgInt};
static const iocshArg asynReportArg1 = {"port", iocshArgString};
static const iocshArg *const asynReportArgs[] = {
    &asynReportArg0,&asynReportArg1};
static const iocshFuncDef asynReportDef = {"asynReport", 2, asynReportArgs};
epicsShareFunc int
 asynReport(int level, const char *portName)
{
    pasynManager->report(stdout,level,portName);
    return 0;
}
static void asynReportCall(const iocshArgBuf * args) {
    asynReport(args[0].ival,args[1].sval);
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
static const iocshArg asynShowOptionArg1 = {"addr", iocshArgInt};
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
epicsShareFunc int
 asynSetTraceMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser=NULL;
    asynStatus status;

    if (portName && (strlen(portName) > 0)) {
        pasynUser = pasynManager->createAsynUser(0,0);
        status = pasynManager->connectDevice(pasynUser,portName,addr);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynManager->freeAsynUser(pasynUser);
            return -1;
        }
    }
    status = pasynTrace->setTraceMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    if (pasynUser) pasynManager->freeAsynUser(pasynUser);
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
epicsShareFunc int
 asynSetTraceIOMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser=NULL;
    asynStatus status;

    if (portName && (strlen(portName) > 0)) {
        pasynUser = pasynManager->createAsynUser(0,0);
        status = pasynManager->connectDevice(pasynUser,portName,addr);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynManager->freeAsynUser(pasynUser);
            return -1;
        }
    }
    status = pasynTrace->setTraceIOMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    if (pasynUser) pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceIOMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceIOMask(portName,addr,mask);
}

static const iocshArg asynSetTraceInfoMaskArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceInfoMaskArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceInfoMaskArg2 = {"mask", iocshArgInt};
static const iocshArg *const asynSetTraceInfoMaskArgs[] = {
    &asynSetTraceInfoMaskArg0,&asynSetTraceInfoMaskArg1,&asynSetTraceInfoMaskArg2};
static const iocshFuncDef asynSetTraceInfoMaskDef =
    {"asynSetTraceInfoMask", 3, asynSetTraceInfoMaskArgs};
epicsShareFunc int
 asynSetTraceInfoMask(const char *portName,int addr,int mask)
{
    asynUser *pasynUser=NULL;
    asynStatus status;

    if (portName && (strlen(portName) > 0)) {
        pasynUser = pasynManager->createAsynUser(0,0);
        status = pasynManager->connectDevice(pasynUser,portName,addr);
        if(status!=asynSuccess) {
            printf("%s\n",pasynUser->errorMessage);
            pasynManager->freeAsynUser(pasynUser);
            return -1;
        }
    }
    status = pasynTrace->setTraceInfoMask(pasynUser,mask);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    if (pasynUser) pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceInfoMaskCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int mask = args[2].ival;
    asynSetTraceInfoMask(portName,addr,mask);
}

epicsShareFunc int
 asynSetTraceFile(const char *portName,int addr,const char *filename)
{
    asynUser        *pasynUser;
    asynStatus      status;
    FILE            *fp;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if((status!=asynSuccess) && (strlen(portName)!=0)) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    if(!filename) {
        fp = 0;
    } else if(strlen(filename)==0 || strcmp(filename,"stderr")==0) {
        fp = stderr;
    } else if(strcmp(filename,"stdout")==0) {
        fp = stdout;
    } else {
        fp = fopen(filename,"w");
        if(!fp) {
            printf("fopen failed %s\n",strerror(errno));
            goto done;
        }
    }
    status = pasynTrace->setTraceFile(pasynUser,fp);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
done:
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}

static const iocshArg asynSetTraceFileArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceFileArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceFileArg2 = {"filename", iocshArgString};
static const iocshArg *const asynSetTraceFileArgs[] = {
    &asynSetTraceFileArg0,&asynSetTraceFileArg1,&asynSetTraceFileArg2};
static const iocshFuncDef asynSetTraceFileDef =
    {"asynSetTraceFile", 3, asynSetTraceFileArgs};
static void asynSetTraceFileCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    const char *filename = args[2].sval;
    asynSetTraceFile(portName,addr,filename);
}

static const iocshArg asynSetTraceIOTruncateSizeArg0 = {"portName", iocshArgString};
static const iocshArg asynSetTraceIOTruncateSizeArg1 = {"addr", iocshArgInt};
static const iocshArg asynSetTraceIOTruncateSizeArg2 = {"size", iocshArgInt};
static const iocshArg *const asynSetTraceIOTruncateSizeArgs[] = {
    &asynSetTraceIOTruncateSizeArg0,&asynSetTraceIOTruncateSizeArg1,&asynSetTraceIOTruncateSizeArg2};
static const iocshFuncDef asynSetTraceIOTruncateSizeDef =
    {"asynSetTraceIOTruncateSize", 3, asynSetTraceIOTruncateSizeArgs};
epicsShareFunc int
 asynSetTraceIOTruncateSize(const char *portName,int addr,int size)
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
    status = pasynTrace->setTraceIOTruncateSize(pasynUser,size);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetTraceIOTruncateSizeCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    int addr = args[1].ival;
    int size = args[2].ival;
    asynSetTraceIOTruncateSize(portName,addr,size);
}

static const iocshArg asynEnableArg0 = {"portName", iocshArgString};
static const iocshArg asynEnableArg1 = {"addr", iocshArgInt};
static const iocshArg asynEnableArg2 = {"yesNo", iocshArgInt};
static const iocshArg *const asynEnableArgs[] = {
    &asynEnableArg0,&asynEnableArg1,&asynEnableArg2};
static const iocshFuncDef asynEnableDef =
    {"asynEnable", 3, asynEnableArgs};
epicsShareFunc int
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
epicsShareFunc int
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

static const iocshArg asynOctetConnectArg0 = {"device name", iocshArgString};
static const iocshArg asynOctetConnectArg1 = {"asyn portName", iocshArgString};
static const iocshArg asynOctetConnectArg2 = {"asyn addr (default=0)", iocshArgInt};
static const iocshArg asynOctetConnectArg3 = {"timeout (sec) (default=1)", iocshArgInt};
static const iocshArg asynOctetConnectArg4 = {"buffer length (default=80)", iocshArgInt};
static const iocshArg asynOctetConnectArg5 = {"drvInfo", iocshArgString};
static const iocshArg *const asynOctetConnectArgs[] = {
    &asynOctetConnectArg0, &asynOctetConnectArg1, &asynOctetConnectArg2,
    &asynOctetConnectArg3, &asynOctetConnectArg4, &asynOctetConnectArg5};
static const iocshFuncDef asynOctetConnectDef =
    {"asynOctetConnect", 6, asynOctetConnectArgs};
static void asynOctetConnectCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *portName   = args[1].sval;
    int addr               = args[2].ival;
    int timeout            = args[3].ival;
    int buffer_len         = args[4].ival;
    const char *drvInfo    = args[5].sval;
    asynOctetConnect(deviceName, portName, addr, timeout, buffer_len,drvInfo);
}

static const iocshArg asynOctetDisconnectArg0 = {"device name", iocshArgString};
static const iocshArg *const asynOctetDisconnectArgs[] = { &asynOctetDisconnectArg0};
static const iocshFuncDef asynOctetDisconnectDef =
    {"asynOctetDisconnect", 1, asynOctetDisconnectArgs};
static void asynOctetDisconnectCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    asynOctetDisconnect(deviceName);
}

static const iocshArg asynOctetReadArg0 = {"device name", iocshArgString};
static const iocshArg asynOctetReadArg1 = {"max. bytes", iocshArgInt};
static const iocshArg *const asynOctetReadArgs[] = {
    &asynOctetReadArg0, &asynOctetReadArg1};
static const iocshFuncDef asynOctetReadDef =
    {"asynOctetRead", 2, asynOctetReadArgs};
static void asynOctetReadCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    int nread              = args[1].ival;
    asynOctetRead(deviceName, nread);
}

static const iocshArg asynOctetWriteArg0 = {"device name", iocshArgString};
static const iocshArg asynOctetWriteArg1 = {"output string", iocshArgString};
static const iocshArg *const asynOctetWriteArgs[] = {
    &asynOctetWriteArg0, &asynOctetWriteArg1};
static const iocshFuncDef asynOctetWriteDef =
    {"asynOctetWrite", 2, asynOctetWriteArgs};
static void asynOctetWriteCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *output     = args[1].sval;
    asynOctetWrite(deviceName, output);
}

static const iocshArg asynOctetWriteReadArg0 = {"device name", iocshArgString};
static const iocshArg asynOctetWriteReadArg1 = {"output string", iocshArgString};
static const iocshArg asynOctetWriteReadArg2 = {"max. bytes", iocshArgInt};
static const iocshArg *const asynOctetWriteReadArgs[] = {
    &asynOctetWriteReadArg0, &asynOctetWriteReadArg1, &asynOctetWriteReadArg2};
static const iocshFuncDef asynOctetWriteReadDef =
    {"asynOctetWriteRead", 3, asynOctetWriteReadArgs};
static void asynOctetWriteReadCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    const char *output     = args[1].sval;
    int nread              = args[2].ival;
    asynOctetWriteRead(deviceName, output, nread);
}

static const iocshArg asynOctetFlushArg0 = {"device name", iocshArgString};
static const iocshArg *const asynOctetFlushArgs[] = {&asynOctetFlushArg0};
static const iocshFuncDef asynOctetFlushDef =
    {"asynOctetFlush", 1, asynOctetFlushArgs};
static void asynOctetFlushCall(const iocshArgBuf * args) {
    const char *deviceName = args[0].sval;
    asynOctetFlush(deviceName);
}

static const iocshArg asynOctetSetInputEosArg0 = {"portName", iocshArgString};
static const iocshArg asynOctetSetInputEosArg1 = {"addr", iocshArgInt};
static const iocshArg asynOctetSetInputEosArg2 = {"eos", iocshArgString};
static const iocshArg *const asynOctetSetInputEosArgs[] = {
    &asynOctetSetInputEosArg0, &asynOctetSetInputEosArg1,
    &asynOctetSetInputEosArg2};
static const iocshFuncDef asynOctetSetInputEosDef =
    {"asynOctetSetInputEos", 3, asynOctetSetInputEosArgs};
static void asynOctetSetInputEosCall(const iocshArgBuf * args) {
    const char *portName   = args[0].sval;
    int addr               = args[1].ival;
    const char *eos        = args[2].sval;
    asynOctetSetInputEos( portName, addr, eos);
}

static const iocshArg asynOctetGetInputEosArg0 = {"portName", iocshArgString};
static const iocshArg asynOctetGetInputEosArg1 = {"addr", iocshArgInt};
static const iocshArg *const asynOctetGetInputEosArgs[] = {
    &asynOctetGetInputEosArg0, &asynOctetGetInputEosArg1};
static const iocshFuncDef asynOctetGetInputEosDef =
    {"asynOctetGetInputEos", 2, asynOctetGetInputEosArgs};
static void asynOctetGetInputEosCall(const iocshArgBuf * args) {
    const char *portName   = args[0].sval;
    int addr               = args[1].ival;
    asynOctetGetInputEos( portName, addr);
}

static const iocshArg asynOctetSetOutputEosArg0 = {"portName", iocshArgString};
static const iocshArg asynOctetSetOutputEosArg1 = {"addr", iocshArgInt};
static const iocshArg asynOctetSetOutputEosArg2 = {"eos", iocshArgString};
static const iocshArg *const asynOctetSetOutputEosArgs[] = {
    &asynOctetSetOutputEosArg0, &asynOctetSetOutputEosArg1,
    &asynOctetSetOutputEosArg2};
static const iocshFuncDef asynOctetSetOutputEosDef =
    {"asynOctetSetOutputEos", 3, asynOctetSetOutputEosArgs};
static void asynOctetSetOutputEosCall(const iocshArgBuf * args) {
    const char *portName   = args[0].sval;
    int addr               = args[1].ival;
    const char *eos        = args[2].sval;
    asynOctetSetOutputEos( portName, addr, eos);
}

static const iocshArg asynOctetGetOutputEosArg0 = {"portName", iocshArgString};
static const iocshArg asynOctetGetOutputEosArg1 = {"addr", iocshArgInt};
static const iocshArg *const asynOctetGetOutputEosArgs[] = {
    &asynOctetGetOutputEosArg0, &asynOctetGetOutputEosArg1};
static const iocshFuncDef asynOctetGetOutputEosDef =
    {"asynOctetGetOutputEos", 2, asynOctetGetOutputEosArgs};
static void asynOctetGetOutputEosCall(const iocshArgBuf * args) {
    const char *portName   = args[0].sval;
    int addr               = args[1].ival;
    asynOctetGetOutputEos( portName, addr);
}

static const iocshArg asynWaitConnectArg0 = {"portName", iocshArgString};
static const iocshArg asynWaitConnectArg1 = {"timeout", iocshArgDouble};
static const iocshArg *const asynWaitConnectArgs[] = {
    &asynWaitConnectArg0, &asynWaitConnectArg1};
static const iocshFuncDef asynWaitConnectDef =
    {"asynWaitConnect", 2, asynWaitConnectArgs};
static void asynWaitConnectCall(const iocshArgBuf * args) {
    const char *portName   = args[0].sval;
    double timeout         = args[1].dval;
    asynWaitConnect( portName, timeout);
}

static const iocshArg asynSetAutoConnectTimeoutArg0 = {"timeout", iocshArgDouble};
static const iocshArg *const asynSetAutoConnectTimeoutArgs[] = {
    &asynSetAutoConnectTimeoutArg0};
static const iocshFuncDef asynSetAutoConnectTimeoutDef =
    {"asynSetAutoConnectTimeout", 1, asynSetAutoConnectTimeoutArgs};
static void asynSetAutoConnectTimeoutCall(const iocshArgBuf * args) {
    double timeout = args[0].dval;
    pasynManager->setAutoConnectTimeout(timeout);
}

static const iocshArg asynRegisterTimeStampSourceArg0 = { "portName",iocshArgString};
static const iocshArg asynRegisterTimeStampSourceArg1 = { "functionName",iocshArgString};
static const iocshArg * const asynRegisterTimeStampSourceArgs[] = {
    &asynRegisterTimeStampSourceArg0, &asynRegisterTimeStampSourceArg1};
static const iocshFuncDef asynRegisterTimeStampSourceDef = 
    {"asynRegisterTimeStampSource",2,asynRegisterTimeStampSourceArgs};
static const iocshFuncDef asynUnregisterTimeStampSourceDef = 
    {"asynUnregisterTimeStampSource",1,asynRegisterTimeStampSourceArgs};

static void asynRegisterTimeStampSourceCall(const iocshArgBuf *args)
{
    asynRegisterTimeStampSource(args[0].sval, args[1].sval);
}

static void asynUnregisterTimeStampSourceCall(const iocshArgBuf *args)
{
    asynUnregisterTimeStampSource(args[0].sval);
}
epicsShareFunc int asynRegisterTimeStampSource(const char *portName, const char *functionName)
{
    asynUser *pasynUser;
    asynStatus status;
    timeStampCallback pFunction;
    
    if (!portName || !functionName || (strlen(portName) == 0) || (strlen(functionName) == 0)) {
        printf("Usage: registerUserTimeStampSource portName functionName\n");
        return -1;
    }
    pasynUser = pasynManager->createAsynUser(0, 0);
    status = pasynManager->connectDevice(pasynUser, portName, 0);
    if (status) {
        printf("asynRegisterUserStampSource, cannot connect to port %s\n", portName);
        return -1;
    }
    pFunction = (timeStampCallback)registryFunctionFind(functionName);
    if (!pFunction) {
        printf("asynRegisterUserStampSource, cannot find function %s\n", functionName);
        return -1;
    }
    pasynManager->registerTimeStampSource(pasynUser, 0, pFunction);
    return 0;
}

epicsShareFunc int asynUnregisterTimeStampSource(const char *portName)
{
    asynUser *pasynUser;
    asynStatus status;
    
    if (!portName || (strlen(portName) == 0)) {
        printf("Usage: asynUnregisterTimeStampSource portName\n");
        return -1;
    }
    pasynUser = pasynManager->createAsynUser(0, 0);
    status = pasynManager->connectDevice(pasynUser, portName, 0);
    if (status) {
        printf("asynUnregisterTimeStampSource, cannot connect to port %s\n", portName);
        return -1;
    }
    pasynManager->unregisterTimeStampSource(pasynUser);
    return 0;
}

#ifdef _WIN32
static unsigned int minPeriodMs;

static void resetTimer(void *Pvt)
{
    /* printf("Calling timeEndPeriod(%d)\n", minPeriodMs); */
    timeEndPeriod(minPeriodMs);
}

epicsShareFunc int asynSetMinTimerPeriod(double minPeriod)
{
    TIMECAPS timeCaps;
    MMRESULT status;
   
    if (minPeriodMs == 0) { /* First time */
        epicsAtExit(resetTimer, 0);
    } else {
        resetTimer(0);
    }
    
    timeGetDevCaps(&timeCaps, sizeof(timeCaps));
    /* printf("Minimum time=%d, maximum time=%d, sleepQuantum=%f\n", 
      timeCaps.wPeriodMin, timeCaps.wPeriodMax, epicsThreadSleepQuantum()); */
    minPeriodMs = (int)(minPeriod*1000. + 0.5);
    if (minPeriodMs < timeCaps.wPeriodMin) minPeriodMs = timeCaps.wPeriodMin;
    if (minPeriodMs > timeCaps.wPeriodMax) minPeriodMs = timeCaps.wPeriodMax;
    status = timeBeginPeriod(minPeriodMs);
    /* printf("Called timePeriodBegin(%d), status=%d, sleepQuantum=%f\n", 
      minPeriodMs, status, epicsThreadSleepQuantum()); */
    return (int)status; 
}

#else /* _WIN32 */

epicsShareFunc int asynSetMinTimerPeriod(double minPeriod)
{
    printf("asynSetMinTimerPeriod is not currently supported on this OS\n");
    return -1;
}

#endif /* _WIN32 */

static const iocshArg asynSetMinTimerPeriodArg0 = { "minimum period", iocshArgDouble };
static const iocshArg *asynSetMinTimerPeriodArgs[] = 
    {&asynSetMinTimerPeriodArg0};
static const iocshFuncDef asynSetMinTimerPeriodDef =
    {"asynSetMinTimerPeriod", 1, asynSetMinTimerPeriodArgs};
static void asynSetMinTimerPeriodCall(const iocshArgBuf *args)
{
    asynSetMinTimerPeriod(args[0].dval);
}

static const iocshArg asynSetQueueLockPortTimeoutArg0 = {"portName", iocshArgString};
static const iocshArg asynSetQueueLockPortTimeoutArg1 = {"timeout", iocshArgDouble};
static const iocshArg *const asynSetQueueLockPortTimeoutArgs[] = {
    &asynSetQueueLockPortTimeoutArg0,&asynSetQueueLockPortTimeoutArg1};
static const iocshFuncDef asynSetQueueLockPortTimeoutDef =
    {"asynSetQueueLockPortTimeout", 2, asynSetQueueLockPortTimeoutArgs};
epicsShareFunc int
 asynSetQueueLockPortTimeout(const char *portName, double timeout)
{
    asynUser *pasynUser;
    asynStatus status;

    pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(pasynUser,portName,0);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
        pasynManager->freeAsynUser(pasynUser);
        return -1;
    }
    status = pasynManager->setQueueLockPortTimeout(pasynUser,timeout);
    if(status!=asynSuccess) {
        printf("%s\n",pasynUser->errorMessage);
    }
    pasynManager->freeAsynUser(pasynUser);
    return 0;
}
static void asynSetQueueLockPortTimeoutCall(const iocshArgBuf * args) {
    const char *portName = args[0].sval;
    double timeout = args[1].dval;
    asynSetQueueLockPortTimeout(portName,timeout);
}

static void asynRegister(void)
{
    static int firstTime = 1;
    if(!firstTime) return;
    firstTime = 0;
    iocshRegister(&asynReportDef,asynReportCall);
    iocshRegister(&asynSetOptionDef,asynSetOptionCall);
    iocshRegister(&asynShowOptionDef,asynShowOptionCall);
    iocshRegister(&asynSetTraceMaskDef,asynSetTraceMaskCall);
    iocshRegister(&asynSetTraceIOMaskDef,asynSetTraceIOMaskCall);
    iocshRegister(&asynSetTraceInfoMaskDef,asynSetTraceInfoMaskCall);
    iocshRegister(&asynSetTraceFileDef,asynSetTraceFileCall);
    iocshRegister(&asynSetTraceIOTruncateSizeDef,asynSetTraceIOTruncateSizeCall);
    iocshRegister(&asynEnableDef,asynEnableCall);
    iocshRegister(&asynAutoConnectDef,asynAutoConnectCall);
    iocshRegister(&asynSetQueueLockPortTimeoutDef,asynSetQueueLockPortTimeoutCall);
    iocshRegister(&asynOctetConnectDef,asynOctetConnectCall);
    iocshRegister(&asynOctetDisconnectDef,asynOctetDisconnectCall);
    iocshRegister(&asynOctetReadDef,asynOctetReadCall);
    iocshRegister(&asynOctetWriteDef,asynOctetWriteCall);
    iocshRegister(&asynOctetWriteReadDef,asynOctetWriteReadCall);
    iocshRegister(&asynOctetFlushDef,asynOctetFlushCall);
    iocshRegister(&asynOctetSetInputEosDef,asynOctetSetInputEosCall);
    iocshRegister(&asynOctetGetInputEosDef,asynOctetGetInputEosCall);
    iocshRegister(&asynOctetSetOutputEosDef,asynOctetSetOutputEosCall);
    iocshRegister(&asynOctetGetOutputEosDef,asynOctetGetOutputEosCall);
    iocshRegister(&asynWaitConnectDef,asynWaitConnectCall);
    iocshRegister(&asynSetAutoConnectTimeoutDef,asynSetAutoConnectTimeoutCall);
    iocshRegister(&asynRegisterTimeStampSourceDef, asynRegisterTimeStampSourceCall);
    iocshRegister(&asynUnregisterTimeStampSourceDef, asynUnregisterTimeStampSourceCall);
    iocshRegister(&asynSetMinTimerPeriodDef, asynSetMinTimerPeriodCall);
}
epicsExportRegistrar(asynRegister);
