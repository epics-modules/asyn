/*
 * ASYN support for USBTMC (Test & Measurement Class) devices
 *
 ***************************************************************************
 * Copyright (c) 2013 W. Eric Norum <wenorum@lbl.gov>                      *
 * This file is distributed subject to a Software License Agreement found  *
 * in the file LICENSE that is included with this distribution.            *
 ***************************************************************************
 */

#include <string.h>
#include <errlog.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsEvent.h>
#include <epicsMessageQueue.h>
#include <epicsMutex.h>
#include <epicsThread.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <iocsh.h>
#include <asynDriver.h>
#include <asynDrvUser.h>
#include <asynOctet.h>
#include <asynInt32.h>
#include <libusb-1.0/libusb.h>

#define USBTMC_INTERFACE_CLASS    0xFE
#define USBTMC_INTERFACE_SUBCLASS 0x03

#define MESSAGE_ID_DEV_DEP_MSG_OUT         1
#define MESSAGE_ID_REQUEST_DEV_DEP_MSG_IN  2
#define MESSAGE_ID_DEV_DEP_MSG_IN          2

#define BULK_IO_HEADER_SIZE      12
#define BULK_IO_PAYLOAD_CAPACITY 4096
#define IDSTRING_CAPACITY        100

#define ASYN_REASON_SRQ 4345
#define ASYN_REASON_STB 4346
#define ASYN_REASON_REN 4347

#if (!defined(LIBUSBX_API_VERSION) || (LIBUSBX_API_VERSION < 0x01000102))
# error "You need to get a newer version of libsb-1.0 (16 at the very least)"
#endif

typedef struct drvPvt {
    /*
     * Used to find matching device
     */
    int                    vendorId;
    int                    productId;
    const char            *serialNumber;

    /*
     * Matched device
     */
    int                    deviceVendorId;
    int                    deviceProductId;
    char                   deviceVendorString[IDSTRING_CAPACITY];
    char                   deviceProductString[IDSTRING_CAPACITY];
    char                   deviceSerialString[IDSTRING_CAPACITY];

    /*
     * Asyn interfaces we provide
     */
    char                  *portName;
    asynInterface          asynCommon;
    asynInterface          asynOctet;
    asynInterface          asynInt32;
    void                  *asynInt32InterruptPvt;
    asynInterface          asynDrvUser;

    /*
     * Libusb hooks
     */
    libusb_context        *usb;
    libusb_device_handle  *handle;
    int                    bInterfaceNumber;
    int                    bInterfaceProtocol;
    int                    isConnected;
    int                    termChar;
    unsigned char          bTag;
    int                    bulkOutEndpointAddress;
    int                    bulkInEndpointAddress;
    int                    interruptEndpointAddress;

    /*
     * Interrupt endpoint handling
     */
    int                    enableInterruptEndpoint;
    char                  *interruptThreadName;
    epicsThreadId          interruptTid;
    epicsMutexId           interruptTidMutex;
    epicsEventId           pleaseTerminate;
    epicsEventId           didTerminate;
    epicsMessageQueueId    statusByteMessageQueue;

    /*
     * Device capabilities
     */
    unsigned char          tmcInterfaceCapabilities;
    unsigned char          tmcDeviceCapabilities;
    unsigned char          usb488InterfaceCapabilities;
    unsigned char          usb488DeviceCapabilities;

    /*
     * I/O buffer
     */
    unsigned char          buf[BULK_IO_HEADER_SIZE+BULK_IO_PAYLOAD_CAPACITY];
    int                    bufCount;
    const unsigned char   *bufp;
    unsigned char          bulkInPacketFlags;

    /*
     * Statistics
     */
    size_t                 connectionCount;
    size_t                 interruptCount;
    size_t                 bytesSentCount;
    size_t                 bytesReceivedCount;
} drvPvt;

static asynStatus disconnect(void *pvt, asynUser *pasynUser);

/*
 * Interrupt endpoint support
 */
static void
interruptThread(void *arg)
{
    drvPvt *pdpvt = (drvPvt *)arg;
    unsigned char cbuf[2];
    int n;
    int s;

    for (;;) {
        s =  libusb_interrupt_transfer (pdpvt->handle,
                                        pdpvt->interruptEndpointAddress,
                                        cbuf,
                                        sizeof cbuf,
                                        &n,
                                        65000);
        if (epicsEventTryWait(pdpvt->pleaseTerminate) == epicsEventWaitOK)
            break;
        if ((s == 0) && (n == sizeof cbuf)) {
            if (cbuf[0] == 0x81) {
                ELLLIST *pclientList;
                interruptNode *pnode;

                pdpvt->interruptCount++;
                pasynManager->interruptStart(pdpvt->asynInt32InterruptPvt, &pclientList);
                pnode = (interruptNode *)ellFirst(pclientList);
                while (pnode) {
                    asynInt32Interrupt *int32Interrupt = pnode->drvPvt;
                    pnode = (interruptNode *)ellNext(&pnode->node);
                    if (int32Interrupt->pasynUser->reason == ASYN_REASON_SRQ) {
                        int32Interrupt->callback(int32Interrupt->userPvt,
                                                 int32Interrupt->pasynUser,
                                                 cbuf[1]);
                    }
                }
                pasynManager->interruptEnd(pdpvt->asynInt32InterruptPvt);
            }
            else if (cbuf[0] == 0x82) {
                if (epicsMessageQueueTrySend(pdpvt->statusByteMessageQueue,
                                                            &cbuf[1], 1) != 0) {
                    errlogPrintf("----- WARNING ----- "
                                 "Can't send status byte to worker thread!\n");
                }
            }
        }
        else if (s != LIBUSB_ERROR_TIMEOUT) {
            errlogPrintf("----- WARNING ----- "
                         "libusb_interrupt_transfer failed (%s).  "
                         "Interrupt thread for ASYN port \"%s\" terminating.\n",
                                       libusb_strerror(s), pdpvt->portName);
            break;
        }
    }
    epicsMutexLock(pdpvt->interruptTidMutex);
    pdpvt->interruptTid = 0;
    epicsEventSignal(pdpvt->didTerminate);
    epicsMutexUnlock(pdpvt->interruptTidMutex);
}

static void
startInterruptThread(drvPvt *pdpvt)
{
    epicsMutexLock(pdpvt->interruptTidMutex);
    if (pdpvt->enableInterruptEndpoint
     && pdpvt->interruptEndpointAddress
     && (pdpvt->interruptTid == 0)) {
        epicsEventTryWait(pdpvt->pleaseTerminate);
        epicsEventTryWait(pdpvt->didTerminate);
        pdpvt->interruptTid = epicsThreadCreate(pdpvt->interruptThreadName,
                                epicsThreadGetPrioritySelf(),
                                epicsThreadGetStackSize(epicsThreadStackSmall),
                                interruptThread, pdpvt);
        if (pdpvt->interruptTid == 0)
            errlogPrintf("----- WARNING ----- "
                   "Can't start interrupt handler thread %s.\n",
                                                    pdpvt->interruptThreadName);
    }
    epicsMutexUnlock(pdpvt->interruptTidMutex);
}

/*
 * Decode a status byte
 */
static void
showHexval(FILE *fp, const char *name, int val, int bitmask, const char *bitname, ...)
{
    const char *sep = " -- ";
    va_list ap;
    va_start(ap, bitname);
    fprintf(fp, "%28s: ", name);
    if (bitmask)
        fprintf(fp, "%#x", val);
    else
        fprintf(fp, "%#4.4x", val);
    for (;;) {
        if (((bitmask > 0) && ((val & bitmask)) != 0)
         || ((bitmask < 0) && ((val & -bitmask)) == 0)
         || ((bitmask == 0) && (bitname != NULL) && (bitname[0] != '\0'))) {
            fprintf(fp, "%s%s", sep, bitname);
            sep = ", ";
        }
        if (bitmask == 0)
            break;
        bitmask = va_arg(ap, int);
        if (bitmask == 0)
            break;
        bitname = va_arg(ap, char *);
    }
    fprintf(fp, "\n");
    va_end(ap);
}

/*
 * Show a byte number
 */
static void
pcomma(FILE *fp, size_t n)
{
    if (n < 1000) {
        fprintf(fp, "%zu", n);
        return;
    }
    pcomma(fp, n/1000);
    fprintf(fp, ",%03zu", n%1000);
}
    
static void
showCount(FILE *fp, const char *label, size_t count)
{
    fprintf(fp, "%22s Count: ", label);
    pcomma(fp, count);
    fprintf(fp, "\n");
}

/*
 * asynCommon methods
 */
static void
report(void *pvt, FILE *fp, int details)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    fprintf(fp, "%20sonnected, Interrupt handler thread %sactive\n",
                                            pdpvt->isConnected ? "C" : "Disc",
                                            pdpvt->interruptTid ? "" : "in");
    showHexval(fp, "Vendor", pdpvt->deviceVendorId, 0, pdpvt->deviceVendorString);
    showHexval(fp, "Product", pdpvt->deviceProductId, 0, pdpvt->deviceProductString);
    if (pdpvt->deviceSerialString[0])
        fprintf(fp, "%28s: \"%s\"\n", "Serial Number", pdpvt->deviceSerialString);
    if (details > 0) {
        fprintf(fp, "          Interface Protocol: %x", pdpvt->bInterfaceProtocol);
        switch (pdpvt->bInterfaceProtocol) {
        case 0: fprintf(fp, " -- USBTMC\n");                break;
        case 1: fprintf(fp, " -- USBTMC USB488\n");         break;
        default: fprintf(fp, "\n");                         break;
        }
        if (pdpvt->termChar >= 0)
            fprintf(fp, "%28s: %x\n", "Terminator", pdpvt->termChar);
        showHexval(fp, "TMC Interface Capabilities",
                                   pdpvt->tmcInterfaceCapabilities,
                                   0x4, "Accepts INDICATOR_PULSE",
                                   0x2, "Talk-only",
                                   0x1, "Listen-only",
                                   -0x3, "Talk/Listen",
                                   0);
        showHexval(fp, "TMC Device Capabilities",
                                   pdpvt->tmcDeviceCapabilities,
                                   0x1, "Supports termChar",
                                   0);
        if (pdpvt->bInterfaceProtocol == 1) {
            showHexval(fp, "488 Interface Capabilities",
                                   pdpvt->usb488InterfaceCapabilities,
                                   0x4, "488.2",
                                   0x2, "REN/GTL/LLO",
                                   0x1, "TRG",
                                   0);
            showHexval(fp, "488 Device Capabilities",
                                   pdpvt->usb488DeviceCapabilities,
                                    0x8, "SCPI",
                                    0x4, "SR1",
                                   -0x4, "SR0",
                                    0x2, "RL1",
                                   -0x2, "RL0",
                                    0x1, "DT1",
                                   -0x1, "DT0",
                                   0);
        }
    }
    if (details > 1) {
        fprintf(fp, "        Bulk output endpoint: %x\n", pdpvt->bulkOutEndpointAddress);
        fprintf(fp, "         Bulk input endpoint: %x\n", pdpvt->bulkInEndpointAddress);
        fprintf(fp, "          Interrupt endpoint: %x\n", pdpvt->interruptEndpointAddress);
        showCount(fp, "Connection", pdpvt->connectionCount);
        showCount(fp, "Interrupt", pdpvt->interruptCount);
        showCount(fp, "Send", pdpvt->bytesSentCount);
        showCount(fp, "Receive", pdpvt->bytesReceivedCount);
    }
    if (details >= 100) {
        int l = details % 100;
        fprintf(fp, "==== Set libusb debug level %d ====\n", l);
        libusb_set_debug(pdpvt->usb, l);
    }
}

/*
 * Get USB descriptor as an ASCII string.
 */
static void
getDeviceString(drvPvt *pdpvt, int i, char *dest)
{
    ssize_t n;

    n = libusb_get_string_descriptor_ascii(pdpvt->handle, i,
                                            pdpvt->buf, sizeof pdpvt->buf);
    if (n < 0) {
        *dest = '\0';
        return;
    }
    if(n >= IDSTRING_CAPACITY)
        n = IDSTRING_CAPACITY - 1;
    strncpy(dest, (char *)pdpvt->buf, n);
}
static void
getDeviceStrings(drvPvt *pdpvt, struct libusb_device_descriptor *desc)
{
    getDeviceString(pdpvt, desc->iManufacturer, pdpvt->deviceVendorString);
    getDeviceString(pdpvt, desc->iProduct, pdpvt->deviceProductString);
    getDeviceString(pdpvt, desc->iSerialNumber, pdpvt->deviceSerialString);
}

/*
 * Get endpoints
 */
static void
getEndpoints(drvPvt *pdpvt, const struct libusb_interface_descriptor *iface_desc)
{
    int e;

    pdpvt->bulkInEndpointAddress = 0;
    pdpvt->bulkOutEndpointAddress = 0;
    pdpvt->interruptEndpointAddress = 0;
    for (e = 0 ; e < iface_desc->bNumEndpoints ; e++) {
        const struct libusb_endpoint_descriptor *ep = &iface_desc->endpoint[e];
        switch (ep->bmAttributes & LIBUSB_TRANSFER_TYPE_MASK) {
        case LIBUSB_TRANSFER_TYPE_BULK:
            if ((ep->bEndpointAddress & LIBUSB_ENDPOINT_DIR_MASK) == LIBUSB_ENDPOINT_IN)
                pdpvt->bulkInEndpointAddress = ep->bEndpointAddress;
            else
                pdpvt->bulkOutEndpointAddress = ep->bEndpointAddress;
            break;

        case LIBUSB_TRANSFER_TYPE_INTERRUPT:
            pdpvt->interruptEndpointAddress = ep->bEndpointAddress;
            break;
        }
    }
}

/*
 * Search the bus for a matching device
 */
static asynStatus
findDevice(drvPvt *pdpvt, asynUser *pasynUser, libusb_device **list, int n)
{
    int i, j, k;

    epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                        "No Vendor/Product/Serial match found");
    for (i = 0 ; i < n ; i++) {
        libusb_device *dev = list[i];
        struct libusb_device_descriptor desc;
        struct libusb_config_descriptor *config;

        int s = libusb_get_device_descriptor(dev, &desc);
        if (s != 0) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "libusb_get_device_descriptor failed: %s", libusb_strerror(s));
            return asynError;
        }
        if (desc.bDeviceClass != LIBUSB_CLASS_PER_INTERFACE)
            continue;
        if ((libusb_get_active_config_descriptor(dev, &config) < 0)
         && (libusb_get_config_descriptor(dev, 0, &config) < 0))
            continue;
        if (config == NULL)
            continue;
        for (j = 0 ; j < config->bNumInterfaces ; j++) {
            const struct libusb_interface *iface = &config->interface[j];
            for (k = 0 ; k < iface->num_altsetting ; k++) {
                const struct libusb_interface_descriptor *iface_desc;
                iface_desc = &iface->altsetting[k];
                if ((iface_desc->bInterfaceClass==USBTMC_INTERFACE_CLASS)
                 && (iface_desc->bInterfaceSubClass==USBTMC_INTERFACE_SUBCLASS)
                 && ((pdpvt->vendorId==0) || (pdpvt->vendorId==desc.idVendor))
                 && ((pdpvt->productId==0) || (pdpvt->productId==desc.idProduct))) {
                    pdpvt->bInterfaceNumber = iface_desc->bInterfaceNumber;
                    pdpvt->bInterfaceProtocol = iface_desc->bInterfaceProtocol;
                    s = libusb_open(dev, &pdpvt->handle);
                    if (s == 0) {
                        pdpvt->deviceVendorId = desc.idVendor;
                        pdpvt->deviceProductId = desc.idProduct;
                        getDeviceStrings(pdpvt, &desc);
                        if ((pdpvt->serialNumber == NULL)
                         || (strcmp(pdpvt->serialNumber,
                             pdpvt->deviceSerialString) == 0)) {
                            getEndpoints(pdpvt, iface_desc);
                            libusb_free_config_descriptor(config);
                            pasynUser->errorMessage[0] = '\0';
                            return asynSuccess;
                        }
                        libusb_close(pdpvt->handle);
                    }
                    else {
                        epicsSnprintf(pasynUser->errorMessage,
                                      pasynUser->errorMessageSize,
                                            "libusb_open failed: %s",
                                                        libusb_strerror(s));
                    }
                }
            }
        }
        libusb_free_config_descriptor(config);
    }
    return asynError;
}

/*
 * Disconnect when it appears that device has gone away
 */
static void
disconnectIfGone(drvPvt *pdpvt, asynUser *pasynUser, int s)
{
    if (s == LIBUSB_ERROR_NO_DEVICE)
        disconnect(pdpvt, pasynUser);
}

/*
 * Check results of control transfer
 */
static asynStatus
checkControlTransfer(const char *msg, drvPvt *pdpvt, asynUser *pasynUser,
                     int s, int want, int usbtmcStatus)
{
    if (s < 0) {
        disconnectIfGone(pdpvt, pasynUser, s);
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "USBTMC %s failed: %s", msg, libusb_strerror(s));
        return asynError;
    }
    if (s != want) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "USBTMC %s failed -- asked for 0x%x, got %x", msg, want, s);
        return asynError;
    }
    if (usbtmcStatus != 1) {
        const char *cp;
        switch (usbtmcStatus) {
        case 0x02:   cp = " (STATUS_PENDING)";                       break;
        case 0x80:   cp = " (STATUS_FAILED)";                        break;
        case 0x81:   cp = " (STATUS_TRANSFER_NOT_IN_PROGRESS)";      break;
        case 0x82:   cp = " (STATUS_SPLIT_NOT_IN_PROGRESS)";         break;
        case 0x83:   cp = " (STATUS_SPLIT_IN_PROGRESS)";             break;
        default:     cp = "";                                        break;
        }
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "USBTMC %s failed -- USBTMC_status: 0x%x%s", msg, usbtmcStatus, cp);
        return asynError;
    }
    return asynSuccess;
}

/*
 * Get device capabilities
 */
static asynStatus
getCapabilities(drvPvt *pdpvt, asynUser *pasynUser)
{
    int s;
    asynStatus status;

    s = libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                0x07, // bRequest: USBTMC GET_CAPABILITIES
                0x0000,                  // wValue
                pdpvt->bInterfaceNumber, // wIndex
                pdpvt->buf,              // data
                0x0018,                  // wLength
                100);                    // timeout (ms)
    status = checkControlTransfer("GET_CAPABILITIES", pdpvt, pasynUser,
                                  s, 0x18, pdpvt->buf[0]);
    if (status != asynSuccess)
        return status;
    pdpvt->tmcInterfaceCapabilities = pdpvt->buf[4];
    pdpvt->tmcDeviceCapabilities = pdpvt->buf[5];
    if (pdpvt->bInterfaceProtocol == 1) {
        pdpvt->usb488InterfaceCapabilities = pdpvt->buf[14];
        pdpvt->usb488DeviceCapabilities = pdpvt->buf[15];
    }
    return asynSuccess;
}

/*
 * Clear input/output buffers
 */
static asynStatus
clearBuffers(drvPvt *pdpvt, asynUser *pasynUser)
{
    int s;
    asynStatus status;
    int pass = 0;
    unsigned char cbuf[2];

    s = libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                0x05, // bRequest: USBTMC INITIATE_CLEAR
                0x0000,                  // wValue
                pdpvt->bInterfaceNumber, // wIndex
                cbuf,                    // data
                1,                       // wLength
                100);                    // timeout (ms)
    status = checkControlTransfer("INITIATE_CLEAR", pdpvt, pasynUser,
                                  s, 1, cbuf[0]);
    if (status != asynSuccess)
        return status;
    for (;;) {
        epicsThreadSleep(0.01); // I don't know why this is necessary, but without some delay here the CHECK_CLEAR_STATUS seems to be stuck at STATUS_PENDING
        s = libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                0x06, // bRequest: USBTMC CHECK_CLEAR_STATUS
                0x0000,                  // wValue
                pdpvt->bInterfaceNumber, // wIndex
                cbuf,                    // data
                0x0002,                  // wLength
                100);                    // timeout (ms)
        status = checkControlTransfer("CHECK_CLEAR_STATUS", pdpvt, pasynUser,
                                      s, 2, 1);
        if (status != asynSuccess)
            return asynError;
        if (cbuf[0] != 2) {
            libusb_clear_halt(pdpvt->handle, pdpvt->bulkInEndpointAddress);
            libusb_clear_halt(pdpvt->handle, pdpvt->bulkOutEndpointAddress);
            if (cbuf[0] == 1)
                return asynSuccess;
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                        "CHECK_CLEAR_STATUS failed -- status: %x", cbuf[0]);
            return asynError;
        }
        switch (++pass) {
        case 5:
            asynPrint(pasynUser, ASYN_TRACE_ERROR, "Note -- RESET DEVICE.\n");
            s = libusb_reset_device(pdpvt->handle);
            if (s != 0) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "libusb_reset_device() failed: %s", libusb_strerror(s));
                return asynError;
            }
            break;

        case 10:
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "CHECK_CLEAR_STATUS remained 'STATUS_PENDING' for too long");
            return asynError;
        }
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                                    "Note -- retrying CHECK_CLEAR_STATUS\n");
    }
    return asynSuccess;
}

static asynStatus
connect(void *pvt, asynUser *pasynUser)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    asynStatus status;

    if (!pdpvt->isConnected) {
        libusb_device **list;
        ssize_t n;
        int s;

        n = libusb_get_device_list(pdpvt->usb, &list);
        if (n < 0) {
            s = n;
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "libusb_get_device_list failed: %s", libusb_strerror(s));
            return asynError;
        }
        status = findDevice(pdpvt, pasynUser, list, n);
        libusb_free_device_list(list, 1);
        if (status != asynSuccess)
            return asynError;
         s = libusb_claim_interface(pdpvt->handle, pdpvt->bInterfaceNumber);
         if (s == LIBUSB_ERROR_BUSY) {
             libusb_detach_kernel_driver(pdpvt->handle, pdpvt->bInterfaceNumber);
             s = libusb_claim_interface(pdpvt->handle, pdpvt->bInterfaceNumber);
         }
         if (s) {
            libusb_close(pdpvt->handle);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "libusb_claim_interface failed: %s", libusb_strerror(s));
            return asynError;
         }
         if (getCapabilities(pdpvt, pasynUser) != asynSuccess) {
            libusb_close(pdpvt->handle);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Can't get device capabilities: %s", pasynUser->errorMessage);
            return asynError;
        }
         if (clearBuffers(pdpvt, pasynUser) != asynSuccess) {
            libusb_close(pdpvt->handle);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Can't clear buffers: %s", pasynUser->errorMessage);
            return asynError;
        }
        pdpvt->bulkInPacketFlags = 0;
        pdpvt->bufCount = 0;
        pdpvt->connectionCount++;
        startInterruptThread(pdpvt);
    }
    pdpvt->isConnected = 1;
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
disconnect(void *pvt, asynUser *pasynUser)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (pdpvt->isConnected) {
        int pass = 0;
        epicsThreadId tid;
        for (;;) {
            unsigned char cbuf[3];

            epicsMutexLock(pdpvt->interruptTidMutex);
            tid = pdpvt->interruptTid;
            epicsMutexUnlock(pdpvt->interruptTidMutex);
            if (tid == 0)
                break;
            if (++pass == 10) {
                errlogPrintf("----- WARNING ----- Thread %s won't terminate!\n",
                                                    pdpvt->interruptThreadName);
                break;
            }

            /*
             * Send signal then force an Interrupt-In message
             */
            epicsEventSignal(pdpvt->pleaseTerminate);
            libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                128,  // bRequest: READ_STATUS_BYTE
                127,                     // wValue (bTag)
                pdpvt->bInterfaceNumber, // wIndex
                cbuf,                    // data
                3,                       // wLength
                100);                    // timeout (ms)
            epicsEventWaitWithTimeout(pdpvt->didTerminate, 2.0);
        }
        libusb_close(pdpvt->handle);
    }
    pdpvt->isConnected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}
static asynCommon commonMethods = { report, connect, disconnect };

/*
 * asynOctet methods
 */
static asynStatus
asynOctetWrite(void *pvt, asynUser *pasynUser,
               const char *data, size_t numchars, size_t *nbytesTransfered)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;

    /*
     * Common to all writes
     */
    pdpvt->bufCount = 0;
    pdpvt->bulkInPacketFlags = 0;
    pdpvt->buf[0] = MESSAGE_ID_DEV_DEP_MSG_OUT;
    pdpvt->buf[3] = 0;
    pdpvt->buf[9] = 0;
    pdpvt->buf[10] = 0;
    pdpvt->buf[11] = 0;

    /*
     * Send
     */
    *nbytesTransfered = 0;
    while (numchars) {
        int nSend, pkSend, pkSent;
        int s;
        if (numchars > BULK_IO_PAYLOAD_CAPACITY) {
            nSend = BULK_IO_PAYLOAD_CAPACITY;
            pdpvt->buf[8] = 0;
        }
        else {
            nSend = numchars;
            pdpvt->buf[8] = 1;
        }
        pdpvt->buf[1] = pdpvt->bTag;
        pdpvt->buf[2] = ~pdpvt->bTag;
        pdpvt->buf[4] = nSend;
        pdpvt->buf[5] = nSend >> 8;
        pdpvt->buf[6] = nSend >> 16;
        pdpvt->buf[7] = nSend >> 24;
        memcpy(&pdpvt->buf[BULK_IO_HEADER_SIZE], data, nSend);
        pdpvt->bTag = (pdpvt->bTag == 0xFF) ? 0x1 : pdpvt->bTag + 1;
        pkSend = nSend + BULK_IO_HEADER_SIZE;
        while (pkSend & 0x3)
            pdpvt->buf[pkSend++] = 0;
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, (const char *)pdpvt->buf,
                                                    pkSend, "Send %d: ", pkSend);
        s = libusb_bulk_transfer(pdpvt->handle, pdpvt->bulkOutEndpointAddress,
                                      pdpvt->buf, pkSend, &pkSent, timeout);
        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "Bulk transfer failed: %s", libusb_strerror(s));
            return asynError;
        }
        if (pkSent != pkSend) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                        "Asked to send %d, actually sent %d", pkSend, pkSent);
            return asynError;
        }
        data += nSend;
        numchars -= nSend;
        *nbytesTransfered += nSend;
    }
    pdpvt->bytesSentCount += *nbytesTransfered;
    return asynSuccess;
}

static asynStatus
asynOctetRead(void *pvt, asynUser *pasynUser,
              char *data, size_t maxchars, size_t *nbytesTransfered,
              int *eomReason)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    unsigned char bTag;
    int s;
    int nCopy, ioCount, payloadSize;
    int eom = 0;
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;

    *nbytesTransfered = 0;
    for (;;) {
        /*
         * Special case for stream device which requires an asynTimeout return.
         */
        if ((pasynUser->timeout == 0) && (pdpvt->bufCount == 0))
            return asynTimeout;

        /*
         * Transfer buffered data
         */
        if (pdpvt->bufCount) {
            nCopy = maxchars;
            if (nCopy > pdpvt->bufCount)
                nCopy = pdpvt->bufCount;
            memcpy(data, pdpvt->bufp, nCopy);
            pdpvt->bufp += nCopy;
            pdpvt->bufCount -= nCopy;
            maxchars -= nCopy;
            *nbytesTransfered += nCopy;
            pdpvt->bytesReceivedCount += nCopy;
            data += nCopy;
            if (maxchars == 0)
                eom |= ASYN_EOM_CNT;
        }
        if (pdpvt->bufCount == 0) {
            if (pdpvt->bulkInPacketFlags & 0x2)
                eom |= ASYN_EOM_EOS;
            if (pdpvt->bulkInPacketFlags & 0x1)
                eom |= ASYN_EOM_END;
        }
        if (eom) {
            if (eomReason) *eomReason = eom;
            return asynSuccess;
        }

        /*
         * Request another chunk
         */
        pdpvt->bulkInPacketFlags = 0;
        pdpvt->buf[0] = MESSAGE_ID_REQUEST_DEV_DEP_MSG_IN;
        pdpvt->buf[1] = pdpvt->bTag;
        pdpvt->buf[2] = ~pdpvt->bTag;
        pdpvt->buf[3] = 0;
        pdpvt->buf[4] = BULK_IO_PAYLOAD_CAPACITY & 0xFF;
        pdpvt->buf[5] = (BULK_IO_PAYLOAD_CAPACITY >> 8) & 0xFF;
        pdpvt->buf[6] = (BULK_IO_PAYLOAD_CAPACITY >> 16) & 0xFF;
        pdpvt->buf[7] = (BULK_IO_PAYLOAD_CAPACITY >> 24) & 0xFF;
        if (pdpvt->termChar >= 0) {
            pdpvt->buf[8] = 2;
            pdpvt->buf[9] = pdpvt->termChar;
        }
        else {
            pdpvt->buf[8] = 0;
            pdpvt->buf[9] = 0;
        }
        pdpvt->buf[10] = 0;
        pdpvt->buf[11] = 0;
        bTag = pdpvt->bTag;
        pdpvt->bTag = (pdpvt->bTag == 0xFF) ? 0x1 : pdpvt->bTag + 1;
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, (const char *)pdpvt->buf,
                            BULK_IO_HEADER_SIZE,
                            "Request %d, command: ", BULK_IO_PAYLOAD_CAPACITY);
        s = libusb_bulk_transfer(pdpvt->handle, pdpvt->bulkOutEndpointAddress,
                          pdpvt->buf, BULK_IO_HEADER_SIZE, &ioCount, timeout);
        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                        "Bulk transfer request failed: %s", libusb_strerror(s));
            return asynError;
        }

        /*
         * Read back
         */
        s = libusb_bulk_transfer(pdpvt->handle, pdpvt->bulkInEndpointAddress, pdpvt->buf,
                                            sizeof pdpvt->buf, &ioCount, timeout);
        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                    "Bulk read failed: %s", libusb_strerror(s));
            return asynError;
        }
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, (const char *)pdpvt->buf,
                        ioCount, "Read %d, flags %#x: ", ioCount, pdpvt->buf[8]);

        /*
         * Sanity check on transfer
         */
        if (ioCount < BULK_IO_HEADER_SIZE) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Incomplete packet header (read only %d)", ioCount);
            return asynError;
        }
        if ((pdpvt->buf[0] != MESSAGE_ID_REQUEST_DEV_DEP_MSG_IN)
         || (pdpvt->buf[1] != bTag)
         || (pdpvt->buf[2] != (unsigned char)~bTag)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Packet header corrupt %x %x %x (btag %x)",
                        pdpvt->buf[0], pdpvt->buf[1], pdpvt->buf[2], bTag);
            return asynError;
        }
        payloadSize = pdpvt->buf[4]        |
                     (pdpvt->buf[5] << 8)  |
                     (pdpvt->buf[6] << 16) |
                     (pdpvt->buf[7] << 24);
        if (payloadSize > (ioCount - BULK_IO_HEADER_SIZE)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "Packet header claims %d sent, but packet contains only %d",
                            payloadSize, ioCount - BULK_IO_HEADER_SIZE);
            return asynError;
        }
        if (payloadSize > BULK_IO_PAYLOAD_CAPACITY) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Packet header claims %d sent, but requested only %d",
                                    payloadSize, BULK_IO_PAYLOAD_CAPACITY);
            return asynError;
        }
        pdpvt->bufCount = payloadSize;
        pdpvt->bufp = &pdpvt->buf[BULK_IO_HEADER_SIZE];

        /*
         * USB TMC uses a short packet to mark the end of a transfer
         * It's not clear to me that the library exposes this short
         * packet in the case following a read that just happens to
         * finish on a pdpvt->buf sized boundary.  For now I am assuming
         * that I will, in fact, get a short packet following.
         */
        pdpvt->bulkInPacketFlags = pdpvt->buf[8];
    }
}

/*
 * I see no mechanism for determining when it is necessary/possible to issue
 * MESSAGE_ID_REQUEST_DEV_DEP_MSG_IN requests and transfers from the bulk-IN
 * endpoint.  I welcome suggestions from libusb experts.
 */
static asynStatus
asynOctetFlush(void *pvt, asynUser *pasynUser)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    pdpvt->bufCount = 0;
    pdpvt->bulkInPacketFlags = 0;
    return asynSuccess;
}

static asynStatus
asynOctetSetInputEos(void *pvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    switch (eoslen) {
    case 0:
        pdpvt->termChar = -1;
        break;
    case 1:
        if ((pdpvt->tmcDeviceCapabilities & 0x1) == 0) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Device does not support terminating characters");
            return asynError;
        }
        pdpvt->termChar = *eos & 0xFF;
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Device does not support multiple terminating characters");
        return asynError;
    }
    return asynSuccess;
}

static asynStatus
asynOctetGetInputEos(void *pvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (pdpvt->termChar < 0) {
        *eoslen = 0;
    }
    else {
        if (eossize < 1) return asynError;
        *eos = pdpvt->termChar;
        *eoslen = 1;
    }
    return asynSuccess;
}

static asynStatus
asynOctetSetOutputEos(void *pvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    return asynError;
}

static asynStatus
asynOctetGetOutputEos(void *pvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    return asynError;
}

static asynOctet octetMethods = { 
    .write        = asynOctetWrite, 
    .read         = asynOctetRead, 
    .flush        = asynOctetFlush,
    .setInputEos  = asynOctetSetInputEos,
    .getInputEos  = asynOctetGetInputEos,
    .setOutputEos = asynOctetSetOutputEos,
    .getOutputEos = asynOctetGetOutputEos,
};

/*
 * asynInt32 methods
 */
static asynStatus
asynInt32Write(void *pvt, asynUser *pasynUser, epicsInt32 value)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    int s;
    asynStatus status;
    unsigned char cbuf[1];

    switch (pasynUser->reason) {
    case ASYN_REASON_REN:
        if ((pdpvt->usb488InterfaceCapabilities & 0x2) == 0) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "Device does not support REN operations.");
            return asynError;
        }
        s = libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                160,  // bRequest: USBTMC REN_CONTROL
                (value != 0),            // wValue
                pdpvt->bInterfaceNumber, // wIndex
                cbuf,                    // data
                1,                       // wLength
                100);                    // timeout (ms)
        status = checkControlTransfer("REN_CONTROL", pdpvt, pasynUser,
                                      s, 1, cbuf[0]);
        if (status != asynSuccess)
            return status;
        return asynSuccess;

    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynInt32Write -- invalid drvUser (reason) %d", pasynUser->reason);
        return asynError;
    }
}

static asynStatus
asynInt32Read(void *pvt, asynUser *pasynUser, epicsInt32 *value)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    int s;
    asynStatus status;
    unsigned char cbuf[3];

    switch (pasynUser->reason) {
    case ASYN_REASON_STB:
        if (pdpvt->interruptTid) {
            /* Flush queue */
            epicsMessageQueueTryReceive(pdpvt->statusByteMessageQueue, cbuf, 1);
        }
        s = libusb_control_transfer(pdpvt->handle,
                0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
                128,  // bRequest: USBTMC READ_STATUS_BYTE
                2,                       // wValue (bTag)
                pdpvt->bInterfaceNumber, // wIndex
                cbuf,                    // data
                3,                       // wLength
                100);                    // timeout (ms)
        status = checkControlTransfer("READ_STATUS_BYTE", pdpvt, pasynUser,
                                      s, 3, cbuf[0]);
        if (status != asynSuccess)
            return status;

        /*
         * Value can be returned in three different ways
         */
        if (pdpvt->interruptEndpointAddress == 0) {
            /*
             * 1 - Directly in the reply
             */
            *value = cbuf[2];
        }
        else if (pdpvt->interruptTid == 0) {
            int n;
            /*
             * 2 - Read status here
             */
            s =  libusb_interrupt_transfer (pdpvt->handle,
                                            pdpvt->interruptEndpointAddress,
                                            cbuf,
                                            2,
                                            &n,
                                            100);
            if (s < 0) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "libusb_interrupt_transfer failed: %s", libusb_strerror(s));
                return asynError;
            }
            if (n != 2) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "libusb_interrupt_transfer got %d, expected 2", n);
                return asynError;
            }
            if (cbuf[0] != 0x82) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "libusb_interrupt_transfer bTag 0x%x, expected 0x82", cbuf[0]);
                return asynError;
            }
            *value = cbuf[1];
        }
        else {
            /*
             * 3 - Read status in interrupt thread
             */
            s = epicsMessageQueueReceiveWithTimeout(
                                pdpvt->statusByteMessageQueue, cbuf, 1, 0.2);
            if (s != 1) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                        "No status byte from interrupt thread");
                return asynError;
            }
            *value = cbuf[0];
        }
        asynPrint(pasynUser, ASYN_TRACEIO_DRIVER, "READ_STATUS_BYTE: 0x%x\n", *value);
        return asynSuccess;

    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "asynInt32Read -- invalid drvUser (reason) %d", pasynUser->reason);
        return asynError;
    }
}

static asynInt32 int32Methods = {
    .write  = asynInt32Write,
    .read   = asynInt32Read,
};

/*
 * drvUser methods
 */
static asynStatus
asynDrvUserCreate(void *pvt, asynUser *pasynUser,
                  const char *drvInfo, const char **pptypeName, size_t *psize)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (epicsStrCaseCmp(drvInfo, "SRQ") == 0) {
        pasynUser->reason = ASYN_REASON_SRQ;
        pdpvt->enableInterruptEndpoint = 1;
        if (pdpvt->isConnected)
            startInterruptThread(pdpvt);
    }
    else if (epicsStrCaseCmp(drvInfo, "REN") == 0) {
        pasynUser->reason = ASYN_REASON_REN;
    }
    else if (epicsStrCaseCmp(drvInfo, "STB") == 0) {
        pasynUser->reason = ASYN_REASON_STB;
    }
    return asynSuccess;
}

static asynStatus
asynDrvUserGetType(void *drvPvt, asynUser *pasynUser,
                   const char **pptypeName, size_t *psize)
{
    return asynSuccess;
}

static asynStatus
asynDrvUserDestroy(void *drvPvt, asynUser *pasynUser)
{
    return asynSuccess;
}

static asynDrvUser drvUserMethods = {
    .create  = asynDrvUserCreate,
    .getType = asynDrvUserGetType,
    .destroy = asynDrvUserDestroy,
};

/*
 * Device configuration
 */
void
usbtmcConfigure(const char *portName,
                int vendorId, int productId, const char *serialNumber,
                int priority, int flags)
{
    drvPvt *pdpvt;
    int s;
    asynStatus status;

    /*
     * Set up local storage
     */
    pdpvt = (drvPvt *)callocMustSucceed(1, sizeof(drvPvt), portName);
    pdpvt->portName = epicsStrDup(portName);
    pdpvt->interruptThreadName = callocMustSucceed(1, strlen(portName)+5, portName);
    epicsSnprintf(pdpvt->interruptThreadName, sizeof pdpvt->interruptThreadName, "%sIntr", portName);
    if (priority == 0) priority = epicsThreadPriorityMedium;
    s = libusb_init(&pdpvt->usb);
    if (s != 0) {
        printf("libusb_init() failed: %s\n", libusb_strerror(s));
        return;
    }
    if ((serialNumber == NULL) || (*serialNumber == '\0')) {
        if ((vendorId == 0) && (productId == 0))
            printf("No device information specified.  Will connect to first USB TMC device found.\n");
        else if (vendorId == 0)
            printf("Will connect to first USB TMC device found with product ID %#4.4x.\n", productId);
        else if (productId == 0)
            printf("Will connect to first USB TMC device found with vendor ID %#4.4x.\n", vendorId);
         else
            printf("Will connect to first USB TMC device found with vendor ID %#4.4x and product ID %#4.4x.\n", vendorId, productId);
    }
    pdpvt->vendorId = vendorId;
    pdpvt->productId = productId;
    if (serialNumber && *serialNumber)
        pdpvt->serialNumber = epicsStrDup(serialNumber);
    else
        pdpvt->serialNumber = NULL;
    pdpvt->termChar = -1;
    pdpvt->bTag = 1;
    pdpvt->interruptTidMutex = epicsMutexMustCreate();
    pdpvt->pleaseTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->didTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->statusByteMessageQueue = epicsMessageQueueCreate(1, 1);
    if (!pdpvt->statusByteMessageQueue) {
        printf("Can't create message queue!\n");
        return;
    }

    /*
     * Create our port
     */
    status = pasynManager->registerPort(pdpvt->portName,
                                        ASYN_CANBLOCK,
                                        (flags & 0x1) == 0,
                                        priority, 0);
    if(status != asynSuccess) {
        printf("registerPort failed\n");
        return;
    }

    /*
     * Register ASYN interfaces
     */
    pdpvt->asynCommon.interfaceType = asynCommonType;
    pdpvt->asynCommon.pinterface  = &commonMethods;
    pdpvt->asynCommon.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName, &pdpvt->asynCommon);
    if (status != asynSuccess) {
        printf("registerInterface failed\n");
        return;
    }

    pdpvt->asynOctet.interfaceType = asynOctetType;
    pdpvt->asynOctet.pinterface  = &octetMethods;
    pdpvt->asynOctet.drvPvt = pdpvt;
    status = pasynOctetBase->initialize(pdpvt->portName, &pdpvt->asynOctet, 0, 0, 0);
    if (status != asynSuccess) {
        printf("pasynOctetBase->initialize failed\n");
        return;
    }

    pdpvt->asynInt32.interfaceType = asynInt32Type;
    pdpvt->asynInt32.pinterface  = &int32Methods;
    pdpvt->asynInt32.drvPvt = pdpvt;
    status = pasynInt32Base->initialize(pdpvt->portName, &pdpvt->asynInt32);
    if (status != asynSuccess) {
        printf("pasynInt32Base->initialize failed\n");
        return;
    }

    /*
     * Always register an interrupt source, just in case we use SRQs
     */
    pasynManager->registerInterruptSource(pdpvt->portName,
                                         &pdpvt->asynInt32,
                                         &pdpvt->asynInt32InterruptPvt);

    pdpvt->asynDrvUser.interfaceType = asynDrvUserType;
    pdpvt->asynDrvUser.pinterface = &drvUserMethods;
    pdpvt->asynDrvUser.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName, &pdpvt->asynDrvUser);
    if (status != asynSuccess) {
        printf("Can't register drvUser\n");
        return;
    }
}

/*
 * IOC shell command registration
 */
static const iocshArg usbtmcConfigureArg0 = {"port name", iocshArgString};
static const iocshArg usbtmcConfigureArg1 = {"vendor ID number", iocshArgInt};
static const iocshArg usbtmcConfigureArg2 = {"product ID number", iocshArgInt};
static const iocshArg usbtmcConfigureArg3 = {"serial string", iocshArgString};
static const iocshArg usbtmcConfigureArg4 = {"priority", iocshArgInt};
static const iocshArg usbtmcConfigureArg5 = {"flags", iocshArgInt};
static const iocshArg *usbtmcConfigureArgs[] = {&usbtmcConfigureArg0,
                                                &usbtmcConfigureArg1,
                                                &usbtmcConfigureArg2,
                                                &usbtmcConfigureArg3,
                                                &usbtmcConfigureArg4,
                                                &usbtmcConfigureArg5};
static const iocshFuncDef usbtmcConfigureFuncDef = {"usbtmcConfigure",6,usbtmcConfigureArgs};
static void usbtmcConfigureCallFunc(const iocshArgBuf *args)
{
    usbtmcConfigure (args[0].sval,
                     args[1].ival, args[2].ival, args[3].sval,
                     args[4].ival, args[5].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void usbtmcRegisterCommands (void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&usbtmcConfigureFuncDef, usbtmcConfigureCallFunc);
    }
}
epicsExportRegistrar(usbtmcRegisterCommands);
