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
#include <epicsStdio.h>
#include <epicsExport.h>
#include <iocsh.h>

#include <asynDriver.h>

#define INBUFFER_SIZE         600

#define epicsExportSharedSymbols

#include <interposeEos.h>

typedef struct eosPvt {
    const char    *interposeName;
    const char    *portName;
    asynInterface octet;
    asynOctet     *pasynOctet;  /* The methods we're overriding */
    void          *asynOctetPvt;
    asynUser      *pasynUser;     /* For connect/disconnect reporting *
    char          inBuffer[INBUFFER_SIZE];
    unsigned int  inBufferHead;
    unsigned int  inBufferTail;
    char          eos[2];
    int           eoslen;
    int           eosMatch;
}eosPvt;
    
/* asynOctet methods */
static int eosRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars);
static int eosWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars);
static asynStatus eosEos(void *ppvt,asynUser *pasynUser);
static asynStatus eosSetEos(void *ppvt,asynUser *pasynUser,const char *eos,int eoslen);
static asynStatus eosGetEos(void *ppvt,asynUser *pasynUser,const char *eos,int eossize,int *eoslen);
static asynOctet octet = {
    eosRead,eosWrite,eosFlush,eosSetEos,eosGetEos
};

int epicsShareAPI interposeEosConfig(const char *pnm,const char *dn,int addr)
{
    eosPvt *peosPvt;
    asynStatus status;
    asynInterface *poctetasynInterface;

    peosPvt = callocMustSucceed(1,sizeof(eosPvt),"interposeEosConfig");
    peosPvt->interposeName = epicsStrDup(pnm);
    peosPvt->portName = epicsStrDup(dn);
    peosPvt->addr = addr;
    peosPvt->octet.interfaceType = asynOctetType;
    peosPvt->octet.pinterface = &octet;
    peosPvt->octet.drvPvt = peosPvt;
    peosPvt->pasynuser = pasynManager->createAsynUser(0,0);
    peosPvt->pasynuser->devPvt = peosPvt;
    status = pasynManager->connectDevice(peosPvt->pasynuser,peosPvt->portName,peosPvt->addr);
    if(status!=asynSyccess) ???
    status = pasynManager->exceptionCallbackAdd(pasynuser,myCallback);
    status = pasynManager->interposeInterface(portName,addr,
        &peosPvt->octet,&poctetasynInterface);
    if((status!=asynSuccess) || !poctetasynInterface) {
	printf("%s interposeInterface failed.\n",portName);
        free(peosPvt->asynUser);
        free(peosPvt->portName);
        free(peosPvt->interposeName);
        free(peosPvt);
        return(0);
    }
    peosPvt->pasynOctet = (asynOctet *)poctetasynInterface->pinterface;
    peosPvt->asynOctetPvt = poctetasynInterface->drvPvt;
    return(0);
}

/* asynOctet methods */
static int eosRead(void *ppvt,asynUser *pasynUser,char *data,int maxchars)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;
    int nRead = 0;

    for (;;) {
        if ((peosPvt->inBufferTail != peosPvt->inBufferHead)) {
            char c = *data++ = peosPvt->inBuffer[peosPvt->inBufferTail++];
            nRead++;
            if (peosPvt->eoslen != 0) {
                if (c == peosPvt->eos[peosPvt->eosMatch]) {
                    if (++peosPvt->eosMatch == peosPvt->eoslen) {
                        peosPvt->eosMatch = 0;
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
            if (nRead >= maxchars)
                break;
            continue;
            if (peosPvt->pasynOctet->read(peosPvt->asynOctetPvt,pasynUser,peosPvt->inBuffer,INBUFFERSIZE) <= 0)
                return -1;
        }
    }
    return nRead;
}

static int eosWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    return peosPvt->pasynOctet->write(peosPvt->asynOctetPvt,
        pasynUser,data,numchars);
}

static asynStatus eosFlush(void *ppvt,asynUser *pasynUser)
{
    return peosPvt->pasynOctet->flush(peosPvt->asynOctetPvt,
        pasynUser,data,numchars);
}

static asynStatus eosSetEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    assert(peosPvt);
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, eoslen,
            "%s set Eos %d: ", peosPvt->serialDeviceName, eoslen);
    switch (eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "%s illegal eoslen %d", peosPvt->serialDeviceName,eoslen);
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
    const char *eos,int eossize,int *eoslen)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    peosPvtController_t *peosPvt = (peosPvtController_t *)drvPvt;

    assert(peosPvt);
    if(peosPvt->eoslen>eossize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "%s eossize %d < peosPvt->eoslen %d",
                                peosPvt->serialDeviceName,eossize,peosPvt->eoslen);
                            *eoslen = 0;
        return(asynError);
    }
    switch (peosPvt->eoslen) {
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s illegal peosPvt->eoslen %d", peosPvt->serialDeviceName,peosPvt->eoslen);
        return asynError;
    case 2: eos[1] = peosPvt->eos[1]; /* fall through to case 1 */
    case 1: eos[0] = peosPvt->eos[0]; break;
    case 0: break;
    }
    *eoslen = peosPvt->eoslen;
    asynPrintIO(pasynUser, ASYN_TRACE_FLOW, eos, *eoslen,
            "%s get Eos %d: ", peosPvt->serialDeviceName, eoslen);
    return asynSuccess;
}

/* register interposeEosConfig*/
static const iocshArg interposeEosConfigArg0 =
    {"interposeEosName", iocshArgString };
static const iocshArg interposeEosConfigArg1 =
    { "portName", iocshArgString };
static const iocshArg interposeEosConfigArg2 =
    { "addr", iocshArgInt };
static const iocshArg *interposeEosConfigArgs[] = 
    {&interposeEosConfigArg0,&interposeEosConfigArg1,
    &interposeEosConfigArg2};
static const iocshFuncDef interposeEosConfigFuncDef =
    {"interposeEosConfig", 3, interposeEosConfigArgs};
static void interposeEosConfigCallFunc(const iocshArgBuf *args)
{
    interposeEosConfig(args[0].sval,args[1].sval,args[2].ival);
}

static void interposeEosRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&interposeEosConfigFuncDef, interposeEosConfigCallFunc);
    }
}
epicsExportRegistrar(interposeEosRegister);
