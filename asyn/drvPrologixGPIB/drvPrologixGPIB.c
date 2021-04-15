/*
 * Prologic Ethernet/GPIB driver
 */
#include <string.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <errlog.h>
#include <iocsh.h>
#include <asynGpibDriver.h>
#include <asynCommonSyncIO.h>
#include <asynOctetSyncIO.h>
#include <asynOctet.h>
#include <drvAsynIPPort.h>

/*
 * Driver private storage
 */
typedef struct dPvt {
    char        *portName;
    void        *asynGpibPvt;

    /*
     * Link to lower-level driver
     */
    char        *hostTCP;
    char        *portNameTCP;
    asynUser    *pasynUserTCPcommon;
    asynUser    *pasynUserTCPoctet;
    int          isConnected;
    int          autoConnect;

    /*
     * Input/Output staging buffer
     */
    char        *buf;
    size_t       bufCapacity;
    size_t       bufCount;
    size_t       bufIndex;

    /*
     * Miscellaneous
     */
    char         versionString[200];
    int          lastAddress;
    int          lastPrimaryAddress;
    int          lastSecondaryAddress;
    int          eos;
} dPvt;

#define EOT_MARKER  0xEF

/*
 * Set the address of the device to which we wish to communicate.
 * Uses buf.
 */
static asynStatus
setAddress(dPvt *pdpvt, asynUser *pasynUser)
{
    int address, primary, secondary;
    size_t n, nt;
    asynStatus status;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;
    if (address < 100) {
        primary = address;
        secondary = -1;
    } else {
        primary = address / 100;
        secondary = address % 100;
        if ((secondary < 0) || (secondary >= 31)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                              "Invalid GPIB secondary address %d", secondary);
            return asynError;
        }
    }
    if ((primary < 0) || (primary >= 31)) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                  "Invalid GPIB primary address %d", primary);
        return asynError;
    }
    if ((primary == pdpvt->lastPrimaryAddress)
     && (secondary == pdpvt->lastSecondaryAddress))
        return asynSuccess;
    if (secondary < 0)
        n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++addr %d\n", primary);
    else
        n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++addr %d %d\n", primary, secondary + 96);
    status = pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf,
                                                                n, 1.0, &nt);
    if (status != asynSuccess) {
        pdpvt->lastPrimaryAddress = -1;
        pdpvt->lastSecondaryAddress = -1;
        return status;
    }
    pdpvt->lastPrimaryAddress = primary;
    pdpvt->lastSecondaryAddress = secondary;
    pdpvt->lastAddress = address;
    return asynSuccess;
}

/*
 * Get more space for I/O buffer
 */
static asynStatus
resizeBuffer(dPvt *pdpvt, asynUser *pasynUser, size_t size)
{
    char *np;
    size_t newCapacity = size + 4096;

    if ((size <= pdpvt->bufCapacity)
     || (newCapacity <= pdpvt->bufCapacity)
     || ((np = realloc(pdpvt->buf, newCapacity)) == NULL)) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                      "Can't allocate memory for output buffer");
        return asynError;
    }
    pdpvt->buf = np;
    pdpvt->bufCapacity = size + newCapacity;
    return asynSuccess;
}

/*
 * Place a character into output buffer
 * Escape special characters
 */
static asynStatus
stashChar(dPvt *pdpvt, asynUser *pasynUser, int c)
{
    if (pdpvt->bufCount >= (pdpvt->bufCapacity - 3)) {
        if (resizeBuffer(pdpvt, pasynUser, pdpvt->bufCapacity) != asynSuccess)
            return asynError;
    }
    switch (c) {
    case '\r':
    case '\n':
    case '\033':
    case '+':
        pdpvt->buf[pdpvt->bufCount++] = '\033';
        break;
    }
    pdpvt->buf[pdpvt->bufCount++] = c;
    return asynSuccess;
}

static void
prologixReport(void *drvPvt, FILE *fd, int details)
{
    dPvt *pdpvt = (dPvt *)drvPvt;

    fprintf(fd, "   Version: %s\n", pdpvt->versionString);
}

static asynStatus
prologixConnect(void *drvPvt, asynUser *pasynUser)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    size_t n, nt;
    int address;
    asynStatus status;
    char *cp;
    int eom;

    pdpvt->lastPrimaryAddress = -1;
    pdpvt->lastSecondaryAddress = -1;
    pdpvt->bufCount = 0;
    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;
    if (address < 0) {
        status = pasynCommonSyncIO->connectDevice(pdpvt->pasynUserTCPcommon);
        if (status != asynSuccess)
            return status;
        n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity,
                    "++savecfg 0\n"    /* Don't save changes in EEPROM */
                    "++mode 1\n"       /* We are controller            */
                    "++ifc\n"          /* Clear the bus                */
                    "++eos 3\n"        /* Handle EOS ourselves         */
                    "++eoi 1\n"        /* Generate EOI on output       */
                    "++eot_char %d\n"  /* Mark EOT on input            */
                    "++eot_enable 1\n"
                    "++ver\n"          /* Request version information  */
                    , EOT_MARKER
                    );
        status = pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf,
                                                                n, 1.0, &nt);
        if (status != asynSuccess)
            return status;
        cp = pdpvt->versionString;
        n = sizeof pdpvt->versionString;
        for (;;) {
            status = pasynOctetSyncIO->read(pdpvt->pasynUserTCPoctet, cp, n, 0.5,
                                                                     &nt, &eom);
            if (status != asynSuccess)
                return status;
            n -= nt;
            if (n == 0) {
                epicsSnprintf(pasynUser->errorMessage,
                              pasynUser->errorMessageSize,
                              "Version string too long");
                return asynError;
            }
            cp += nt;
            if ((n <= (sizeof pdpvt->versionString - 2))
             && (cp[-2] == '\r')
             && (cp[-1] == '\n')) {
                cp[-2] = '\0';
                break;
            }
        }
    }
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
prologixDisconnect(void *drvPvt, asynUser *pasynUser)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    int address;
    asynStatus status;

    if ((status = pasynManager->getAddr(pasynUser, &address)) != asynSuccess)
        return status;
    if (address < 0) {
        status = pasynCommonSyncIO->disconnectDevice(pdpvt->pasynUserTCPcommon);
        if (status != asynSuccess)
            return status;
    }
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}

/*
 * Read from a GPIB device.
 */
static asynStatus
prologixRead(void *drvPvt, asynUser *pasynUser,
             char *data, int maxchars, int *nbytesTransfered, int *eomReason)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    size_t n;
    int eom;

    /*
     * Get entire reply on first invocation of this method following
     * a write, flush or connect.
     */
    if (pdpvt->bufCount == 0) {
        char *cp = pdpvt->buf;
        size_t nt;
        double timeout = pasynUser->timeout;
        asynStatus status;
        int atEOT = 0;
        int terminator = (pdpvt->eos >= 0) ? pdpvt->eos : EOT_MARKER;

        /*
         * Address the device
         */
        if ((status = setAddress(pdpvt, pasynUser)) != asynSuccess)
            return status;

        /*
         * Read
         */
        if (pdpvt->eos >= 0)
            n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++read %d\n", pdpvt->eos);
        else
            n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++read eoi\n");
        status = pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf,
                                                                    n, 1.0, &nt);
        if (status != asynSuccess)
            return status;

        /*
         * Read until we see the appropriate terminator
         */
        *nbytesTransfered = 0;
        for (;;)  {
            /*
             * Ensure that there's space for the read
             */
            for (;;) {
                n = pdpvt->bufCapacity - pdpvt->bufCount;
                if (n)
                    break;
                if (resizeBuffer(pdpvt, pasynUser, pdpvt->bufCapacity + 16384) != asynSuccess) {
                    pdpvt->bufCount = 0;
                    return asynError;
                }
            }

            /*
             * Read a chunk
             */
            status = pasynOctetSyncIO->read(pdpvt->pasynUserTCPoctet, cp, n,
                                                            timeout, &nt, &eom);
            if (atEOT && (status == asynTimeout))
                break;
            if (status != asynSuccess)
                return status;
            pdpvt->bufCount += nt;
            cp += nt;
            n -= nt;

            /*
             * See if we've received the terminator
             */
            if ((nt >= 1) && ((cp[-1] & 0xFF) == terminator)) {
                /*
                 * If there's no EOS character we might be in binary mode and
                 * the EOT marker at the end of the buffer might actually
                 * be part of the data stream.
                 * The only way to determine if we're really at the end of
                 * the message is to try another read and see if times out.
                 *
                 * Yuck.
                 */
                if (pdpvt->eos >= 0)
                    break;
                timeout = 0.005;
                atEOT = 1;
            }
            else {
                timeout = pasynUser->timeout;
                atEOT = 0;
            }
        }
        if (pdpvt->eos < 0)
            pdpvt->bufCount--;   /* Drop EOT marker */
        pdpvt->bufIndex = 0;
    }
    eom = 0;
    n = pdpvt->bufCount - pdpvt->bufIndex;
    if (maxchars >= n) {
        if (pdpvt->eos >= 0)
            eom |= ASYN_EOM_EOS;
        else
            eom |= ASYN_EOM_END;
    }
    if (n >= maxchars) {
        n = maxchars;
        eom |= ASYN_EOM_CNT;
    }
    memcpy(data, pdpvt->buf + pdpvt->bufIndex, n);
     pdpvt->bufIndex += n;
    if (eomReason) *eomReason = eom;
    *nbytesTransfered = n;
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, n,
                                "%s %d prologixRead %d EOM:%#x\n",
                                pdpvt->portName, pdpvt->lastAddress, (int)n, eom);
    return asynSuccess;
}

static asynStatus
prologixWrite(void *drvPvt, asynUser *pasynUser,
              const char *data, int numchars, int *nbytesTransfered)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    size_t n, nt;
    asynStatus status;

    /*
     * Check for output buffer space.
     * Escape stuffing may make this bigger, but at least this gets us close.
     */
    if (numchars >= pdpvt->bufCapacity) {
        if (resizeBuffer(pdpvt, pasynUser, numchars) != asynSuccess)
            return asynError;
    }

    /*
     * Address the device
     */
    if ((status = setAddress(pdpvt, pasynUser)) != asynSuccess)
        return status;

    /*
     * Create command string
     */
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                 "%s %d prologixWrite\n", pdpvt->portName, pdpvt->lastAddress);
    *nbytesTransfered = 0;
    n = numchars;
    pdpvt->bufCount = 0;
    while (n) {
        if ((status = stashChar(pdpvt, pasynUser, *data++)) != asynSuccess) {
            pdpvt->bufCount = 0;
            return status;
        }
        n--;
    }
    if (pdpvt->eos >= 0) {
        if ((status = stashChar(pdpvt, pasynUser, pdpvt->eos)) != asynSuccess) {
            pdpvt->bufCount = 0;
            return status;
        }
    }
    pdpvt->buf[pdpvt->bufCount++] = '\n';

    /*
     * Send the command
     */
    status = pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf,
                                    pdpvt->bufCount, pasynUser->timeout, &nt);
    if (status == asynSuccess)
        *nbytesTransfered = numchars;
    pdpvt->bufCount = 0;
    return status;
}

static asynStatus
prologixFlush(void *drvPvt, asynUser *pasynUser)
{
    dPvt *pdpvt = (dPvt *)drvPvt;

    pdpvt->bufCount = 0;
    return pasynOctetSyncIO->flush(pdpvt->pasynUserTCPoctet);
}

static asynStatus
prologixGetEos(void *drvPvt, asynUser *pasynUser,
               char *eos, int eossize, int *eoslen)
{
    dPvt *pdpvt = (dPvt *)drvPvt;

    if (pdpvt->eos < 0) {
        *eoslen = 0;
    }
    else {
        *eoslen = 1;
        if (eossize > 0)
            *eos = pdpvt->eos;
    }
    return asynSuccess;
}

static asynStatus
prologixSetEos(void *drvPvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    int newEos;
    size_t n, nt;

    switch (eoslen) {
    case 0: newEos = -1;          break;
    case 1: newEos = *eos & 0xFF; break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Invalid EOS");
        return asynError;
    }
    if (pdpvt->eos == newEos)
        return asynSuccess;
    n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++eot_enable %d\n",
                                                                (pdpvt->eos < 0));
    return pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf, n, 1.0, &nt);
}

static asynStatus
prologixAddressedCmd(void *drvPvt, asynUser *pasynUser,
                     const char *data, int length)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, "prologixAddressedCmd unimplemented");
    return asynError;
}

static asynStatus
prologixUniversalCmd(void *drvPvt, asynUser *pasynUser, int cmd)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, "prologixUniversalCmd unimplemented");
    return asynError;
}

static asynStatus
prologixIfc(void *drvPvt, asynUser *pasynUser)
{
    dPvt *pdpvt = (dPvt *)drvPvt;
    size_t n, nt;

    n = epicsSnprintf(pdpvt->buf, pdpvt->bufCapacity, "++ifc\n");
    return pasynOctetSyncIO->write(pdpvt->pasynUserTCPoctet, pdpvt->buf, n, 1.0, &nt);
}

static asynStatus
prologixRen(void *drvPvt, asynUser *pasynUser, int onOff)
{
    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize, "prologixRen unimplemented");
    return asynError;
}

static asynStatus
prologixSrqStatus(void *drvPvt, int *srqStatus)
{
    *srqStatus = 0;
    return asynSuccess;
}

static asynStatus
prologixSrqEnable(void *drvPvt, int onOff)
{
    return asynSuccess;
}

static asynStatus
prologixSerialPollBegin(void *drvPvt)
{
    printf ("=== prologixSerialPollBegin unimplemented\n");
    return asynError;
}

static asynStatus
prologixSerialPoll(void *drvPvt, int addr, double timeout, int *statusByte)
{
    printf ("=== prologixSerialPoll unimplemented\n");
    return asynError;
}

static asynStatus
prologixSerialPollEnd(void *drvPvt)
{
    printf ("=== prologixSerialPollEnd unimplemented\n");
    return asynError;
}

static asynGpibPort prologixMethods = {
    prologixReport,
    prologixConnect,
    prologixDisconnect,
    prologixRead,
    prologixWrite,
    prologixFlush,
    prologixSetEos,
    prologixGetEos,
    prologixAddressedCmd,
    prologixUniversalCmd,
    prologixIfc,
    prologixRen,
    prologixSrqStatus,
    prologixSrqEnable,
    prologixSerialPollBegin,
    prologixSerialPoll,
    prologixSerialPollEnd
};

static void
prologixGPIBConfigure(const char *portName, const char *host, int priority, int noAutoConnect)
{
    dPvt *pdpvt;
    asynStatus status;

    /*
     * Set up local storage
     */
    pdpvt = (dPvt *)callocMustSucceed(1, sizeof(dPvt), portName);
    pdpvt->portName = epicsStrDup(portName);
    pdpvt->bufCapacity = 4096;
    pdpvt->buf = callocMustSucceed(1, pdpvt->bufCapacity, portName);
    pdpvt->eos = -1;

    /*
     * Create the port that we'll use for I/O.
     */
    pdpvt->portNameTCP = callocMustSucceed(1, strlen(portName)+10, portName);
    sprintf(pdpvt->portNameTCP, "%s_TCP", portName);
    pdpvt->hostTCP = callocMustSucceed(1, strlen(host)+20, portName);
    if (strchr (host, ':'))
        sprintf(pdpvt->hostTCP, "%s", host);
    else
        sprintf(pdpvt->hostTCP, "%s:1234 TCP", host);
    drvAsynIPPortConfigure(pdpvt->portNameTCP, pdpvt->hostTCP,
                                               priority, 
                                               1, /* No auto connect  */
                                               1  /* No process EOS */ );
    status = pasynCommonSyncIO->connect(pdpvt->portNameTCP, -1,
                                                &pdpvt->pasynUserTCPcommon, NULL);
    if (status != asynSuccess) {
        printf("Can't find ASYN port \"%s\".\n", pdpvt->portNameTCP);
        return;
    }
    status = pasynOctetSyncIO->connect(pdpvt->portNameTCP, -1,
                                                &pdpvt->pasynUserTCPoctet, NULL);
    if (status != asynSuccess) {
        printf("Can't find ASYN port \"%s\".\n", pdpvt->portNameTCP);
        return;
    }
     
    /*
     * Register as a GPIB driver
     */
    pdpvt->asynGpibPvt = pasynGpib->registerPort(pdpvt->portName,
                                             ASYN_CANBLOCK | ASYN_MULTIDEVICE,
                                             !noAutoConnect,
                                             &prologixMethods,
                                             pdpvt,
                                             priority,
                                             0);
    if(pdpvt->asynGpibPvt == NULL) {
        printf("registerPort failed\n");
        return;
    }
}

/*
 * IOC shell command registration
 */
static const iocshArg prologixGPIBConfigureArg0 = { "port",iocshArgString};
static const iocshArg prologixGPIBConfigureArg1 = { "host",iocshArgString};
static const iocshArg prologixGPIBConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg prologixGPIBConfigureArg3 = { "noAutoConnect",iocshArgInt};
static const iocshArg *prologixGPIBConfigureArgs[] = {
                    &prologixGPIBConfigureArg0, &prologixGPIBConfigureArg1,
                    &prologixGPIBConfigureArg2, &prologixGPIBConfigureArg3 };
static const iocshFuncDef prologixGPIBConfigureFuncDef =
      {"prologixGPIBConfigure", 4, prologixGPIBConfigureArgs};
static void prologixGPIBConfigureCallFunc(const iocshArgBuf *args)
{
    prologixGPIBConfigure(args[0].sval, args[1].sval,
                          args[2].ival, args[3].ival);
}

static void
drvPrologixGPIB_RegisterCommands(void)
{
    iocshRegister(&prologixGPIBConfigureFuncDef,prologixGPIBConfigureCallFunc);
}
epicsExportRegistrar(drvPrologixGPIB_RegisterCommands);
