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
    const char    *portName;
    asynInterface eosInterfae;
    asynOctet     *plowerLevelMethods;  /* The methods we're overriding */
    void          *lowerLevelPvt;
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
static asynOctet eosMethods = {
    eosRead,eosWrite,eosFlush,eosSetEos,eosGetEos
};

int epicsShareAPI interposeEosConfig(const char *portName,int addr)
{
    eosPvt *peosPvt;
    asynStatus status;
    asynInterface *plowerLevelInterface;

    peosPvt = callocMustSucceed(1,sizeof(eosPvt),"interposeEosConfig");
    peosPvt->portName = epicsStrDup( portName);
    peosPvt->addr = addr;
    peosPvt->eosInterfae.interfaceType = asynOctetType;
    peosPvt->eosInterfae.pinterface = &eosMethods;
    peosPvt->eosInterfae.drvPvt = peosPvt;
    peosPvt->pasynuser = pasynManager->createAsynUser(0,0);
    peosPvt->pasynuser->devPvt = peosPvt;
    if ((pasynManager->connectDevice(peosPvt->pasynuser,peosPvt->portName,
                                           peosPvt->addr) != asynSuccess)
     || (pasynManager->exceptionCallbackAdd(pasynuser,
                                           eosCallback) != asynSuccess)
     || (pasynManager->interposeInterface(portName,addr,&peosPvt->eosInterfae,
                                           &plowerLevelInterface) != asynSuccess)
     || (plowerLevelInterface == NULL))
        printf("%s interposeInterface failed.\n",portName);
        free(peosPvt->asynUser);
        free(peosPvt->portName);
        free(peosPvt);
        return(-1);
    }
    peosPvt->plowerLevelMethods = (asynOctet *)plowerLevelInterface->pinterface;
    peosPvt->lowerLevelPvt = plowerLevelInterface->drvPvt;
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
            if (peosPvt->plowerLevelMethods->read(peosPvt->lowerLevelPvt,pasynUser,peosPvt->inBuffer,INBUFFERSIZE) <= 0)
                return -1;
        }
    }
    return nRead;
}

static int eosWrite(void *ppvt,asynUser *pasynUser,const char *data,int numchars)
{
    eosPvt *peosPvt = (eosPvt *)ppvt;

    return peosPvt->plowerLevelMethods->write(peosPvt->lowerLevelPvt,
        pasynUser,data,numchars);
}

static asynStatus eosFlush(void *ppvt,asynUser *pasynUser)
{
    return peosPvt->plowerLevelMethods->flush(peosPvt->lowerLevelPvt,
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
    { "portName", iocshArgString };
static const iocshArg interposeEosConfigArg1 =
    { "addr", iocshArgInt };
static const iocshArg *interposeEosConfigArgs[] = 
    {&interposeEosConfigArg0,&interposeEosConfigArg1};
static const iocshFuncDef interposeEosConfigFuncDef =
    {"interposeEosConfig", 2, interposeEosConfigArgs};
static void interposeEosConfigCallFunc(const iocshArgBuf *args)
{
    interposeEosConfig(args[0].sval,args[1].ival);
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
