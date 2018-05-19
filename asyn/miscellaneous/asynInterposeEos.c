/*asynInterposeEos.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* 
 * End-of-string processing for asyn
 *
 * Author: Eric Norum
 */

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsAssert.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynInterposeEos.h"

#define START_OUTPUT_SIZE 100
#define INPUT_SIZE        2048

typedef struct eosPvt {
    char          *portName;
    asynInterface eosInterface;
    asynOctet     *poctet;  /* The methods we're overriding */
    void          *octetPvt;
    asynUser      *pasynUser;     /* For connect/disconnect reporting */
    int           processEosIn;
    size_t        inBufSize;
    char          *inBuf;
    unsigned int  inBufHead;
    unsigned int  inBufTail;
    char          eosIn[2];
    int           eosInLen;
    int           eosInMatch;
    int           processEosOut;
    size_t        outBufSize;
    char          *outBuf;
    char          eosOut[2];
    int           eosOutLen;
}eosPvt;
    
/* Connect/disconnect handling */
static void eosInExceptionHandler(asynUser *pasynUser,asynException exception);

/* asynOctet methods */
static asynStatus writeIt(void *ppvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered);
static asynStatus readIt(void *ppvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus flushIt(void *ppvt,asynUser *pasynUser);
static asynStatus registerInterruptUser(void *ppvt,asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt,void **registrarPvt);
static asynStatus cancelInterruptUser(void *drvPvt,asynUser *pasynUser,
     void *registrarPvt);
static asynStatus setInputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getInputEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize ,int *eoslen);
static asynStatus setOutputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getOutputEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen);
static asynOctet octet = {
    writeIt,readIt,flushIt,
    registerInterruptUser, cancelInterruptUser,
    setInputEos,getInputEos,setOutputEos,getOutputEos
};

epicsShareFunc int asynInterposeEosConfig(const char *portName,int addr,
    int processEosIn,int processEosOut)
{
    eosPvt        *peosPvt;
    asynInterface *plowerLevelInterface;
    asynStatus    status;
    asynUser      *pasynUser;
    size_t        len;

    len = sizeof(eosPvt) + strlen(portName) + 1;
    peosPvt = callocMustSucceed(1,len,"asynInterposeEosConfig");
    peosPvt->portName = (char *)(peosPvt+1);
    strcpy(peosPvt->portName,portName);
    peosPvt->eosInterface.interfaceType = asynOctetType;
    peosPvt->eosInterface.pinterface = &octet;
    peosPvt->eosInterface.drvPvt = peosPvt;
    pasynUser = pasynManager->createAsynUser(0,0);
    peosPvt->pasynUser = pasynUser;
    peosPvt->pasynUser->userPvt = peosPvt;
    status = pasynManager->connectDevice(pasynUser,portName,addr);
    if(status!=asynSuccess) {
        printf("%s connectDevice failed\n",portName);
        pasynManager->freeAsynUser(pasynUser);
        free(peosPvt);
        return -1;
    }
    status = pasynManager->exceptionCallbackAdd(pasynUser,eosInExceptionHandler);
    if(status!=asynSuccess) {
        printf("%s exceptionCallbackAdd failed\n",portName);
        pasynManager->freeAsynUser(pasynUser);
        free(peosPvt);
        return -1;
    }
    status = pasynManager->interposeInterface(portName,addr,
       &peosPvt->eosInterface,&plowerLevelInterface);
    if(status!=asynSuccess) {
        printf("%s interposeInterface failed\n",portName);
        pasynManager->exceptionCallbackRemove(pasynUser);
        pasynManager->freeAsynUser(pasynUser);
        free(peosPvt);
        return -1;
    }
    peosPvt->poctet = (asynOctet *)plowerLevelInterface->pinterface;
    peosPvt->octetPvt = plowerLevelInterface->drvPvt;
    peosPvt->processEosIn = processEosIn;
    if(processEosIn) {
        peosPvt->inBuf = callocMustSucceed(1,INPUT_SIZE,
            "asynInterposeEosConfig");
        peosPvt->inBufSize = INPUT_SIZE;
    }
    peosPvt->processEosOut = processEosOut;
    if(processEosOut) {
        peosPvt->outBuf = pasynManager->memMalloc(START_OUTPUT_SIZE);
        peosPvt->outBufSize = START_OUTPUT_SIZE;
    }
    return(0);
}

static void eosInExceptionHandler(asynUser *pasynUser,asynException exception)
{
    eosPvt *peosPvt = (eosPvt *)pasynUser->userPvt;

    if (exception == asynExceptionConnect) {
        peosPvt->inBufHead = 0;
        peosPvt->inBufTail = 0;
        peosPvt->eosInMatch = 0;
    }
}

/* asynOctet methods */
static asynStatus writeIt(void *ppvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    eosPvt     *peosPvt = (eosPvt *)ppvt;
    asynStatus status;
    size_t     nbytesActual = 0;

    if(!peosPvt->processEosOut) {
        return peosPvt->poctet->write(peosPvt->octetPvt,
            pasynUser,data,numchars,nbytesTransfered);
    }
    if(peosPvt->outBufSize<(numchars + peosPvt->eosOutLen)) {
        pasynManager->memFree(peosPvt->outBuf,peosPvt->outBufSize);
        peosPvt->outBufSize = numchars + peosPvt->eosOutLen;
        peosPvt->outBuf = pasynManager->memMalloc(peosPvt->outBufSize);
    }
    memcpy(peosPvt->outBuf,data,numchars);
    if(peosPvt->eosOutLen>0) {
        memcpy(&peosPvt->outBuf[numchars],peosPvt->eosOut,peosPvt->eosOutLen);
    }
    status = peosPvt->poctet->write(peosPvt->octetPvt, pasynUser,
         peosPvt->outBuf,(numchars + peosPvt->eosOutLen),&nbytesActual);
    if (status!=asynError)
        asynPrintIO(pasynUser,ASYN_TRACEIO_FILTER,peosPvt->outBuf,nbytesActual,
                "%s wrote\n",peosPvt->portName);
    *nbytesTransfered = (nbytesActual>numchars) ? numchars : nbytesActual;
    return status;
}

static asynStatus readIt(void *ppvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;
    size_t thisRead;
    size_t nRead = 0;
    int eom = 0;
    asynStatus status = asynSuccess;
    if(!peosPvt->processEosIn) {
        return peosPvt->poctet->read(peosPvt->octetPvt,
            pasynUser,data,maxchars,nbytesTransfered,eomReason);
    }
    for (;;) {
        if ((peosPvt->inBufTail != peosPvt->inBufHead)) {
            char c = *data++ = peosPvt->inBuf[peosPvt->inBufTail++];
            nRead++;
            if (peosPvt->eosInLen > 0) {
                if (c == peosPvt->eosIn[peosPvt->eosInMatch]) {
                    if (++peosPvt->eosInMatch == peosPvt->eosInLen) {
                        peosPvt->eosInMatch = 0;
                        nRead -= peosPvt->eosInLen;
                        data -= peosPvt->eosInLen;
                        *(data+1) = 0;
                        eom |= ASYN_EOM_EOS;
                        break;
                    }
                } else {
                    /*
                     * Resynchronize the search.  Since the driver
                     * allows a maximum two-character EOS it doesn't
                     * have to worry about cases like:
                     *    End-of-string is "eeef"
                     *    Input stream so far is "eeeeeeeee"
                     */
                    if (c == peosPvt->eosIn[0]) {
                        peosPvt->eosInMatch = 1;
                    } else {
                        peosPvt->eosInMatch = 0;
                    }
                }
            }
            if (nRead >= maxchars)  {
                eom |= ASYN_EOM_CNT;
                break;
            }
            continue;
        }
        if(eom) break;
        status = peosPvt->poctet->read(peosPvt->octetPvt,
             pasynUser,peosPvt->inBuf,peosPvt->inBufSize,&thisRead,&eom);
        if(status==asynSuccess) {
            asynPrintIO(pasynUser,ASYN_TRACEIO_FILTER,peosPvt->inBuf,thisRead,
                "%s read %d bytes eom=%d\n",peosPvt->portName, thisRead, eom);
            /*
             * Read could have returned with ASYN_EOM_CNT set in eom because
             * the number of octets available exceeded inBufSize.  This is not
             * a reason for us to stop reading.
             */
            eom &= ~ASYN_EOM_CNT;
        } else {
           asynPrint(pasynUser, ASYN_TRACE_WARNING, "%s read from low-level driver returned %d\n",
               peosPvt->portName, status);
        }
        if(status!=asynSuccess || thisRead==0) break;
        peosPvt->inBufTail = 0;
        peosPvt->inBufHead = (int)thisRead;
    }
    if(nRead<maxchars) *data = 0; /*null terminate string if room*/
    if (eomReason) *eomReason = eom;
    *nbytesTransfered = nRead;
    return status;
}

static asynStatus flushIt(void *ppvt,asynUser *pasynUser)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    if(!peosPvt->processEosIn) {
        return peosPvt->poctet->flush(peosPvt->octetPvt,pasynUser);
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW, "%s flush\n",peosPvt->portName);
    peosPvt->inBufHead = 0;
    peosPvt->inBufTail = 0;
    peosPvt->eosInMatch = 0;
    return peosPvt->poctet->flush(peosPvt->octetPvt,pasynUser);
}

static asynStatus registerInterruptUser(void *ppvt,asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt,void **registrarPvt)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    return peosPvt->poctet->registerInterruptUser(peosPvt->octetPvt,
        pasynUser,callback,userPvt,registrarPvt);
} 

static asynStatus cancelInterruptUser(void *drvPvt,asynUser *pasynUser,
     void *registrarPvt)
{
    eosPvt *peosPvt = (eosPvt *)drvPvt;

    return peosPvt->poctet->cancelInterruptUser(peosPvt->octetPvt,
        pasynUser,registrarPvt);
} 

static asynStatus setInputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    if(!peosPvt->processEosIn) {
        return peosPvt->poctet->setInputEos(peosPvt->octetPvt,pasynUser,
           eos,eoslen);
    }
    asynPrintIO(pasynUser,ASYN_TRACE_FLOW,eos,eoslen,
            "%s set Eos %d\n",peosPvt->portName, eoslen);
    switch (eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "%s illegal eoslen %d", peosPvt->portName,eoslen);
        return asynError;
    case 2: peosPvt->eosIn[1] = eos[1]; /* fall through to case 1 */
    case 1: peosPvt->eosIn[0] = eos[0]; break;
    case 0: break;
    }
    peosPvt->eosInLen = eoslen;
    peosPvt->eosInMatch = 0;
    return asynSuccess;
}

static asynStatus getInputEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    if(!peosPvt->processEosIn) {
        return peosPvt->poctet->getInputEos(peosPvt->octetPvt,pasynUser,
           eos,eossize,eoslen);
    }
    if(peosPvt->eosInLen>eossize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "%s eossize %d < peosPvt->eoslen %d",
                                peosPvt->portName,eossize,peosPvt->eosInLen);
        return(asynError);
    }
    switch (peosPvt->eosInLen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s illegal peosPvt->eosInLen %d", peosPvt->portName,peosPvt->eosInLen);
        return asynError;
    case 2: eos[1] = peosPvt->eosIn[1]; /* fall through to case 1 */
    case 1: eos[0] = peosPvt->eosIn[0]; break;
    case 0: break;
    }
    *eoslen = peosPvt->eosInLen;
    if(peosPvt->eosInLen<eossize) eos[peosPvt->eosInLen] = 0;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s get Eos %d\n", peosPvt->portName, *eoslen);
    return asynSuccess;
}

static asynStatus setOutputEos(void *ppvt,asynUser *pasynUser,
    const char *eos, int eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    assert(peosPvt);
    asynPrintIO(pasynUser,ASYN_TRACE_FLOW,eos,eoslen,
            "%s set Eos %d\n",peosPvt->portName, eoslen);
    switch (eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "%s illegal eoslen %d", peosPvt->portName,eoslen);
        return asynError;
    case 2: peosPvt->eosOut[1] = eos[1]; /* fall through to case 1 */
    case 1: peosPvt->eosOut[0] = eos[0]; break;
    case 0: break;
    }
    peosPvt->eosOutLen = eoslen;
    return asynSuccess;
}

static asynStatus getOutputEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    assert(peosPvt);
    if(peosPvt->eosOutLen>eossize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "%s eossize %d < peosPvt->eosOutLen %d",
                                peosPvt->portName,eossize,peosPvt->eosOutLen);
        return(asynError);
    }
    switch (peosPvt->eosOutLen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s illegal peosPvt->eosOutLen %d", peosPvt->portName,peosPvt->eosOutLen);
        return asynError;
    case 2: eos[1] = peosPvt->eosOut[1]; /* fall through to case 1 */
    case 1: eos[0] = peosPvt->eosOut[0]; break;
    case 0: break;
    }
    *eoslen = peosPvt->eosOutLen;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s get Eos %d\n", peosPvt->portName, *eoslen);
    return asynSuccess;
}

/* register asynInterposeEosConfig*/
static const iocshArg asynInterposeEosConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg asynInterposeEosConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg asynInterposeEosConfigArg2 =
    { "processIn (0,1) => (no,yes)", iocshArgInt };
static const iocshArg asynInterposeEosConfigArg3 =
    { "processOut (0,1) => (no,yes)", iocshArgInt };
static const iocshArg *asynInterposeEosConfigArgs[] = 
    {&asynInterposeEosConfigArg0,&asynInterposeEosConfigArg1,
     &asynInterposeEosConfigArg2,&asynInterposeEosConfigArg3};
static const iocshFuncDef asynInterposeEosConfigFuncDef =
    {"asynInterposeEosConfig", 4, asynInterposeEosConfigArgs};
static void asynInterposeEosConfigCallFunc(const iocshArgBuf *args)
{
    asynInterposeEosConfig(args[0].sval,args[1].ival,
          args[2].ival,args[3].ival);
}

static void asynInterposeEosRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynInterposeEosConfigFuncDef, asynInterposeEosConfigCallFunc);
    }
}
epicsExportRegistrar(asynInterposeEosRegister);
