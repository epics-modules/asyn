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
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>
#include <asynOctet.h>

#define INBUFFER_SIZE         600

#define epicsExportSharedSymbols

#include <asynInterposeEos.h>

typedef struct eosPvt {
    const char    *portName;
    asynInterface eosInterface;
    asynOctet     *plowerLevelMethods;  /* The methods we're overriding */
    void          *lowerLevelPvt;
    asynUser      *pasynUser;     /* For connect/disconnect reporting */
    char          inBuffer[INBUFFER_SIZE];
    unsigned int  inBufferHead;
    unsigned int  inBufferTail;
    char          eos[2];
    int           eoslen;
    int           eosMatch;
}eosPvt;
    
/* Connect/disconnect handling */
static void eosExceptionHandler(asynUser *pasynUser,asynException exception);

/* asynOctet methods */
static asynStatus eosRead(void *ppvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason);
static asynStatus eosWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered);
static asynStatus eosFlush(void *ppvt,asynUser *pasynUser);
static asynStatus eosSetEos(void *ppvt,asynUser *pasynUser,const char *eos,int eoslen);
static asynStatus eosGetEos(void *ppvt,asynUser *pasynUser,char *eos,int eossize,int *eoslen);
static asynOctet eosMethods = {
    eosRead,eosWrite,eosFlush,eosSetEos,eosGetEos
};

int epicsShareAPI asynInterposeEosConfig(const char *portName,int addr)
{
    eosPvt *peosPvt;
    asynInterface *plowerLevelInterface;

    peosPvt = callocMustSucceed(1,sizeof(eosPvt),"asynInterposeEosConfig");
    peosPvt->portName = epicsStrDup( portName);
    peosPvt->eosInterface.interfaceType = asynOctetType;
    peosPvt->eosInterface.pinterface = &eosMethods;
    peosPvt->eosInterface.drvPvt = peosPvt;
    peosPvt->pasynUser = pasynManager->createAsynUser(0,0);
    peosPvt->pasynUser->userPvt = peosPvt;
    if ((pasynManager->connectDevice(peosPvt->pasynUser,peosPvt->portName,
                                                       addr) != asynSuccess)
     || (pasynManager->exceptionCallbackAdd(peosPvt->pasynUser,
                                           eosExceptionHandler) != asynSuccess)
     || (pasynManager->interposeInterface(portName,addr,&peosPvt->eosInterface,
                                           &plowerLevelInterface) != asynSuccess)
     || (plowerLevelInterface == NULL)) {
        printf("%s interposeInterface failed.\n",portName);
        free(peosPvt->pasynUser);
        free((void *)peosPvt->portName);
        free(peosPvt);
        return(-1);
    }
    peosPvt->plowerLevelMethods = (asynOctet *)plowerLevelInterface->pinterface;
    peosPvt->lowerLevelPvt = plowerLevelInterface->drvPvt;
    return(0);
}

static void eosExceptionHandler(asynUser *pasynUser,asynException exception)
{
    eosPvt *peosPvt = (eosPvt *)pasynUser->userPvt;

    if (exception == asynExceptionConnect) {
        peosPvt->inBufferHead = 0;
        peosPvt->inBufferTail = 0;
        peosPvt->eosMatch = 0;
    }
}

/* asynOctet methods */
static asynStatus eosRead(void *ppvt,asynUser *pasynUser,
    char *data,int maxchars,int *nbytesTransfered,int *eomReason)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;
    int thisRead;
    int nRead = 0;
    asynStatus status = asynSuccess;

    if(eomReason) *eomReason = 0;
    for (;;) {
        if ((peosPvt->inBufferTail != peosPvt->inBufferHead)) {
            char c = *data++ = peosPvt->inBuffer[peosPvt->inBufferTail++];
            nRead++;
            if (peosPvt->eoslen > 0) {
                if (c == peosPvt->eos[peosPvt->eosMatch]) {
                    if (++peosPvt->eosMatch == peosPvt->eoslen) {
                        peosPvt->eosMatch = 0;
                        if(eomReason) *eomReason += EOMEOS;
                        break;
                    }
                }
                else {
                    /*
                     * Resynchronize the search.  Since the driver
                     * allows a maximum two-character EOS it doesn't
                     * have to worry about cases like:
                     *    End-of-string is "eeef"
                     *    Input stream so far is "eeeeeeeee"
                     */
                    if (c == peosPvt->eos[0])
                        peosPvt->eosMatch = 1;
                    else
                        peosPvt->eosMatch = 0;
                }
            }
            if (nRead >= maxchars) break;
            continue;
        }
        if(eomReason && *eomReason) break;
        status = peosPvt->plowerLevelMethods->read(peosPvt->lowerLevelPvt,
                        pasynUser,peosPvt->inBuffer,INBUFFER_SIZE,&thisRead,eomReason);
        if(status!=asynSuccess || thisRead==0) break;
        peosPvt->inBufferTail = 0;
        peosPvt->inBufferHead = thisRead;
    }
    *nbytesTransfered = nRead;
    return status;
}

static asynStatus eosWrite(void *ppvt,asynUser *pasynUser,
    const char *data,int numchars,int *nbytesTransfered)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    return peosPvt->plowerLevelMethods->write(peosPvt->lowerLevelPvt,
        pasynUser,data,numchars,nbytesTransfered);
}

static asynStatus eosFlush(void *ppvt,asynUser *pasynUser)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    peosPvt->inBufferHead = 0;
    peosPvt->inBufferTail = 0;
    peosPvt->eosMatch = 0;
    return peosPvt->plowerLevelMethods->flush(peosPvt->lowerLevelPvt,pasynUser);
}

static asynStatus eosSetEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    assert(peosPvt);
    asynPrintIO(pasynUser,ASYN_TRACE_FLOW,eos,eoslen,
            "%s set Eos %d: ",peosPvt->portName, eoslen);
    switch (eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "%s illegal eoslen %d", peosPvt->portName,eoslen);
        return asynError;
    case 2: peosPvt->eos[1] = eos[1]; /* fall through to case 1 */
    case 1: peosPvt->eos[0] = eos[0]; break;
    case 0: break;
    }
    peosPvt->eoslen = eoslen;
    peosPvt->eosMatch = 0;
    return asynSuccess;
}

static asynStatus eosGetEos(void *ppvt,asynUser *pasynUser,
    char *eos,int eossize,int *eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    assert(peosPvt);
    if(peosPvt->eoslen>eossize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "%s eossize %d < peosPvt->eoslen %d",
                                peosPvt->portName,eossize,peosPvt->eoslen);
        return(asynError);
    }
    switch (peosPvt->eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s illegal peosPvt->eoslen %d", peosPvt->portName,peosPvt->eoslen);
        return asynError;
    case 2: eos[1] = peosPvt->eos[1]; /* fall through to case 1 */
    case 1: eos[0] = peosPvt->eos[0]; break;
    case 0: break;
    }
    *eoslen = peosPvt->eoslen;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s get Eos %d: ", peosPvt->portName, eoslen);
    return asynSuccess;
}

/* register asynInterposeEosConfig*/
static const iocshArg asynInterposeEosConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg asynInterposeEosConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg *asynInterposeEosConfigArgs[] = 
    {&asynInterposeEosConfigArg0,&asynInterposeEosConfigArg1};
static const iocshFuncDef asynInterposeEosConfigFuncDef =
    {"asynInterposeEosConfig", 2, asynInterposeEosConfigArgs};
static void asynInterposeEosConfigCallFunc(const iocshArgBuf *args)
{
    asynInterposeEosConfig(args[0].sval,args[1].ival);
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
