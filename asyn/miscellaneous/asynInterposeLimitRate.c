#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>
#include <iocsh.h>
#include <epicsExport.h>
#include <epicsThread.h>

#include <asynDriver.h>
#include <asynOctet.h>
#include "asynInterposeLimitRate.h"

typedef struct limitRatePvt {
    const char*    portName;
    asynInterface  octet;
    asynOctet*     pasynOctet;
    void*          asynOctetPvt;
    double         minInterval;
    epicsTimeStamp lastTime;
} limitRatePvt;

/* asynOctet methods */
static asynStatus writeIt(void* ppvt, asynUser* pasynUser,
    const char* data, size_t numchars, size_t* nbytesTransfered);
static asynStatus readIt(void* ppvt, asynUser* pasynUser,
    char* data, size_t maxchars, size_t* nbytesTransfered, int* eomReason);
static asynStatus flushIt(void *ppvt,asynUser *pasynUser);
static asynStatus registerInterruptUser(void *ppvt,asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt,void **registrarPvt);
static asynStatus cancelInterruptUser(void *ppvt,asynUser *pasynUser,
    void *registrarPvt);
static asynStatus setInputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getInputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(void *ppvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getOutputEos(void *ppvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);

static asynOctet octet = {
    writeIt, readIt, flushIt,
    registerInterruptUser,cancelInterruptUser,
    setInputEos,getInputEos,setOutputEos,getOutputEos
};

/* asynOctet methods */
static asynStatus flushIt(void *ppvt, asynUser *pasynUser)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser,ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::flush\n");
    return pLimitRate->pasynOctet->flush(
        pLimitRate->asynOctetPvt, pasynUser);
}

static asynStatus registerInterruptUser(void *ppvt, asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::registerInterruptUser\n");
    return pLimitRate->pasynOctet->registerInterruptUser(
        pLimitRate->asynOctetPvt, pasynUser, callback, userPvt, registrarPvt);
}

static asynStatus cancelInterruptUser(void* ppvt, asynUser* pasynUser,
    void* registrarPvt)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::cancelInterruptUser\n");
    return pLimitRate->pasynOctet->cancelInterruptUser(
        pLimitRate->asynOctetPvt, pasynUser, registrarPvt);
}

static asynStatus setInputEos(void* ppvt, asynUser* pasynUser,
    const char* eos, int eoslen)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::setInputEos\n");
    return pLimitRate->pasynOctet->setInputEos(pLimitRate->asynOctetPvt,
        pasynUser, eos, eoslen);
}

static asynStatus getInputEos(void* ppvt, asynUser* pasynUser,
    char* eos, int eossize, int* eoslen)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::getInputEos\n");
    return pLimitRate->pasynOctet->getInputEos(pLimitRate->asynOctetPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynStatus setOutputEos(void* ppvt,asynUser* pasynUser,
    const char* eos, int eoslen)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::setOutputEos\n");
    return pLimitRate->pasynOctet->setOutputEos(pLimitRate->asynOctetPvt,
        pasynUser, eos, eoslen);
}

static asynStatus getOutputEos(void* ppvt, asynUser* pasynUser,
    char* eos, int eossize, int* eoslen)
{
    limitRatePvt* pLimitRate = (limitRatePvt*)ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::getOutputEos\n");
    return pLimitRate->pasynOctet->getOutputEos(pLimitRate->asynOctetPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynStatus writeIt(void* ppvt, asynUser* pasynUser,
    const char* data, size_t numchars, size_t* nbytesTransfered)
{
    epicsTimeStamp currTime;
    double elapsed;

    limitRatePvt* pLimitRate = (limitRatePvt*) ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::write\n");

    epicsTimeGetCurrent(&currTime);
    elapsed = epicsTimeDiffInSeconds(&currTime, &pLimitRate->lastTime);

    // We have to wait for the required interval
    if (elapsed < pLimitRate->minInterval) {
        epicsThreadSleep(pLimitRate->minInterval - elapsed);
    }

    // Update last time when a message was sent
    epicsTimeGetCurrent(&pLimitRate->lastTime);

    return pLimitRate->pasynOctet->write(pLimitRate->asynOctetPvt,
                    pasynUser, data, numchars, nbytesTransfered);
}

static asynStatus readIt(void* ppvt, asynUser* pasynUser,
    char* data, size_t maxchars, size_t* nbytesTransfered, int* eomReason)
{
    limitRatePvt* pLimitRate = (limitRatePvt*) ppvt;

    asynPrint(pasynUser, ASYN_TRACEIO_FILTER,
        "entered asynInterposeLimitRate::read\n");

    return pLimitRate->pasynOctet->read(pLimitRate->asynOctetPvt,
                    pasynUser, data, maxchars, nbytesTransfered, eomReason);;
}

ASYN_API int asynInterposeLimitRateConfig(const char* port, double interval)
{
    limitRatePvt* pLimitRate;
    char* portName;
    asynStatus status;
    asynInterface* poctetasynInterface;

    portName = callocMustSucceed(strlen(port) + 1, sizeof(char),
        "asynInterposeLimitRateConfig");
    strcpy(portName, port);
    pLimitRate = callocMustSucceed(1, sizeof(limitRatePvt), "asynInterposeLimitRateConfig");
    pLimitRate->portName = portName;
    epicsTimeGetCurrent(&pLimitRate->lastTime);
    pLimitRate->minInterval = interval;
    pLimitRate->octet.interfaceType = asynOctetType;
    pLimitRate->octet.pinterface = &octet;
    pLimitRate->octet.drvPvt = pLimitRate;

    status = pasynManager->interposeInterface(portName, 0,
        &pLimitRate->octet, &poctetasynInterface);
    if ((status != asynSuccess) || !poctetasynInterface) {
        printf("%s asynInterposeLimitRate failed.\n", portName);
        free(pLimitRate);
        free(portName);
        return(0);
    }

    pLimitRate->pasynOctet = (asynOctet*) poctetasynInterface->pinterface;
    pLimitRate->asynOctetPvt = poctetasynInterface->drvPvt;
    
    return(0);
}

/* register asynInterposeLimitRateConfig*/
static const iocshArg asynInterposeLimitRateConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg asynInterposeLimitRateConfigArg1 =
    { "minimumInterval", iocshArgDouble };
static const iocshArg* asynInterposeLimitRateConfigArgs[] =
    {&asynInterposeLimitRateConfigArg0, &asynInterposeLimitRateConfigArg1};
static const iocshFuncDef asynInterposeLimitRateConfigFuncDef =
    {"asynInterposeLimitRateConfig", 2, asynInterposeLimitRateConfigArgs};
static void asynInterposeLimitRateConfigCallFunc(const iocshArgBuf* args)
{
    asynInterposeLimitRateConfig(args[0].sval, args[1].dval);
}

static void asynInterposeLimitRateRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynInterposeLimitRateConfigFuncDef, asynInterposeLimitRateConfigCallFunc);
    }
}
epicsExportRegistrar(asynInterposeLimitRateRegister);
