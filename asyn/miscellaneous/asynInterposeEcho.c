/*asynInterposeEcho.c */
/***********************************************************************/

/* Interpose for devices where each written char needs to read back
 * before sending the next char.
 * 
 * Author: Dirk Zimoch
 */

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include <epicsExport.h>

#if defined(__GNUC__) && __GNUC__ > 2
#define Z "z"
#elif defined(_WIN32)
#define Z "I"
#else
#define Z 
#endif

/* compatibility with older EPICS 3.14 versions */
#ifndef epicsStrSnPrintEscaped
#define epicsStrnEscapedFromRaw epicsStrSnPrintEscaped
#endif

typedef struct interposePvt {
    asynInterface octet;
    asynOctet     *pasynOctetDrv;
    void          *drvPvt;
}interposePvt;

/* asynOctet methods */
static asynStatus writeIt(void *ppvt, asynUser *pasynUser,
    const char *data, size_t numchars, size_t *nbytesTransfered)
{
    interposePvt *pvt = (interposePvt *)ppvt;
    size_t n;
    size_t transfered = 0;
    char echo[4];
    asynStatus status = asynSuccess;
    int eomReason;
    
    while (transfered < numchars) {
        /* write one char at a time */
        status = pvt->pasynOctetDrv->write(pvt->drvPvt,
            pasynUser, data, 1, &n);
        if (status != asynSuccess) break;
        if (n != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "wrote %" Z "d chars instead of 1", n);
            status = asynError;
            break;
        }
        /* read back echo */
        status = pvt->pasynOctetDrv->read(pvt->drvPvt,
            pasynUser, echo, 1, &n, &eomReason);
        if (status == asynTimeout)
        {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "timeout reading back char number %" Z "d", transfered);
        }
        if (status != asynSuccess) break;
        if (n != 1 || echo[0] != data[0]) {
            char outstr[16];
            char echostr[16];
            epicsStrnEscapedFromRaw(outstr, sizeof(outstr), data, 1);
            epicsStrnEscapedFromRaw(echostr, sizeof(echostr), echo, n);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "got back '%s' instead of '%s'", echostr, outstr);
            status = asynError;
            break;
        }
        transfered++;
        data++;
    }
    *nbytesTransfered = transfered;
    return status;
}

static asynStatus readIt(void *ppvt, asynUser *pasynUser,
    char *data, size_t maxchars, size_t *nbytesTransfered, int *eomReason)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->read(pvt->drvPvt,
        pasynUser, data, maxchars, nbytesTransfered, eomReason);
}

static asynStatus flushIt(void *ppvt, asynUser *pasynUser)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->flush(pvt->drvPvt, pasynUser);
}

static asynStatus registerInterruptUser(void *ppvt, asynUser *pasynUser,
    interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->registerInterruptUser(
        pvt->drvPvt,
        pasynUser, callback, userPvt, registrarPvt);
}

static asynStatus cancelInterruptUser(void *drvPvt, asynUser *pasynUser,
    void *registrarPvt)
{
    interposePvt *pvt = (interposePvt *)drvPvt;

    return pvt->pasynOctetDrv->cancelInterruptUser(
        pvt->drvPvt, pasynUser, registrarPvt);
}

static asynStatus setInputEos(void *ppvt, asynUser *pasynUser,
    const char *eos, int eoslen)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->setInputEos(pvt->drvPvt,
        pasynUser, eos, eoslen);
}

static asynStatus getInputEos(void *ppvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->getInputEos(pvt->drvPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynStatus setOutputEos(void *ppvt, asynUser *pasynUser,
    const char *eos, int eoslen)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->setOutputEos(pvt->drvPvt,
        pasynUser, eos, eoslen);
}

static asynStatus getOutputEos(void *ppvt, asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    interposePvt *pvt = (interposePvt *)ppvt;

    return pvt->pasynOctetDrv->getOutputEos(pvt->drvPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynOctet octet = {
    writeIt, readIt, flushIt,
    registerInterruptUser, cancelInterruptUser,
    setInputEos, getInputEos, setOutputEos, getOutputEos
};

/* asynOctet methods */
epicsShareFunc int 
asynInterposeEcho(const char *portName, int addr)
{
    interposePvt *pvt;
    asynStatus status;
    asynInterface *poctetasynInterface;

    pvt = callocMustSucceed(1, sizeof(interposePvt), "asynInterposeEcho");
    pvt->octet.interfaceType = asynOctetType;
    pvt->octet.pinterface = &octet;
    pvt->octet.drvPvt = pvt;
    status = pasynManager->interposeInterface(portName, addr,
        &pvt->octet, &poctetasynInterface);
    if ((status!=asynSuccess) || !poctetasynInterface) {
	printf("%s interposeInterface failed.\n", portName);
        free(pvt);
        return -1;
    }
    pvt->pasynOctetDrv = (asynOctet *)poctetasynInterface->pinterface;
    pvt->drvPvt = poctetasynInterface->drvPvt;
    return 0;
}

/* register asynInterposeEcho*/
static const iocshArg iocshArg0 = {"portName", iocshArgString};
static const iocshArg iocshArg1 = {"addr", iocshArgInt};
static const iocshArg *iocshArgs[] =
    {&iocshArg0, & iocshArg1};
    
static const iocshFuncDef asynInterposeEchoFuncDef =
    {"asynInterposeEcho", 2, iocshArgs};

static void asynInterposeEchoCallFunc(const iocshArgBuf *args)
{
    asynInterposeEcho(args[0].sval, args[1].ival);
}

static void asynInterposeEchoRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&asynInterposeEchoFuncDef, asynInterposeEchoCallFunc);
    }
}
epicsExportRegistrar(asynInterposeEchoRegister);
