/* 
 * RFC 2117 support for remote serial ports
 * 
 * Author: W. Eric Norum
 * "$Date: 2011/01/12 00:13:59 $ (UTC)"
 */

/************************************************************************\
* Copyright (c) 2011 Lawrence Berkeley National Laboratory, Accelerator
* Technology Group, Engineering Division
* This code is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution.
\*************************************************************************/

#include <string.h>
#include <stdio.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsString.h>
#include <epicsTypes.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynOption.h"
#include "asynInterposeCom.h"

/*
 * TELNET special characters
 */
#define C_IAC   255             /* "Interpret As Command" */
#define C_DONT  254
#define C_DO    253
#define C_WONT  252
#define C_WILL  251
#define C_SB    250             /* Subnegotiation Begin   */
#define C_SE    240             /* Subnegotiation End     */

#define WD_TRANSMIT_BINARY  0   /* Will/Do transmit binary */

#define SB_COM_PORT_OPTION      44   /* Subnegotiation command port option */
#define CPO_SET_BAUDRATE         1   /* Comand port option set baud rate */
#define CPO_SET_DATASIZE         2   /* Comand port option set data size */
#define CPO_SET_PARITY           3   /* Comand port option set parity */
# define CPO_PARITY_NONE           1 
# define CPO_PARITY_ODD            2
# define CPO_PARITY_EVEN           3
# define CPO_PARITY_MARK           4
# define CPO_PARITY_SPACE          5
#define CPO_SET_STOPSIZE         4   /* Comand port option set stop size */
#define CPO_SET_CONTROL          5   /* Comand port option set control mode */
# define CPO_CONTROL_NOFLOW        1   /* No flow control */
# define CPO_CONTROL_IXON       2   /* XON/XOFF Flow control*/
# define CPO_CONTROL_HWFLOW        3   /* Hardware flow control */
#define CPO_SET_LINESTATE_MASK  10 /* Comand port option set linestate mask */
#define CPO_SET_MODEMSTATE_MASK 11 /* Comand port option set modemstate mask */
#define CPO_SERVER_NOTIFY_LINESTATE  106
#define CPO_SERVER_NOTIFY_MODEMSTATE 107

/*
 * Interposed layer private storage
 */
typedef struct interposePvt {
    const char    *portName;

    asynInterface  octet;          /* This asynOctet interface */
    asynOctet     *pasynOctetDrv;  /* Methods of next lower interface */
    void          *drvOctetPvt;    /* Private data of next lower interface */
    asynInterface  option;         /* This asynOption interface */
    asynOption    *pasynOptionDrv; /* Methods of next lower interface */
    void          *drvOptionPvt;   /* Private data of next lower interface */

    int            baud;           /* Serial line parameters */
    int            parity;
    int            bits;
    int            stop;
    int            flow;

    char          *xBuf;          /* Buffer for transmit IAC stuffing */ 
    size_t         xBufCapacity;
} interposePvt;

/*
 * Fetch next character from device
 * Inefficient, but 
 *    a) this is likely only happening during IOC startup
 *    b) the replies from the device are fairly short
 */
static int
nextChar(interposePvt *pinterposePvt, asynUser *pasynUser)
{
    asynOctet    *poct = pinterposePvt->pasynOctetDrv;
    char c;
    size_t        nbytes;
    int           eom;
    asynStatus    status;

    status = poct->read(pinterposePvt->drvOctetPvt, pasynUser, &c, 1, &nbytes, &eom);
    if (status != asynSuccess)
        return EOF;
    return c & 0xFF;
}

/*
 * Fetch next character and verify that it matches
 */
static int
expectChar(interposePvt *pinterposePvt, asynUser *pasynUser, int expect)
{
    int       c = nextChar(pinterposePvt, pasynUser);

    if (c == EOF)
        return 0;
    if (c != (expect & 0xFF)) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                            "Expected %#X, got %#X", expect, c);
        return 0;
    }
    return 1;
}

/*
 * asynOctet methods
 */

/*
 * Double up IAC characters.
 * We assume that memchr and memcpy are nicely optimized so we're better off
 * using them than looking at and copying the characters one at a time ourselves.
 */
static asynStatus
writeIt(void *ppvt, asynUser *pasynUser,
    const char *data, size_t numchars, size_t *nbytesTransfered)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    const char *iac;
    char *dst = pinterposePvt->xBuf;
    size_t nIAC = 0;
    asynStatus status;

    if ((iac = memchr(data, C_IAC, numchars)) != NULL) {
        size_t nLeft = numchars;
        size_t nCopy = iac - data + 1;
        for (;;) {
            size_t nNew = nCopy + 1;
            char *np;
            if (nNew > pinterposePvt->xBufCapacity) {
                /*
                 * Try to strike a balance between too many
                 * realloc calls and too much wasted space.
                 */
                size_t newSize = pinterposePvt->xBufCapacity + 1024;
                if (newSize < numchars)
                    newSize = numchars + 1024;
                if (newSize < nNew)
                    newSize = nNew + 1024;
                np = realloc(pinterposePvt->xBuf, newSize);
                if (np == NULL) {
                    epicsSnprintf(pasynUser->errorMessage,
                                  pasynUser->errorMessageSize, "Out of memory");
                    return asynError;
                }
                dst = np + (dst - pinterposePvt->xBuf);
                pinterposePvt->xBuf = np;
                pinterposePvt->xBufCapacity = newSize;
            }
            memcpy(dst, data, nCopy);
            dst += nCopy;
            if (iac != NULL) {
                *dst++ = C_IAC;
                nIAC++;
            }
            nLeft -= nCopy;
            if (nLeft == 0)
                break;
            data += nCopy;
            if ((iac = memchr(data, C_IAC, nLeft)) != NULL)
                nCopy = iac - data + 1;
            else
                nCopy = nLeft;
        }
        numchars += nIAC;
        data = pinterposePvt->xBuf;
    }
    status =  pinterposePvt->pasynOctetDrv->write(pinterposePvt->drvOctetPvt,
                                pasynUser, data, numchars, nbytesTransfered);
    if (*nbytesTransfered == numchars)
        *nbytesTransfered -= nIAC;
    return status;
}

static asynStatus
readIt(void *ppvt, asynUser *pasynUser,
    char *data, size_t maxchars, size_t *nbytesTransfered, int *eomReason)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    int eom;
    size_t nRead, nCheck;
    char *iac;
    char *base = data;
    int unstuffed = 0;
    asynStatus status;
    
    status = pinterposePvt->pasynOctetDrv->read(pinterposePvt->drvOctetPvt,
                                    pasynUser, data, maxchars, &nRead, &eom);
    if (status != asynSuccess)
        return status;
    nCheck = nRead;
    while ((iac = memchr(data, C_IAC, nCheck)) != NULL) {
        int c;
        unstuffed = 1;
        eom &= ~ASYN_EOM_CNT;
        if (iac == data + nCheck - 1) {
            c = nextChar(pinterposePvt, pasynUser);
            iac--;
        }
        else {
            c = *(iac + 1) & 0xFF;
            nRead--;
        }
        if (c != C_IAC) {
            epicsSnprintf(pasynUser->errorMessage,
                          pasynUser->errorMessageSize, "Missing IAC");
            return asynError;
        }
        nCheck -= (iac - data) + 2;
        data = iac + 1;
        if (nCheck == 0)
            break;
        memmove(data, data + 1, nCheck);
    }
    if (unstuffed)
        asynPrintIO(pasynUser, ASYN_TRACEIO_FILTER, base, nRead,
                                "nRead %d after IAC unstuffing", (int)nRead);
    if (nRead == maxchars)
        eom |= ASYN_EOM_CNT;
    *nbytesTransfered = nRead;
    if (eomReason) *eomReason = eom;
    return asynSuccess;
}

static asynStatus
flushIt(void *ppvt, asynUser *pasynUser)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->flush(pinterposePvt->drvOctetPvt, pasynUser);
}

static asynStatus
registerInterruptUser(void *ppvt, asynUser *pasynUser,
            interruptCallbackOctet callback, void *userPvt, void **registrarPvt)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->registerInterruptUser(
        pinterposePvt->drvOctetPvt,
        pasynUser, callback, userPvt, registrarPvt);
}

static asynStatus
cancelInterruptUser(void *drvPvt, asynUser *pasynUser, void *registrarPvt)
{
    interposePvt *pinterposePvt = (interposePvt *)drvPvt;

    return pinterposePvt->pasynOctetDrv->cancelInterruptUser(
        pinterposePvt->drvOctetPvt, pasynUser, registrarPvt);
}

static asynStatus
setInputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->setInputEos(pinterposePvt->drvOctetPvt,
        pasynUser, eos, eoslen);
}

static asynStatus
getInputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->getInputEos(pinterposePvt->drvOctetPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynStatus
setOutputEos(void *ppvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->setOutputEos(pinterposePvt->drvOctetPvt,
        pasynUser, eos, eoslen);
}

static asynStatus
getOutputEos(void *ppvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;

    return pinterposePvt->pasynOctetDrv->getOutputEos(pinterposePvt->drvOctetPvt,
        pasynUser, eos, eossize, eoslen);
}

static asynOctet octetMethods = {
    writeIt, readIt, flushIt,
    registerInterruptUser, cancelInterruptUser,
    setInputEos, getInputEos, setOutputEos, getOutputEos
};

/*
 * asynOption support
 */

/*
 * Two possible actions depending upon the 'command':
 *    1) Tell that server that we WILL do something and
 *       verify that it will allow us to DO so.
 *    2) Tell the server to DO something and verify that it WILL.
 */
static asynStatus
willdo(interposePvt *pinterposePvt, asynUser *pasynUser, int command, int code)
{
    char          cbuf[3];
    asynStatus    status;
    int           c;
    int           wd;
    size_t        nbytes;

    cbuf[0] = C_IAC;
    cbuf[1] = command;
    cbuf[2] = code;
    status =  pinterposePvt->pasynOctetDrv->write(pinterposePvt->drvOctetPvt,
                                                pasynUser, cbuf, 3, &nbytes);
    if (status != asynSuccess)
        return status;
    for (;;) {
        while ((c = nextChar(pinterposePvt, pasynUser)) != C_IAC) {
            if (c == EOF) return asynError;
        }
        switch (c = nextChar(pinterposePvt, pasynUser)) {
        case EOF:   return asynError;
        case C_IAC: break;
        case C_SE:  break;
 
        case C_DO:
        case C_DONT:
            wd = c;
            if ((c = nextChar(pinterposePvt, pasynUser)) == EOF)
                return asynError;
            if (c != cbuf[2]) continue;
            if (cbuf[1] == (char)C_DO) {
                epicsSnprintf(pasynUser->errorMessage,
                          pasynUser->errorMessageSize,
                          "Received response %#x in response to DO.", c);
                return asynError;
            }
            if (wd == C_DONT) {
                epicsSnprintf(pasynUser->errorMessage,
                              pasynUser->errorMessageSize,
                              "Device says DON'T %#x.", c);
                return asynError;
            }
            return asynSuccess;

        case C_WILL:
        case C_WONT:
            wd = c;
            if ((c = nextChar(pinterposePvt, pasynUser)) == EOF)
                return asynError;
            if (c != cbuf[2]) continue;
            if (cbuf[1] == (char)C_WILL) {
                epicsSnprintf(pasynUser->errorMessage,
                              pasynUser->errorMessageSize,
                              "Received response %#x in response to WILL.", c);
                return asynError;
            }
            if (wd == C_WONT) {
                epicsSnprintf(pasynUser->errorMessage,
                              pasynUser->errorMessageSize,
                              "Device says WON'T %#x.", c);
                return asynError;
            }
            return asynSuccess;

        case C_SB:
            if (nextChar(pinterposePvt, pasynUser) != SB_COM_PORT_OPTION)
                break;
            if (((c = nextChar(pinterposePvt, pasynUser))
                    != CPO_SERVER_NOTIFY_LINESTATE)
              && (c != CPO_SERVER_NOTIFY_MODEMSTATE)) {
                if (c == EOF) return asynError;
                break;
            }
            if (nextChar(pinterposePvt, pasynUser) == EOF)
                return asynError;
            break;

        default:
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "Unexpected character %#x in TELNET reply", c);
            return asynError;
        }
    }
}

/*
 * Send a COM_PORT_OPTION subcommand to the server
 */
static asynStatus
sbComPortOption(interposePvt *pinterposePvt, asynUser *pasynUser, const char *xBuf, int xLen, char *rBuf)
{
    char          cbuf[20];
    asynStatus    status;
    int           c;
    size_t        nbytes;

    cbuf[0] = C_IAC;
    cbuf[1] = C_SB;
    cbuf[2] = SB_COM_PORT_OPTION;
    memcpy(cbuf+3, xBuf, xLen);
    cbuf[3+xLen+0] = C_IAC;
    cbuf[3+xLen+1] = C_SE;
    status =  pinterposePvt->pasynOctetDrv->write(pinterposePvt->drvOctetPvt,
                                            pasynUser, cbuf, 5+xLen, &nbytes);
    if (status != asynSuccess)
        return status;
    for (;;) {
        while ((c = nextChar(pinterposePvt, pasynUser)) != C_IAC) {
            if (c == EOF)
                return asynError;
        }
        if (!expectChar(pinterposePvt, pasynUser, C_SB)
         || !expectChar(pinterposePvt, pasynUser, SB_COM_PORT_OPTION))
            return asynError;
        c = nextChar(pinterposePvt, pasynUser);
        if ((c == CPO_SERVER_NOTIFY_LINESTATE )
         || (c == CPO_SERVER_NOTIFY_MODEMSTATE)) {
            if ((nextChar(pinterposePvt, pasynUser) == EOF)
             || !expectChar(pinterposePvt, pasynUser, C_IAC)
             || !expectChar(pinterposePvt, pasynUser, C_SE))
                return asynError;
        }
        else if (c == (*xBuf + 100)) {
            while (--xLen > 0) {
                if ((c = nextChar(pinterposePvt, pasynUser)) == EOF)
                    return asynError;
                *rBuf++ = c;
            }
            if (!expectChar(pinterposePvt, pasynUser, C_IAC)
             || !expectChar(pinterposePvt, pasynUser, C_SE))
                return asynError;
            return asynSuccess;
        }
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                        "Sent COM-PORT-OPTION %d but got reply %d", *xBuf, c);
            return asynError;

        }
    }
    return asynSuccess;
}

/*
 * asynOption methods
 */
static asynStatus
setOption(void *ppvt, asynUser *pasynUser, const char *key, const char *val)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    asynStatus status;
    char xBuf[5], rBuf[4];

    if (epicsStrCaseCmp(key, "baud") == 0) {
        int b;
        epicsUInt32 baud;
        if(sscanf(val, "%d", &b) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Bad number");
            return asynError;
        }
        baud = b;
        xBuf[0] = CPO_SET_BAUDRATE;
        xBuf[1] = baud >> 24;
        xBuf[2] = baud >> 16;
        xBuf[3] = baud >> 8;
        xBuf[4] = baud;
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 5, rBuf);
        if (status == asynSuccess) {
            pinterposePvt->baud = ((rBuf[0] & 0xFF) << 24) |
                                  ((rBuf[1] & 0xFF) << 16) |
                                  ((rBuf[2] & 0xFF) <<  8) |
                                   (rBuf[3] & 0xFF);
            if (pinterposePvt->baud != baud) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                              "Tried to set %d baud, actually set %d baud.",
                                                  baud, pinterposePvt->baud);
                return asynError;
            }
        }
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        int b;
        if(sscanf(val, "%d", &b) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Bad number");
            return asynError;
        }
        xBuf[0] = CPO_SET_DATASIZE;
        xBuf[1] = b;
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf);
        if (status == asynSuccess) {
            pinterposePvt->bits = rBuf[0] & 0xFF;
            if (pinterposePvt->bits != b) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                              "Tried to set %d bits, actually set %d bits.",
                                                  b, pinterposePvt->bits);
                return asynError;
            }
        }
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        xBuf[0] = CPO_SET_PARITY;
        if      (epicsStrCaseCmp(val, "none") == 0)  xBuf[1] = CPO_PARITY_NONE;
        else if (epicsStrCaseCmp(val, "even") == 0)  xBuf[1] = CPO_PARITY_EVEN;
        else if (epicsStrCaseCmp(val, "odd") == 0)   xBuf[1] = CPO_PARITY_ODD;
        else if (epicsStrCaseCmp(val, "mark") == 0)  xBuf[1] = CPO_PARITY_MARK;
        else if (epicsStrCaseCmp(val, "space") == 0) xBuf[1] = CPO_PARITY_SPACE;
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                  "Invalid parity selection");
            return asynError;
        }
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf);
        if (status == asynSuccess) pinterposePvt->parity = rBuf[0] & 0xFF;
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        float b;
        if(sscanf(val, "%g", &b) != 1) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Bad number");
            return asynError;
        }
        if((b != 1) && (b != 2)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                      "Bad  stop bit count");
            return asynError;
        }
        xBuf[0] = CPO_SET_STOPSIZE;
        xBuf[1] = (char)b;
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf);
        if (status == asynSuccess) {
            pinterposePvt->stop = rBuf[0] & 0xFF;
            if (pinterposePvt->stop != b) {
                epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                      "Tried to set %g stop bits, actually set %d stop bits.",
                                                      b, pinterposePvt->stop);
                return asynError;
            }
        }
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        xBuf[0] = CPO_SET_CONTROL;
        if (pinterposePvt->flow == CPO_CONTROL_IXON){
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "XON/XOFF already set. Now using RTS/CTS.");
        }
        if      (epicsStrCaseCmp(val, "n") == 0) xBuf[1] = pinterposePvt->flow;
        else if (epicsStrCaseCmp(val, "y") == 0) xBuf[1] = CPO_CONTROL_HWFLOW;
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Bad  value");
            return asynError;
        }
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf);
        if (status == asynSuccess) pinterposePvt->flow = rBuf[0] & 0xFF;
    }
    else if (epicsStrCaseCmp(key, "ixon") == 0){
        xBuf[0] = CPO_SET_CONTROL;
        if (pinterposePvt->flow == CPO_CONTROL_HWFLOW){
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                             "RTS/CTS already set. Now using XON/XOFF.");
        }
        if      (epicsStrCaseCmp(val, "n") == 0) xBuf[1] = pinterposePvt->flow;
        else if (epicsStrCaseCmp(val, "y") == 0) xBuf[1] = CPO_CONTROL_IXON;
        else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                                  "Bad option value");
        }
        status = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf);
        if (status == asynSuccess) {
           pinterposePvt->flow = rBuf[0] & 0xFF;
        }
        else{
           printf("XON/XOFF not set.\n");
        }
    }
    else {
        if (pinterposePvt->pasynOptionDrv) {
            /* Call the setOption function in the underlying driver */
            status =  pinterposePvt->pasynOptionDrv->setOption(pinterposePvt->drvOptionPvt, pasynUser, key, val);
        } else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                          "Can't handle option \"%s\"", key);
            return asynError;
        }
    }
    return status;
}

static asynStatus
getOption(void *ppvt, asynUser *pasynUser, const char *key, char *val, int valSize)
{
    interposePvt *pinterposePvt = (interposePvt *)ppvt;
    int l = 0;
    asynStatus status = asynSuccess;

    if (epicsStrCaseCmp(key, "baud") == 0) {
        l = epicsSnprintf(val, valSize, "%d", pinterposePvt->baud);
    }
    else if (epicsStrCaseCmp(key, "bits") == 0) {
        l = epicsSnprintf(val, valSize, "%d", pinterposePvt->bits);
    }
    else if (epicsStrCaseCmp(key, "parity") == 0) {
        switch (pinterposePvt->parity) {
        case CPO_PARITY_NONE:  l = epicsSnprintf(val, valSize, "none");   break;
        case CPO_PARITY_EVEN:  l = epicsSnprintf(val, valSize, "even");   break;
        case CPO_PARITY_ODD:   l = epicsSnprintf(val, valSize, "odd");    break;
        case CPO_PARITY_MARK:  l = epicsSnprintf(val, valSize, "mark");   break;
        case CPO_PARITY_SPACE: l = epicsSnprintf(val, valSize, "space");  break;
        }
    }
    else if (epicsStrCaseCmp(key, "stop") == 0) {
        l = epicsSnprintf(val, valSize, "%d", pinterposePvt->stop);
    }
    else if (epicsStrCaseCmp(key, "crtscts") == 0) {
        switch (pinterposePvt->flow) {
        case CPO_CONTROL_NOFLOW:  l = epicsSnprintf(val, valSize, "N");  break;
        case CPO_CONTROL_IXON: l = epicsSnprintf(val, valSize, "N");  break;
        case CPO_CONTROL_HWFLOW:  l = epicsSnprintf(val, valSize, "Y");  break;
        default:
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                          "Unknown flow control code %#X", pinterposePvt->flow);
            return asynError;
        }
    }
    else if (epicsStrCaseCmp(key, "ixon") == 0) {
        switch (pinterposePvt->flow) {
        case CPO_CONTROL_NOFLOW:  l = epicsSnprintf(val, valSize, "N");  break;
        case CPO_CONTROL_IXON: l = epicsSnprintf(val, valSize, "Y");  break;
        case CPO_CONTROL_HWFLOW:  l = epicsSnprintf(val, valSize, "N");  break;
        default:
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                          "Unknown flow control code %#X", pinterposePvt->flow);
            return asynError;
        }
    }
    else {
        if (pinterposePvt->pasynOptionDrv) {
            /* Call the getOption function in the underlying driver */
            status =  pinterposePvt->pasynOptionDrv->getOption(pinterposePvt->drvOptionPvt, pasynUser, key, val, valSize);
        } else {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                                "Unsupported key \"%s\"", key);
            return asynError;
        }
    }
    if (l >= valSize) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Value buffer for key '%s' is too small.", key);
        return asynError;
    }
    return status;
}

static asynOption optionMethods = { setOption, getOption }; 

static asynStatus
restoreSettings(interposePvt *pinterposePvt, asynUser *pasynUser)
{ 
    asynStatus s;
    int i;
    const char *keys[] = { "baud", "bits", "parity", "stop", "crtscts", "ixon" };
    char val[20];
    char xBuf[2], rBuf[1];

    xBuf[0] = CPO_SET_MODEMSTATE_MASK;
    xBuf[1] = 0;
    if (((s = willdo(pinterposePvt, pasynUser, C_DO,   WD_TRANSMIT_BINARY))
                                                                != asynSuccess)
     || ((s = willdo(pinterposePvt, pasynUser, C_WILL, WD_TRANSMIT_BINARY))
                                                                != asynSuccess)
     || ((s = willdo(pinterposePvt, pasynUser, C_WILL, SB_COM_PORT_OPTION))
                                                                != asynSuccess)
     || ((s = sbComPortOption(pinterposePvt, pasynUser, xBuf, 2, rBuf))
                                                                != asynSuccess))
        return s;
    for (i = 0 ; i < (sizeof keys / sizeof keys[0]) ; i++) {
        s = getOption(pinterposePvt, pasynUser, keys[i], val, sizeof val);
        if (s != asynSuccess)
            return s;
        s = setOption(pinterposePvt, pasynUser, keys[i], val);
        if (s != asynSuccess)
            return s;
    }
    return asynSuccess;
}

/*
 * Restore parameters on reconnect
 */
static void
exceptionHandler(asynUser *pasynUser, asynException exception)
{
    interposePvt *pinterposePvt = (interposePvt *)pasynUser->userPvt;

    if (exception == asynExceptionConnect) {
        if (restoreSettings(pinterposePvt, pasynUser) != asynSuccess)
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                            "Unable to restore parameters for port %s: %s\n",
                              pinterposePvt->portName, pasynUser->errorMessage);
    }
}

epicsShareFunc int 
asynInterposeCOM(const char *portName)
{
    interposePvt *pinterposePvt;
    asynStatus status;
    asynInterface *poctetasynInterface;
    asynInterface *poptionasynInterface;
    asynUser *pasynUser;

    /*
     * Interpose ourselves
     */
    pinterposePvt = callocMustSucceed(1, sizeof(interposePvt), "asynInterposeCOM");
    pinterposePvt->xBuf = NULL;
    pinterposePvt->xBufCapacity = 0;
    pinterposePvt->portName = epicsStrDup(portName);
    pinterposePvt->octet.interfaceType = asynOctetType;
    pinterposePvt->octet.pinterface = &octetMethods;
    pinterposePvt->octet.drvPvt = pinterposePvt;
    status = pasynManager->interposeInterface(portName, -1,
                                              &pinterposePvt->octet,
                                              &poctetasynInterface);
    if ((status != asynSuccess) || (poctetasynInterface == NULL)) {
        printf("%s interposeInterface failed.\n", portName);
        free(pinterposePvt);
        return -1;
    }
    pinterposePvt->pasynOctetDrv = (asynOctet *)poctetasynInterface->pinterface;
    pinterposePvt->drvOctetPvt = poctetasynInterface->drvPvt;

    /*
     * Advertise our asynOption interface
     */
    pinterposePvt->option.interfaceType = asynOptionType;
    pinterposePvt->option.pinterface = &optionMethods;
    pinterposePvt->option.drvPvt = pinterposePvt;
    status = pasynManager->interposeInterface(portName, -1,
                                              &pinterposePvt->option,
                                              &poptionasynInterface);
    if (status != asynSuccess) {
        printf("%s interposeInterface failed for options.\n", portName);
        free(pinterposePvt);
        return -1;
    }
    if (poptionasynInterface != NULL) {
        printf("INFO -- asynInterposeCOM options extending and perhaps overriding those of lower interface.\n");
        pinterposePvt->pasynOptionDrv = (asynOption *)poptionasynInterface->pinterface;
        pinterposePvt->drvOptionPvt = poptionasynInterface->drvPvt;
    }

    /*
     * Set default parameters
     */
    pasynUser = pasynManager->createAsynUser(NULL, NULL);
    status = pasynManager->connectDevice(pasynUser, portName, -1);
    if (status != asynSuccess) {
        printf("Can't find port %s that I just created!\n", portName);
        return -1;
    }
    pasynUser->userPvt = pinterposePvt;
    pasynUser->timeout = 2;
    pinterposePvt->baud = 9600;
    pinterposePvt->bits = 8;
    pinterposePvt->parity = CPO_PARITY_NONE;
    pinterposePvt->stop = 1;
    pinterposePvt->flow = CPO_CONTROL_NOFLOW;
    status = pasynManager->exceptionCallbackAdd(pasynUser, exceptionHandler);
    if (status != asynSuccess) {
        printf("exceptionCallbackAdd failed\n");
        return -1;
    }
    status = pasynManager->lockPort(pasynUser);
    if (status == asynSuccess) {
        status = restoreSettings(pinterposePvt, pasynUser);
        pasynManager->unlockPort(pasynUser);
    }
    if (status != asynSuccess)
        printf("WARNING -- Can't set serial port parameters: %s\n",
                                                    pasynUser->errorMessage);
    return 0;
}