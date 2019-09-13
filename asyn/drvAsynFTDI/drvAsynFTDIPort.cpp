/***************************************************
* Asyn device support for FTDI comms library        *
****************************************************/

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <osiUnistd.h>
#include <osiSock.h>
#include <cantProceed.h>
#include <errlog.h>
#include <iocsh.h>
#include <epicsAssert.h>
#include <epicsExit.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <osiUnistd.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynInterposeCom.h"
#include "asynInterposeEos.h"
#include "drvAsynFTDIPort.h"
#include "ftdiDriver.h"

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each IP socket.
 */
typedef struct {
    asynUser          *pasynUser;        /* Not currently used */
    int               FTDIvendor;
    int               FTDIproduct;
    int               FTDIbaudrate;
    int               FTDIlatency;
    char              *portName;
    FTDIDriver         *driver;
    unsigned long      nRead;
    unsigned long      nWritten;
    int                haveAddress;
    osiSockAddr        farAddr;
    asynInterface      common;
    asynInterface      option;
    asynInterface      octet;
} ftdiController_t;

/*
 * asynOption methods
 */
static asynStatus
getOption(void *drvPvt, asynUser *pasynUser, const char *key, char *val, int valSize)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;
    int l;

    if (epicsStrCaseCmp(key, "baud") == 0)
        l = epicsSnprintf(val, valSize, "%d", ftdi->driver->getBaudRate());

    else if (epicsStrCaseCmp(key, "bits") == 0) {
        const char *v;
        switch (ftdi->driver->getBits()) {
            case BITS_7: v = "7"; break;
            case BITS_8: v = "8"; break;
            default:     v = "?";
        }

        l = epicsSnprintf(val, valSize, "%s", v);
    }

    else if (epicsStrCaseCmp(key, "parity") == 0) {
        const char *v;
        switch(ftdi->driver->getParity()) {
            case NONE:  v = "none";  break;
            case ODD:   v = "odd";   break;
            case EVEN:  v = "even";  break;
            case MARK:  v = "mark";  break;
            case SPACE: v = "space"; break;
            default:    v = "?";
        }
        l = epicsSnprintf(val, valSize, "%s", v);
    }

    else if (epicsStrCaseCmp(key, "stop") == 0) {
        const char *v;
        switch(ftdi->driver->getStopBits()) {
            case STOP_BIT_1:  v = "1";   break;
            case STOP_BIT_15: v = "1.5"; break;
            case STOP_BIT_2:  v = "2";   break;
            default:          v = "?";
        }
        l = epicsSnprintf(val, valSize, "%s", v);
    }

    else if (epicsStrCaseCmp(key, "break") == 0) {
        const char *v;
        switch(ftdi->driver->getBreak()) {
            case BREAK_OFF:  v = "off"; break;
            case BREAK_ON:   v = "on";  break;
            default:         v = "?";
        }
        l = epicsSnprintf(val, valSize, "%s", v);
    }

    else if (epicsStrCaseCmp(key, "flow")) {
        int flowctrl = ftdi->driver->getFlowControl();
        if (flowctrl == SIO_DISABLE_FLOW_CTRL)
            l = epicsSnprintf(val, valSize, "%s", "none");
        else {
            char v[256] = {};
            int n = 0;
            if (flowctrl & SIO_RTS_CTS_HS)  { strcat(v, "rts_cts"); ++n; }
            if (n)                          { strcat(v, "|");            }
            if (flowctrl & SIO_DTR_DSR_HS)  { strcat(v, "dtr_dsr"); ++n; }
            if (n)                          { strcat(v, "|");            }
            if (flowctrl & SIO_XON_XOFF_HS) { strcat(v, "xon_xoff");     }
            l = epicsSnprintf(val, valSize, "%s", v);
        }
    }

    else {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Unsupported key \"%s\"", key);
        return asynError;
    }

    if (l >= valSize) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Value buffer for key '%s' is too small.", key);
        return asynError;
    }
    return asynSuccess;
}

static asynStatus
setOption(void *drvPvt, asynUser *pasynUser, const char *key, const char *val)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;
    assert(ftdi);

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s setOption key %s val %s\n", ftdi->portName, key, val);

    if (epicsStrCaseCmp(key, "baud") == 0) {
        int baud;
        if (sscanf(val, "%d", &baud) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, "Bad number");
            return asynError;
        }
        if (ftdi->driver->setBaudrate(baud)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "Unsupported data rate (%d baud)", baud);
            return asynError;
        }
    } else if (epicsStrCaseCmp(key, "parity") == 0) {
        enum ftdi_parity_type parity;
        if      (epicsStrCaseCmp(val, "none")  == 0) parity = NONE;
        else if (epicsStrCaseCmp(val, "even")  == 0) parity = EVEN;
        else if (epicsStrCaseCmp(val, "odd")   == 0) parity = ODD;
        else if (epicsStrCaseCmp(val, "mark")  == 0) parity = MARK;
        else if (epicsStrCaseCmp(val, "space") == 0) parity = SPACE;
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "Invalid parity \"%s\".", val);
            return asynError;
        }
        if (ftdi->driver->setParity(parity))
            return asynError;
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        enum ftdi_stopbits_type sbits;
        if      (epicsStrCaseCmp(val, "1")   == 0) sbits = STOP_BIT_1;
        else if (epicsStrCaseCmp(val, "1.5") == 0) sbits = STOP_BIT_15;
        else if (epicsStrCaseCmp(val, "2")   == 0) sbits = STOP_BIT_2;
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "Invalid number of stop bits \"%s\".", val);
            return asynError;
        }
        if (ftdi->driver->setStopBits(sbits))
            return asynError;
    }
    else if (epicsStrCaseCmp(key, "break") == 0) {
        enum ftdi_break_type brk;
        if      (epicsStrCaseCmp(val, "on")  == 0) brk = BREAK_ON;
        else if (epicsStrCaseCmp(val, "off") == 0) brk = BREAK_OFF;
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                "Invalid break \"%s\".", val);
            return asynError;
        }
        if (ftdi->driver->setBreak(brk))
            return asynError;
    }
    else if (epicsStrCaseCmp(key, "flow") == 0) {
        int flowctrl = SIO_DISABLE_FLOW_CTRL;
        if (epicsStrCaseCmp(val, "none") != 0) {
            char *str = epicsStrDup(val);
            char *token;
            char *rest = str;

            while ((token = strtok_r(rest, "|", &rest))) {
                if      (epicsStrCaseCmp(token, "rts_cts")  == 0) flowctrl |= SIO_RTS_CTS_HS;
                else if (epicsStrCaseCmp(token, "dtr_dsr")  == 0) flowctrl |= SIO_DTR_DSR_HS;
                else if (epicsStrCaseCmp(token, "xon_xoff") == 0) flowctrl |= SIO_XON_XOFF_HS;
                else {
                    epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                        "Invalid flow control \"%s\".", val);
                    free(str);
                    return asynError;
                }
            }
            free(str);
        }
        if (ftdi->driver->setFlowControl(flowctrl))
            return asynError;
    }
    else if (epicsStrCaseCmp(key, "") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "Unsupported key \"%s\"", key);
        return asynError;
    }

    return asynSuccess;
}
static const struct asynOption asynOptionMethods = { setOption, getOption };

/*
 * Close a connection
 */
static void
closeConnection(asynUser *pasynUser,ftdiController_t *ftdi,const char *why)
{
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Close %d:%d connection (driver %p): %s\n", ftdi->FTDIvendor, ftdi->FTDIproduct, ftdi->driver, why);
    if (ftdi->driver){
        ftdi->driver->disconnectFTDI();
        delete(ftdi->driver);
        ftdi->driver = 0;
    }
}

/*Beginning of asynCommon methods*/
/*
 * Report link parameters
 */
static void
asynCommonReport(void *drvPvt, FILE *fp, int details)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;

    assert(ftdi);
    if (details >= 1) {
        fprintf(fp, "    Port %d:%d: %sonnected\n",
                                                ftdi->FTDIvendor, ftdi->FTDIproduct,
                                                ftdi->driver ? "C" : "Disc");
    }
    if (details >= 2) {
        fprintf(fp, "    Characters written: %lu\n", ftdi->nWritten);
        fprintf(fp, "       Characters read: %lu\n", ftdi->nRead);
    }
}

/*
 * Clean up a connection on exit
 */
static void
cleanup (void *arg)
{
    asynStatus status;
    ftdiController_t *ftdi = (ftdiController_t *)arg;

    if (!ftdi) return;
    status=pasynManager->lockPort(ftdi->pasynUser);
    if(status!=asynSuccess)
        asynPrint(ftdi->pasynUser, ASYN_TRACE_ERROR, "%s: cleanup locking error\n", ftdi->portName);

    if (ftdi->driver){
        asynPrint(ftdi->pasynUser, ASYN_TRACE_FLOW, "%s: shutdown socket\n", ftdi->portName);
        ftdi->driver->disconnectFTDI();
        delete(ftdi->driver);
    }

    if(status==asynSuccess)
        pasynManager->unlockPort(ftdi->pasynUser);
}

/*
 * Create a link
*/
static asynStatus
connectIt(void *drvPvt, asynUser *pasynUser)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;

    /*
     * Sanity check
     */
    assert(ftdi);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "Open connection to %d:%d  reason:%d \n", ftdi->FTDIvendor, ftdi->FTDIproduct,
                                                     pasynUser->reason);

    if (ftdi->driver){
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%d:%d: Link already open!", ftdi->FTDIvendor, ftdi->FTDIproduct);
        return asynError;
    }

    // Create the driver
    ftdi->driver = new FTDIDriver();

    // Set the vendor & product IDs
    ftdi->driver->setVPID(ftdi->FTDIvendor, ftdi->FTDIproduct);

    // Set the baud rate
    ftdi->driver->setBaudrate(ftdi->FTDIbaudrate);

    // Set the latency
    ftdi->driver->setLatency(ftdi->FTDIlatency);

    // Connect to the remote host
    if (ftdi->driver->connectFTDI() != FTDIDriverSuccess){
      epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                    "Can't connect to %d:%d",
                    ftdi->FTDIvendor, ftdi->FTDIproduct);
      ftdi->haveAddress = 0;
      delete(ftdi->driver);
      ftdi->driver = 0;
      return asynError;
    }

    asynPrint(pasynUser, ASYN_TRACE_FLOW, "Opened connection to %d:%d\n", ftdi->FTDIvendor, ftdi->FTDIproduct);
    return asynSuccess;
}

static asynStatus
asynCommonConnect(void *drvPvt, asynUser *pasynUser)
{
    asynStatus status = asynSuccess;

    status = connectIt(drvPvt, pasynUser);

    if (status == asynSuccess)
        pasynManager->exceptionConnect(pasynUser);
    return status;
}

static asynStatus
asynCommonDisconnect(void *drvPvt, asynUser *pasynUser)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;

    assert(ftdi);
    closeConnection(pasynUser,ftdi,"Disconnect request");
    return asynSuccess;
}

/*Beginning of asynOctet methods*/
/*
 * Write to the USB FTDI port
 */
static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars,size_t *nbytesTransfered)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;
    size_t thisWrite;
    asynStatus status = asynSuccess;

    assert(ftdi);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%d:%d write.\n", ftdi->FTDIvendor, ftdi->FTDIproduct);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                "%d:%d write %lu\n", ftdi->FTDIvendor, ftdi->FTDIproduct, numchars);
    *nbytesTransfered = 0;

    // Here we will simply issue the write to the driver
    if (!ftdi->driver){
        if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess){
          return status;
        }
    }
    if (numchars == 0){
      return asynSuccess;
    }
    if (ftdi->driver->write((unsigned char *)data, numchars, &thisWrite, (int)(pasynUser->timeout*1000.0)) != FTDIDriverSuccess){
      epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                               "%d:%d write error", ftdi->FTDIvendor, ftdi->FTDIproduct);
      closeConnection(pasynUser,ftdi,"Write error");
      status = asynError;
    }
    *nbytesTransfered = thisWrite;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "wrote %lu to %d:%d, return %s.\n", (unsigned long)*nbytesTransfered,
                                                ftdi->FTDIvendor, ftdi->FTDIproduct,
                                               pasynManager->strStatus(status));
    return status;
}

/*
 * Read from the TCP port
 */
static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars,size_t *nbytesTransfered,int *gotEom)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;
    size_t thisRead;
    int reason = 0;
    asynStatus status = asynSuccess;
    FTDIDriverStatus driverStatus = FTDIDriverSuccess;

    assert(ftdi);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%d:%d read.\n", ftdi->FTDIvendor, ftdi->FTDIproduct);
    if (!ftdi->driver){
      if ((status = connectIt(drvPvt, pasynUser)) != asynSuccess){
        return status;
      }
    }
    if (maxchars <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                  "%d:%d maxchars %d. Why <=0?\n",ftdi->FTDIvendor, ftdi->FTDIproduct,(int)maxchars);
        return asynError;
    }

    if (gotEom) *gotEom = 0;
    driverStatus = ftdi->driver->read((unsigned char *)data, maxchars, &thisRead, (int)(pasynUser->timeout*1000.0));
    if (driverStatus != FTDIDriverSuccess){
      if (driverStatus == FTDIDriverError){
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                               "%d:%d read error", ftdi->FTDIvendor, ftdi->FTDIproduct);
        closeConnection(pasynUser,ftdi,"Read error");
        status = asynError;
      } else if (driverStatus == FTDIDriverTimeout){
        status = asynTimeout;
      }
    }

    if (thisRead < 0){
        thisRead = 0;
    }
    *nbytesTransfered = thisRead;

    /* If there is room add a null byte */
    if (thisRead < maxchars)
        data[thisRead] = 0;
    else
        reason |= ASYN_EOM_CNT;

    if (gotEom) *gotEom = reason;

    if (thisRead == 0 && pasynUser->timeout == 0){
      status = asynTimeout;
    }

    return status;
}

/*
 * Flush pending input
 */
static asynStatus
flushIt(void *drvPvt,asynUser *pasynUser)
{
    ftdiController_t *ftdi = (ftdiController_t *)drvPvt;

    assert(ftdi);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%d:%d flush\n", ftdi->FTDIvendor, ftdi->FTDIproduct);
    if (ftdi->driver){
      ftdi->driver->flush();
    }
    return asynSuccess;
}

/*
 * Clean up a ftdiController
 */
static void
ftdiCleanup(ftdiController_t *ftdi)
{
    if (ftdi) {
        if (ftdi->driver)
        free(ftdi->portName);
/*        free(ftdi->FTDIDeviceName); */
        free(ftdi);
    }
}

/*
 * asynCommon methods
 */
static const struct asynCommon drvAsynFTDIPortAsynCommon = {
    asynCommonReport,
    asynCommonConnect,
    asynCommonDisconnect
};

/*
 * Configure and register a USB port from a hostInfo string
 */
epicsShareFunc int
drvAsynFTDIPortConfigure(const char *portName,
                         int vendor,
                         int product,
                         int baudrate,
                         int latency,
                         unsigned int priority,
                         int noAutoConnect,
                         int noProcessEos)
{
    ftdiController_t *ftdi;
    asynInterface *pasynInterface;
    asynStatus status;
    int nbytes;
    asynOctet *pasynOctet;

    /* Latency must be between 1 and 255 */
    if (latency < 1)
        latency = 1;
    else if (latency > 255)
        latency= 255;

    /*
     * Check arguments
     *
    if (vendor == 0) {
        printf("asynFTDI vendor ID missing.\n");
        return -1;
    }
    if (product == NULL) {
        printf("asynFTDI product ID missing.\n");
        return -1;
    }
    if (baudrate == NULL) {
        printf("asynFTDI username missing.\n");
        return -1;
    } */


    /*
     * Create a driver
     */
    nbytes = sizeof(*ftdi) + sizeof(asynOctet);
    ftdi = (ftdiController_t *)callocMustSucceed(1, nbytes,
          "drvAsynFTDIPortConfigure()");
    pasynOctet = (asynOctet *)(ftdi+1);
    ftdi->driver = NULL;
    ftdi->FTDIvendor = vendor;
    ftdi->FTDIproduct = product;
    ftdi->FTDIbaudrate = baudrate;
    ftdi->FTDIlatency = latency;
    ftdi->portName = epicsStrDup(portName);

    /*
     *  Link with higher level routines
     */
    pasynInterface = (asynInterface *)callocMustSucceed(2, sizeof *pasynInterface, "drvAsynFTDIPortConfigure");
    ftdi->common.interfaceType = asynCommonType;
    ftdi->common.pinterface  = (void *)&drvAsynFTDIPortAsynCommon;
    ftdi->common.drvPvt = ftdi;
    ftdi->option.interfaceType = asynOptionType;
    ftdi->option.pinterface = (void*)&asynOptionMethods;
    ftdi->option.drvPvt = ftdi;
    if (pasynManager->registerPort(ftdi->portName,
                                   ASYN_CANBLOCK,
                                   !noAutoConnect,
                                   priority,
                                   500000) != asynSuccess) {
        printf("drvAsynFTDIPortConfigure: Can't register myself.\n");
        ftdiCleanup(ftdi);
        return -1;
    }
    status = pasynManager->registerInterface(ftdi->portName,&ftdi->common);
    if(status != asynSuccess) {
        printf("drvAsynFTDIPortConfigure: Can't register common.\n");
        ftdiCleanup(ftdi);
        return -1;
    }
    status = pasynManager->registerInterface(ftdi->portName,&ftdi->option);
    if(status != asynSuccess) {
        printf("drvAsynFTDIPortConfigure: Can't register option.\n");
        ftdiCleanup(ftdi);
        return -1;
    }
    pasynOctet->read = readIt;
    pasynOctet->write = writeIt;
    pasynOctet->flush = flushIt;
    ftdi->octet.interfaceType = asynOctetType;
    ftdi->octet.pinterface  = pasynOctet;
    ftdi->octet.drvPvt = ftdi;
    status = pasynOctetBase->initialize(ftdi->portName,&ftdi->octet, 0, 0, 1);
    if(status != asynSuccess) {
        printf("drvAsynFTDIPortConfigure: pasynOctetBase->initialize failed.\n");
        ftdiCleanup(ftdi);
        return -1;
    }
    if (!noProcessEos)
        asynInterposeEosConfig(ftdi->portName, -1, 1, 1);
    ftdi->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(ftdi->pasynUser,ftdi->portName,-1);
    if(status != asynSuccess) {
        printf("connectDevice failed %s\n",ftdi->pasynUser->errorMessage);
        ftdiCleanup(ftdi);
        return -1;
    }

    /*
     * Register for socket cleanup
     */
    epicsAtExit(cleanup, ftdi);
    return 0;
}

/*
 * IOC shell command registration
 */
static const iocshArg drvAsynFTDIPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynFTDIPortConfigureArg1 = { "vendor ID",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg2 = { "product ID",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg3 = { "baudrate",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg4 = { "latency",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg5 = { "priority",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg6 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynFTDIPortConfigureArg7 = { "noProcessEos",iocshArgInt};
static const iocshArg *drvAsynFTDIPortConfigureArgs[] = {
    &drvAsynFTDIPortConfigureArg0, &drvAsynFTDIPortConfigureArg1,
    &drvAsynFTDIPortConfigureArg2, &drvAsynFTDIPortConfigureArg3,
    &drvAsynFTDIPortConfigureArg4, &drvAsynFTDIPortConfigureArg5,
    &drvAsynFTDIPortConfigureArg6};
static const iocshFuncDef drvAsynFTDIPortConfigureFuncDef =
                      {"drvAsynFTDIPortConfigure",7,drvAsynFTDIPortConfigureArgs};
static void drvAsynFTDIPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynFTDIPortConfigure(args[0].sval, args[1].ival, args[2].ival, args[3].ival, args[4].ival, args[5].ival, args[6].ival, args[7].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynFTDIPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynFTDIPortConfigureFuncDef,drvAsynFTDIPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynFTDIPortRegisterCommands);

