/**********************************************************************
* Asyn device support using local serial interface on WIN32
*
* Mark Rivers
* July 26, 2011
**********************************************************************/       
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * $Id: drvAsynSerialPort.c,v 1.49 2009-08-13 20:35:31 norume Exp $
 */

#include <string.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <errno.h>
#include <windows.h>

#include <osiUnistd.h>
#include <cantProceed.h>
#include <errlog.h>
#include <iocsh.h>
#include <epicsAssert.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <osiUnistd.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynInterposeEos.h"
#include "drvAsynSerialPort.h"

/*
 * This structure holds the hardware-specific information for a single
 * asyn link.  There is one for each serial line.
 */
typedef struct {
    asynUser          *pasynUser;
    char              *serialDeviceName;
    char              *portName;
    unsigned long      nRead;
    unsigned long      nWritten;
    int                baud;
    HANDLE             commHandle;
    COMMCONFIG         commConfig;
    double             readTimeout;
    double             writeTimeout;
    epicsTimerId       timer;
    volatile int       timeoutFlag;
    asynInterface      common;
    asynInterface      option;
    asynInterface      octet;
} ttyController_t;

typedef struct serialBase {
    epicsTimerQueueId timerQueue;
} serialBase;
static serialBase *pserialBase = 0;
static void serialBaseInit(void)
{
    if(pserialBase) return;
    pserialBase = callocMustSucceed(1,sizeof(serialBase),"serialBaseInit");
    pserialBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

/*
 * asynOption methods
 */
static asynStatus
getOption(void *drvPvt, asynUser *pasynUser,
                              const char *key, char *val, int valSize)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    DWORD commConfigSize = sizeof(tty->commConfig);
    BOOL ret;
    DWORD error;
    int l;
    
    val[0] = '\0';
    assert(tty);
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    ret = GetCommConfig(tty->commHandle, &tty->commConfig, &commConfigSize);
    if (ret == 0) {
        error = GetLastError();
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "%s error calling GetCommConfig %d", tty->serialDeviceName, error);
        return asynError;
    }

    if (epicsStrCaseCmp(key, "baud") == 0) {
        l = epicsSnprintf(val, valSize, "%d", tty->commConfig.dcb.BaudRate);
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        l = epicsSnprintf(val, valSize, "%d", tty->commConfig.dcb.ByteSize);
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        switch (tty->commConfig.dcb.Parity) {
            case 0:
                l = epicsSnprintf(val, valSize, "none");
                break;
            case 1:
                l = epicsSnprintf(val, valSize, "odd");
                break;
            case 2:
                l = epicsSnprintf(val, valSize, "even");
                break;
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        l = epicsSnprintf(val, valSize, "%d",  (tty->commConfig.dcb.StopBits == 2) ? 2 : 1);
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->commConfig.dcb.fOutxDsrFlow == TRUE) ? 'N' : 'Y');
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->commConfig.dcb.fOutxCtsFlow == TRUE) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "ixon") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->commConfig.dcb.fOutX == TRUE) ? 'Y' : 'N');
    }
    else if (epicsStrCaseCmp(key, "ixany") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  'N');
    }
    else if (epicsStrCaseCmp(key, "ixoff") == 0) {
        l = epicsSnprintf(val, valSize, "%c",  (tty->commConfig.dcb.fInX == TRUE) ? 'Y' : 'N');
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
    asynPrint(tty->pasynUser, ASYN_TRACEIO_DRIVER,
              "%s getOption, key=%s, val=%s\n",
              tty->serialDeviceName, key, val);
    return asynSuccess;
}

static asynStatus
setOption(void *drvPvt, asynUser *pasynUser, const char *key, const char *val)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    DWORD commConfigSize = sizeof(tty->commConfig);
    BOOL ret;
    DWORD error;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s setOption key %s val %s\n", tty->portName, key, val);
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }

    ret = GetCommConfig(tty->commHandle, &tty->commConfig, &commConfigSize);
    if (ret == 0) {
        error = GetLastError();
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "%s error calling GetCommConfig %d", tty->serialDeviceName, error);
        return asynError;
    }

    if (epicsStrCaseCmp(key, "baud") == 0) {
        int baud;
        if(sscanf(val, "%d", &baud) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                "Bad number");
            return asynError;
        }
        tty->commConfig.dcb.BaudRate = baud;
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        int bits;
        if(sscanf(val, "%d", &bits) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                "Bad number");
            return asynError;
        }
        tty->commConfig.dcb.ByteSize = bits;
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        if (epicsStrCaseCmp(val, "none") == 0) {
            tty->commConfig.dcb.Parity = 0;
        }
        else if (epicsStrCaseCmp(val, "odd") == 0) {
            tty->commConfig.dcb.Parity = 1;
        }
        else if (epicsStrCaseCmp(val, "even") == 0) {
            tty->commConfig.dcb.Parity = 2;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                            "Invalid parity.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        if (epicsStrCaseCmp(val, "1") == 0) {
            tty->commConfig.dcb.StopBits = ONESTOPBIT;
        }
        else if (epicsStrCaseCmp(val, "2") == 0) {
            tty->commConfig.dcb.StopBits = TWOSTOPBITS;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Invalid number of stop bits.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "clocal") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->commConfig.dcb.fOutxDsrFlow = FALSE;
            tty->commConfig.dcb.fDsrSensitivity = FALSE;
            tty->commConfig.dcb.fDtrControl = DTR_CONTROL_ENABLE;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->commConfig.dcb.fOutxDsrFlow = TRUE;
            tty->commConfig.dcb.fDsrSensitivity = TRUE;
            tty->commConfig.dcb.fDtrControl = DTR_CONTROL_HANDSHAKE;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid clocal value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->commConfig.dcb.fOutxCtsFlow = TRUE;
            tty->commConfig.dcb.fRtsControl = RTS_CONTROL_HANDSHAKE;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->commConfig.dcb.fOutxCtsFlow = FALSE;
            tty->commConfig.dcb.fRtsControl = RTS_CONTROL_ENABLE;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                      "Invalid crtscts value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "ixon") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->commConfig.dcb.fOutX = TRUE  ;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->commConfig.dcb.fOutX = FALSE;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid ixon value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "ixany") == 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Option ixany not supported on Windows");
        return asynError;       
    }
    else if (epicsStrCaseCmp(key, "ixoff") == 0) {
        if (epicsStrCaseCmp(val, "Y") == 0) {
            tty->commConfig.dcb.fInX = TRUE;
        }
        else if (epicsStrCaseCmp(val, "N") == 0) {
            tty->commConfig.dcb.fInX = FALSE;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                    "Invalid ixoff value.");
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "") != 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
        return asynError;
    }
    ret = SetCommConfig(tty->commHandle, &tty->commConfig, commConfigSize);
    if (ret == 0) {
        error = GetLastError();
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "%s error calling SetCommConfig %d", tty->serialDeviceName, error);
        return asynError;
    }
    return asynSuccess;
}
static const struct asynOption asynOptionMethods = { setOption, getOption };

/*
 * Close a connection
 */
static void
closeConnection(asynUser *pasynUser,ttyController_t *tty)
{
    if (tty->commHandle != INVALID_HANDLE_VALUE) {
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                           "Close %s connection.\n", tty->serialDeviceName);
        CloseHandle(tty->commHandle);
        tty->commHandle = INVALID_HANDLE_VALUE;
        pasynManager->exceptionDisconnect(pasynUser);
    }
}

/*
 * Unblock the I/O operation
 */
static void
timeoutHandler(void *p)
{
    ttyController_t *tty = (ttyController_t *)p;

    asynPrint(tty->pasynUser, ASYN_TRACE_FLOW,
                               "%s timeout handler.\n", tty->serialDeviceName);
    tty->timeoutFlag = 1;
}


/*
 * Report link parameters
 */
static void
report(void *drvPvt, FILE *fp, int details)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    fprintf(fp, "Serial line %s: %sonnected\n",
        tty->serialDeviceName,
        tty->commHandle != INVALID_HANDLE_VALUE ? "C" : "Disc");
    if (details >= 1) {
        fprintf(fp, "            commHandle: %d\n",  tty->commHandle);
        fprintf(fp, "    Characters written: %lu\n", tty->nWritten);
        fprintf(fp, "       Characters read: %lu\n", tty->nRead);
    }
}

/*
 * Create a link
 */
static asynStatus
connectIt(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    /*
     * Sanity check
     */
    assert(tty);
    if (tty->commHandle != INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "%s: Link already open!", tty->serialDeviceName);
        return asynError;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                              "Open connection to %s\n", tty->serialDeviceName);

    /*
     * Open serial line and set configuration
     * Must open in non-blocking mode in case carrier detect is not
     * present and we plan to use the line in CLOCAL mode.
     */
    tty->commHandle = CreateFile(tty->serialDeviceName,
                                 GENERIC_READ | GENERIC_WRITE,  // access (read-write) mode
                                 0,                             // share mode
                                 0,                             // pointer to security attributes
                                 OPEN_EXISTING,                 // how to create
                                 0,                             // not overlapped I/O
                                 0                              // handle to file with attributes to copy
                                  );
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                            "%s Can't open  %s",
                                    tty->serialDeviceName, strerror(errno));
        return asynError;
    }

    /* setOption(tty, tty->pasynUser, "baud", "9600"); */
   
    /*
     * Turn off non-blocking mode
     */
    if (!FlushFileBuffers(tty->commHandle))
        return asynError;
    tty->readTimeout = -1e-99;
    tty->writeTimeout = -1e-99;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                          "Opened connection to %s\n", tty->serialDeviceName);
    pasynManager->exceptionConnect(pasynUser);
    return asynSuccess;
}

static asynStatus
disconnect(void *drvPvt, asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                                    "%s disconnect\n", tty->serialDeviceName);
    epicsTimerCancel(tty->timer);
    closeConnection(pasynUser,tty);
    return asynSuccess;
}


/*
 * Write to the serial line
 */
static asynStatus writeIt(void *drvPvt, asynUser *pasynUser,
    const char *data, size_t numchars,size_t *nbytesTransfered)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisWrite;
    int nleft = (int)numchars;
    int timerStarted = 0;
    BOOL ret;
    DWORD error;
    asynStatus status = asynSuccess;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                            "%s write.\n", tty->serialDeviceName);
    asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, numchars,
                            "%s write %d\n", tty->serialDeviceName, numchars);
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    if (numchars == 0) {
        *nbytesTransfered = 0;
        return asynSuccess;
    }
    if (tty->writeTimeout != pasynUser->timeout) {
        tty->writeTimeout = pasynUser->timeout;
    }
    tty->timeoutFlag = 0;
    nleft = (int)numchars;
    if (tty->writeTimeout > 0)
        {
        epicsTimerStartDelay(tty->timer, tty->writeTimeout);
        timerStarted = 1;
        }
    for (;;) {
        ret = WriteFile(tty->commHandle,     // handle to file to write to
                        data,     // pointer to data to write to file
                        nleft,     // number of bytes to write
                        &thisWrite, // pointer to number of bytes written
                        NULL     // pointer to structure for overlapped I/O
                        );
        if (ret == 0) {
            error = GetLastError();
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s write error: %d",
                                        tty->serialDeviceName, error);
            closeConnection(pasynUser,tty);
            status = asynError;
            break;
        }
        tty->nWritten += thisWrite;
        nleft -= thisWrite;
        if (nleft == 0)
            break;
        data += thisWrite;
        if (tty->timeoutFlag || (tty->writeTimeout == 0)) {
            status = asynTimeout;
            break;
        }
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    *nbytesTransfered = numchars - nleft;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "wrote %lu to %s, return %s\n",
                                            (unsigned long)*nbytesTransfered,
                                            tty->serialDeviceName,
                                            pasynManager->strStatus(status));
    return status;
}

/*
 * Read from the serial line
 */
static asynStatus readIt(void *drvPvt, asynUser *pasynUser,
    char *data, size_t maxchars,size_t *nbytesTransfered,int *gotEom)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    int thisRead;
    int nRead = 0;
    int timerStarted = 0;
    COMMTIMEOUTS ctimeout;
    BOOL ret;
    DWORD error;
    asynStatus status = asynSuccess;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
               "%s read.\n", tty->serialDeviceName);
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    if (maxchars <= 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s maxchars %d Why <=0?",tty->serialDeviceName,(int)maxchars);
        return asynError;
    }
    if (tty->readTimeout != pasynUser->timeout) {
        if (pasynUser->timeout >= 0) {
            if (pasynUser->timeout == 0) {
                ctimeout.ReadIntervalTimeout          = MAXDWORD; 
                ctimeout.ReadTotalTimeoutMultiplier   = 0; 
                ctimeout.ReadTotalTimeoutConstant     = 0; 
                ctimeout.WriteTotalTimeoutMultiplier  = 0; 
                ctimeout.WriteTotalTimeoutConstant    = 0;
            }
            else {
                ctimeout.ReadIntervalTimeout          = (int)(pasynUser->timeout*1000.); 
                ctimeout.ReadTotalTimeoutMultiplier   = 1; 
                ctimeout.ReadTotalTimeoutConstant     = (int)(pasynUser->timeout*1000.); 
                ctimeout.WriteTotalTimeoutMultiplier  = 1; 
                ctimeout.WriteTotalTimeoutConstant    = (int)(pasynUser->timeout*1000.);
            } 

            ret = SetCommTimeouts(tty->commHandle, &ctimeout);
            if (ret == 0) {
                error = GetLastError();
                epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                              "Can't set \"%s\" timeout: %f, error=%d",
                              tty->serialDeviceName, pasynUser->timeout, error);
                return asynError;
            }
            tty->readTimeout = pasynUser->timeout;
        }
    }
    tty->timeoutFlag = 0;
    if (gotEom) *gotEom = 0;
    for (;;) {
        if (!timerStarted && (tty->readTimeout > 0)) {
            epicsTimerStartDelay(tty->timer, tty->readTimeout);
            timerStarted = 1;
        }
        ret = ReadFile(
                        tty->commHandle,  // handle of file to read
                        data,             // pointer to buffer that receives data
                        1,                // number of bytes to read
                        &thisRead,        // pointer to number of bytes read
                        NULL              // pointer to structure for data
                        );
        if (ret == 0) {
            error = GetLastError();
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                          "%s read error: %d",
                          tty->serialDeviceName, error);
            status = asynError;
            break;
        }
        if (thisRead > 0) {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, data, thisRead,
                       "%s read %d\n", tty->serialDeviceName, thisRead);
            nRead = thisRead;
            tty->nRead += thisRead;
            break;
        }
        else {
            if (tty->readTimeout == 0)
                tty->timeoutFlag = 1;
        }
        if (tty->timeoutFlag)
            break;
        if (tty->readTimeout == 0) /* Timeout of 0 means return immediately */
            break;
    }
    if (timerStarted) epicsTimerCancel(tty->timer);
    if (tty->timeoutFlag && (status == asynSuccess))
        status = asynTimeout;
    *nbytesTransfered = nRead;
    /* If there is room add a null byte */
    if (nRead < (int)maxchars)
        data[nRead] = 0;
    else if (gotEom)
        *gotEom = ASYN_EOM_CNT;
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s read %d, return %d\n",
                            tty->serialDeviceName, *nbytesTransfered, status);
    return status;
}

/*
 * Flush pending input
 */
static asynStatus
flushIt(void *drvPvt,asynUser *pasynUser)
{
    ttyController_t *tty = (ttyController_t *)drvPvt;
    BOOL ret;
    DWORD error;

    assert(tty);
    asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", tty->serialDeviceName);
    if (tty->commHandle == INVALID_HANDLE_VALUE) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                                "%s disconnected:", tty->serialDeviceName);
        return asynError;
    }
    ret = PurgeComm(tty->commHandle, PURGE_RXCLEAR);
    if (ret == 0) {
        error = GetLastError();
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "%s flush error: %d",
                      tty->serialDeviceName, error);
        return asynError;
    }
    return asynSuccess;
}

static asynOctet asynOctetMethods = { writeIt, readIt, flushIt };

/*
 * Clean up a ttyController
 */
static void
ttyCleanup(ttyController_t *tty)
{
    if (tty) {
        if (tty->commHandle != INVALID_HANDLE_VALUE)
            CloseHandle(tty->commHandle);
        free(tty->portName);
        free(tty->serialDeviceName);
        free(tty);
    }
}

/*
 * asynCommon methods
 */
static const struct asynCommon asynCommonMethods = {
    report,
    connectIt,
    disconnect
};

/*
 * Configure and register a generic serial device
 */
epicsShareFunc int
drvAsynSerialPortConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int noAutoConnect,
                     int noProcessEos)
{
    ttyController_t *tty;
    asynStatus status;
    char winTtyName[MAX_PATH];


    /*
     * Check arguments
     */
    if (portName == NULL) {
        printf("Port name missing.\n");
        return -1;
    }
    if (ttyName == NULL) {
        printf("TTY name missing.\n");
        return -1;
    }

    if(!pserialBase) serialBaseInit();
    /*
     * Create a driver
     */
    tty = (ttyController_t *)callocMustSucceed(1, sizeof(*tty), "drvAsynSerialPortConfigure()");

    /*
     * Create timeout mechanism
     */
     tty->timer = epicsTimerQueueCreateTimer(
         pserialBase->timerQueue, timeoutHandler, tty);
     if(!tty->timer) {
        printf("drvAsynSerialPortConfigure: Can't create timer.\n");
        return -1;
    }
    tty->commHandle = INVALID_HANDLE_VALUE;
    if ( (epicsStrnCaseCmp(ttyName, "\\\\.\\", 4) != 0)) {
        /* 
         * The user did not pass a Windows device name, so prepend \\.\
         */
        epicsSnprintf(winTtyName, sizeof(winTtyName), "\\\\.\\%s", ttyName);
    } 
    else {
        strncpy(winTtyName, ttyName, sizeof(winTtyName));
    }   
    tty->serialDeviceName = epicsStrDup(winTtyName);
    tty->portName = epicsStrDup(portName);

    /*
     *  Link with higher level routines
     */
    tty->common.interfaceType = asynCommonType;
    tty->common.pinterface  = (void *)&asynCommonMethods;
    tty->common.drvPvt = tty;
    tty->option.interfaceType = asynOptionType;
    tty->option.pinterface  = (void *)&asynOptionMethods;
    tty->option.drvPvt = tty;
    if (pasynManager->registerPort(tty->portName,
                                   ASYN_CANBLOCK,
                                   !noAutoConnect,
                                   priority,
                                   0) != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register myself.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->common);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register common.\n");
        ttyCleanup(tty);
        return -1;
    }
    status = pasynManager->registerInterface(tty->portName,&tty->option);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register option.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->octet.interfaceType = asynOctetType;
    tty->octet.pinterface  = &asynOctetMethods;
    tty->octet.drvPvt = tty;
    status = pasynOctetBase->initialize(tty->portName,&tty->octet,
                             (noProcessEos ? 0 : 1),(noProcessEos ? 0 : 1),1);
    if(status != asynSuccess) {
        printf("drvAsynSerialPortConfigure: Can't register octet.\n");
        ttyCleanup(tty);
        return -1;
    }
    tty->pasynUser = pasynManager->createAsynUser(0,0);
    status = pasynManager->connectDevice(tty->pasynUser,tty->portName,-1);
    if(status != asynSuccess) {
        printf("connectDevice failed %s\n",tty->pasynUser->errorMessage);
        ttyCleanup(tty);
        return -1;
    }
    return 0;
}

/*
 * IOC shell command registration
 */
static const iocshArg drvAsynSerialPortConfigureArg0 = { "port name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg1 = { "tty name",iocshArgString};
static const iocshArg drvAsynSerialPortConfigureArg2 = { "priority",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg3 = { "disable auto-connect",iocshArgInt};
static const iocshArg drvAsynSerialPortConfigureArg4 = { "noProcessEos",iocshArgInt};
static const iocshArg *drvAsynSerialPortConfigureArgs[] = {
    &drvAsynSerialPortConfigureArg0, &drvAsynSerialPortConfigureArg1,
    &drvAsynSerialPortConfigureArg2, &drvAsynSerialPortConfigureArg3,
    &drvAsynSerialPortConfigureArg4};
static const iocshFuncDef drvAsynSerialPortConfigureFuncDef =
                      {"drvAsynSerialPortConfigure",5,drvAsynSerialPortConfigureArgs};
static void drvAsynSerialPortConfigureCallFunc(const iocshArgBuf *args)
{
    drvAsynSerialPortConfigure(args[0].sval, args[1].sval, args[2].ival,
                               args[3].ival, args[4].ival);
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void
drvAsynSerialPortRegisterCommands(void)
{
    static int firstTime = 1;
    if (firstTime) {
        iocshRegister(&drvAsynSerialPortConfigureFuncDef,drvAsynSerialPortConfigureCallFunc);
        firstTime = 0;
    }
}
epicsExportRegistrar(drvAsynSerialPortRegisterCommands);
