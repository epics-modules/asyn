/* asynRecord.c - Record Support Routines for asyn record */
/*
 * Based on earlier asynRecord.c and serialRecord.c.
 * See documentation for differences between those records and this one.
 *
 *      Author:    Mark Rivers
 *      Date:      3/8/2004
 *
 */
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <epicsMutex.h>
#include <callback.h>
#include <cantProceed.h>
#include <dbScan.h>
#include <alarm.h>
#include <dbDefs.h>
#include <dbEvent.h>
#include <dbAccess.h>
#include <dbFldTypes.h>
#include <menuScan.h>
#include <devSup.h>
#include <drvSup.h>
#include <errMdef.h>
#include <recSup.h>
#include <recGbl.h>
#include <special.h>
#include <epicsString.h>
#include <epicsStdio.h>

#include <epicsExport.h>
#include "asynGpibDriver.h"
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynInt32.h"
#include "asynUInt32Digital.h"
#include "asynFloat64.h"
#include "asynDrvUser.h"
#include "asynOption.h"
#include "drvAsynIPPort.h"
#define GEN_SIZE_OFFSET
#include "asynRecord.h"
#include <epicsExport.h>

#undef GEN_SIZE_OFFSET
/* These should be in a header file*/
#define NUM_BAUD_CHOICES 16
static char *baud_choices[NUM_BAUD_CHOICES] = {"Unknown",
    "300", "600", "1200", "2400", "4800",
    "9600", "19200", "38400", "57600",
    "115200", "230400", "460800", "576000", "921600", "1152000"};
#define NUM_PARITY_CHOICES 4
static char *parity_choices[NUM_PARITY_CHOICES] = {"Unknown", "none", "even", "odd"};
#define NUM_DBIT_CHOICES 5
static char *data_bit_choices[NUM_DBIT_CHOICES] = {"Unknown", "5", "6", "7", "8"};
#define NUM_SBIT_CHOICES 3
static char *stop_bit_choices[NUM_SBIT_CHOICES] = {"Unknown", "1", "2"};
#define NUM_MODEM_CHOICES 3
static char *modem_control_choices[NUM_MODEM_CHOICES] = {"Unknown", "Y", "N"};
#define NUM_FLOW_CHOICES 3
static char *flow_control_choices[NUM_FLOW_CHOICES] = {"Unknown", "N", "Y"};
#define NUM_IX_CHOICES 3
static char *ix_control_choices[NUM_IX_CHOICES] = {"Unknown", "N", "Y"};
#define NUM_DRTO_CHOICES 3
static char *drto_choices[NUM_DRTO_CHOICES] = {"Unknown", "N", "Y"};
#define OPT_SIZE 80    /* Size of buffer for setting and getting port options */
#define EOS_SIZE 10    /* Size of buffer for EOS */
#define ERR_SIZE 100    /* Size of buffer for error message */
#define QUEUE_TIMEOUT 10.0    /* Timeout for queueRequest */

/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
static long init_record(dbCommon * pasynRec, int pass);
static long process(dbCommon * pasynRec);
static long special(struct dbAddr * paddr, int after);
#define get_value NULL
static long cvt_dbaddr(struct dbAddr * paddr);
static long get_array_info(struct dbAddr * paddr, long *no_elements,
                               long *offset);
static long put_array_info(struct dbAddr * paddr, long nNew);
#define get_units NULL
static long get_precision(const struct dbAddr * paddr, long *precision);
#define get_enum_str NULL
#define get_enum_strs NULL
#define put_enum_str NULL
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double NULL
static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt);
static void monitor(asynRecord * pasynRec);
static void monitorStatus(asynRecord * pasynRec);
static asynStatus connectDevice(asynRecord * pasynRec);
static asynStatus registerInterrupts(asynRecord * pasynRec);
static asynStatus cancelInterrupts(asynRecord * pasynRec);
static void callbackInterruptOctet(void *drvPvt, asynUser *pasynUser,
                 char *data,size_t numchars, int eomReason);
static void callbackInterruptInt32(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value);
static void callbackInterruptUInt32(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value);
static void callbackInterruptFloat64(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value);
static asynStatus cancelIOInterruptScan(asynRecord *pasynRec);
static void gpibUniversalCmd(asynUser * pasynUser);
static void gpibAddressedCmd(asynUser * pasynUser);
static void asynCallbackProcess(asynUser * pasynUser);
static void asynCallbackSpecial(asynUser * pasynUser);
static void queueTimeoutCallbackProcess(asynUser * pasynUser);
static void queueTimeoutCallbackSpecial(asynUser * pasynUser);
static void exceptCallback(asynUser * pasynUser, asynException exception);
static void performIO(asynUser * pasynUser);
static void performInt32IO(asynUser * pasynUser);
static void performUInt32DigitalIO(asynUser * pasynUser);
static void performFloat64IO(asynUser * pasynUser);
static void performOctetIO(asynUser * pasynUser);
static void setOption(asynUser * pasynUser);
static void getOptions(asynUser * pasynUser);
static void setEos(asynUser * pasynUser);
static void getEos(asynUser * pasynUser);
static void reportError(asynRecord * pasynRec, asynStatus status,
            const char *pformat,...);
static void resetError(asynRecord * pasynRec);
rset asynRSET = {
    RSETNUMBER,
    report,
    initialize,
    init_record,
    process,
    special,
    get_value,
    cvt_dbaddr,
    get_array_info,
    put_array_info,
    get_units,
    get_precision,
    get_enum_str,
    get_enum_strs,
    put_enum_str,
    get_graphic_double,
    get_control_double,
get_alarm_double};
epicsExportAddress(rset, asynRSET);

typedef struct oldValues {  /* Used in monitor() and monitorStatus() */
    epicsInt32 octetiv;     /* asynOctet is valid */
    epicsInt32 optioniv;    /* asynOption is valid */
    epicsInt32 gpibiv;      /* asynGpib is valid */
    epicsInt32 i32iv;       /* asynInt32 is valid */
    epicsInt32 ui32iv;      /* asynUInt32Digital is valid */
    epicsInt32 f64iv;       /* asynFloat64 is valid */
    epicsInt32 addr;        /* asyn address */
    epicsEnum16 pcnct;      /* Port Connect/Disconnect */
    char drvinfo[40];       /* Driver info string */
    epicsInt32 reason;      /* asynUser->reason */
    epicsInt32 i32inp;      /* asynInt32 input */
    epicsInt32 i32out;      /* asynInt32 output */
    unsigned long ui32inp;  /* asynUInt32Digital input */
    unsigned long ui32out;  /* asynUInt32Digital output */
    unsigned long ui32mask; /* asynUInt32Digital mask */
    double f64inp;          /* asynFloat64 input */
    double f64out;          /* asynFloat64 output */
    epicsInt32 nowt;        /* Number of bytes to write */
    epicsInt32 nawt;        /* Number of bytes actually written */
    epicsInt32 nrrd;        /* Number of bytes to read */
    epicsInt32 nord;        /* Number of bytes read */
    epicsEnum16 eomr;       /* EOM reason */
    epicsEnum16 baud;       /* Baud rate as enum*/
    epicsInt32 lbaud;       /* Baud rate as int */
    epicsEnum16 prty;       /* Parity */
    epicsEnum16 dbit;       /* Data bits */
    epicsEnum16 sbit;       /* Stop bits */
    epicsEnum16 mctl;       /* Modem control */
    epicsEnum16 fctl;       /* Flow control */
    epicsEnum16 ixon;       /* Output XON/XOFF */
    epicsEnum16 ixoff;      /* Input XON/XOFF */
    epicsEnum16 ixany;      /* XON=any character */
    epicsEnum16 drto;       /* Disconnect on read timeout */
    char hostinfo[40];      /* IP host info */
    epicsEnum16 ucmd;       /* Universal command */
    epicsEnum16 acmd;       /* Addressed command */
    unsigned char spr;      /* Serial poll response */
    epicsInt32 tmsk;        /* Trace mask */
    epicsEnum16 tb0;        /* Trace error */
    epicsEnum16 tb1;        /* Trace IO device */
    epicsEnum16 tb2;        /* Trace IO filter */
    epicsEnum16 tb3;        /* Trace IO driver */
    epicsEnum16 tb4;        /* Trace flow */
    epicsEnum16 tb5;        /* Trace warning */
    epicsInt32 tiom;        /* Trace I/O mask */
    epicsEnum16 tib0;       /* Trace IO ASCII */
    epicsEnum16 tib1;       /* Trace IO escape */
    epicsEnum16 tib2;       /* Trace IO hex */
    epicsInt32 tsiz;        /* Trace IO truncate size */
    epicsInt32 tinm;        /* Trace Info mask */
    epicsEnum16 tinb0;      /* Trace Info time */
    epicsEnum16 tinb1;      /* Trace Info port */
    epicsEnum16 tinb2;      /* Trace Info source */
    epicsEnum16 tinb3;      /* Trace Info thread */
    char tfil[40];          /* Trace IO file */
    FILE *traceFd;          /* Trace file descriptor */
    epicsEnum16 auct;       /* Autoconnect */
    epicsEnum16 cnct;       /* Connect/Disconnect */
    epicsEnum16 enbl;       /* Enable/Disable */
    char errs[ERR_SIZE + 1];/* Error string */
}   oldValues;
#define REMEMBER_STATE(FIELD) pasynRecPvt->old.FIELD = pasynRec->FIELD
#define POST_IF_NEW(FIELD) \
    if(pasynRec->FIELD != pasynRecPvt->old.FIELD) { \
        if(interruptAccept) \
           db_post_events(pasynRec, &pasynRec->FIELD, monitor_mask); \
        pasynRecPvt->old.FIELD = pasynRec->FIELD; }
typedef enum {
    stateNoDevice, stateIdle, stateCancelIO, stateIO
}   callbackState;
typedef enum {
    callbackConnect, callbackGetOption, callbackSetOption, 
    callbackGetEos, callbackSetEos
}   callbackType;
typedef struct callbackMessage {
    callbackType callbackType;
    int fieldIndex;
} callbackMessage;
typedef struct asynRecPvt {
    CALLBACK callback;
    IOSCANPVT ioScanPvt;
    void *interruptPvt;
    epicsMutexId interruptLock;
    int gotValue;
    struct dbAddr scanAddr;
    asynRecord *prec;    /* Pointer to record */
    callbackState state;
    asynUser *pasynUser;
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynOption *pasynOption;
    void *asynOptionPvt;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    asynInt32 *pasynInt32;
    void *asynInt32Pvt;
    asynUInt32Digital *pasynUInt32;
    void *asynUInt32Pvt;
    asynFloat64 *pasynFloat64;
    void *asynFloat64Pvt;
    asynDrvUser *pasynDrvUser;
    void *asynDrvUserPvt;
    char *outbuff;
    oldValues old;
}   asynRecPvt;

/* We need to define a DSET for I/O interrupt scanning */
typedef struct asynRecordDset {
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record;
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     do_io;
} asynRecordDset;

asynRecordDset asynRecordDevice = {
    5,0,0,0,getIoIntInfo,0};
epicsExportAddress(dset, asynRecordDevice);


static long init_record(dbCommon *pRec, int pass)
{
    asynRecord *pasynRec = (asynRecord *)pRec;
    asynRecPvt *pasynRecPvt;
    asynUser *pasynUser;
    asynStatus status;
    char fieldName[100];

    if(pass != 0)  {
        if(strlen(pasynRec->port) != 0) {
            status = connectDevice(pasynRec);
            pasynRecPvt = pasynRec->dpvt;
            if(status == asynSuccess) pasynRecPvt->state = stateIdle;
        }
        return (0);
    }
    /* Allocate and initialize private structure used by this record */
    pasynRecPvt = (asynRecPvt *) callocMustSucceed(
                                         1, sizeof(asynRecPvt), "asynRecord");
    pasynRec->dpvt = pasynRecPvt;
    pasynRecPvt->prec = pasynRec;
    /* Allocate the space for the binary/hybrid output and input arrays */
    if(pasynRec->omax <= 0) pasynRec->omax = MAX_STRING_SIZE;
    if(pasynRec->imax <= 0) pasynRec->imax = MAX_STRING_SIZE;
    pasynRec->optr = (char *) callocMustSucceed(
                                 pasynRec->omax, sizeof(char), "asynRecord");
    pasynRec->iptr = (char *) callocMustSucceed(
                                 pasynRec->imax, sizeof(char), "asynRecord");
    pasynRecPvt->outbuff = (char *) callocMustSucceed(
                                 pasynRec->omax, sizeof(char), "asynRecord");
    pasynRec->errs = (char *) callocMustSucceed(
                                   ERR_SIZE + 1, sizeof(char), "asynRecord");
    pasynRec->udf = 0;
    recGblResetAlarms(pasynRec);    /* let initial state be no alarm */
    strcpy(pasynRec->tfil, "Unknown");
    /* Initialize asyn, connect to device */
    pasynUser = pasynManager->createAsynUser(
                     asynCallbackProcess, queueTimeoutCallbackProcess);
    pasynUser->timeout = 1;
    pasynUser->userPvt = pasynRecPvt;
    pasynRecPvt->pasynUser = pasynUser;
    pasynRecPvt->state = stateNoDevice;
    pasynRecPvt->interruptLock = epicsMutexCreate();
    /* Get the dbaddr field of this record's SCAN field */
    strcpy(fieldName, pasynRec->name);
    strcat(fieldName, ".SCAN");
    dbNameToAddr(fieldName, &pasynRecPvt->scanAddr);
    scanIoInit(&pasynRecPvt->ioScanPvt);
    return (0);
}

static long process(dbCommon *pRec)
{
    asynRecord *pasynRec = (asynRecord *)pRec;
    asynRecPvt    *pasynRecPvt = pasynRec->dpvt;
    callbackState state = pasynRecPvt->state;
    asynStatus    status;
    int           yesNo;

    if(!pasynRec->pact) {
        if(state == stateIdle) {
            /* Need to store state of fields that could have been changed from
             * outside since the last time the values were posted by monitor()
             * and which process() or routines it calls can also change */
            REMEMBER_STATE(ucmd);
            REMEMBER_STATE(acmd);
            REMEMBER_STATE(nowt);
            REMEMBER_STATE(nrrd);
            resetError(pasynRec);
            /* If we got value from interrupt no need to read */
            if(pasynRecPvt->gotValue) goto done;  
            status = pasynManager->queueRequest(pasynRecPvt->pasynUser,
                                    asynQueuePriorityLow, QUEUE_TIMEOUT);
            if(status==asynSuccess) {
                yesNo = 0;
                pasynManager->canBlock(pasynRecPvt->pasynUser,&yesNo);
                if(yesNo) {
                    pasynRecPvt->state = state = stateIO;
                    pasynRec->pact = TRUE;
                    return 0;
                } else {
                    goto done;
                }
            }
            reportError(pasynRec, asynSuccess,"queueRequest failed");
        } else if(state==stateNoDevice){
            reportError(pasynRec, asynSuccess,"Not connect to a port");
        } else {
            reportError(pasynRec, asynSuccess,"Special is active");
        }
        recGblSetSevr(pasynRec,STATE_ALARM,MINOR_ALARM);
    } else {
        pasynRecPvt->state = stateIdle;
    }
done:
    recGblGetTimeStamp(pasynRec);
    monitor(pasynRec);
    recGblFwdLink(pasynRec);
    pasynRec->pact = FALSE;
    pasynRecPvt->gotValue = 0;
    return (0);
}

static long special(struct dbAddr * paddr, int after)
{
    asynRecord *pasynRec = (asynRecord *) paddr->precord;
    int        fieldIndex = dbGetFieldIndex(paddr);
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    asynUser *pasynUserSpecial;
    callbackMessage *pmsg;
    asynStatus status = asynSuccess;
    int        traceMask;
    FILE       *fd;
    asynQueuePriority priority;

    if(!after) {
        return 0;
    }
    resetError(pasynRec);
    /* The first set of fields can be handled even if state != stateIdle */
    switch (fieldIndex) {
    case asynRecordAQR:
        {
            int wasQueued = 0;
            status = pasynManager->cancelRequest(pasynUser,&wasQueued);
            if(wasQueued) {
                reportError(pasynRec,status, "I/O request canceled");
                recGblSetSevr(pasynRec,STATE_ALARM,MAJOR_ALARM);
                asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s:special cancelRequest\n",pasynRec->name);
                callbackRequestProcessCallback(&pasynRecPvt->callback,
                    pasynRec->prio, pasynRec);
            }
            pasynRecPvt->state = stateIdle;

        }
        return 0;
    case asynRecordTMSK:
        pasynTrace->setTraceMask(pasynUser, pasynRec->tmsk);
        return 0;
    case asynRecordTB0:
    case asynRecordTB1:
    case asynRecordTB2:
    case asynRecordTB3:
    case asynRecordTB4:
    case asynRecordTB5:
        traceMask = (pasynRec->tb0 ? ASYN_TRACE_ERROR : 0) |
                    (pasynRec->tb1 ? ASYN_TRACEIO_DEVICE : 0) |
                    (pasynRec->tb2 ? ASYN_TRACEIO_FILTER : 0) |
                    (pasynRec->tb3 ? ASYN_TRACEIO_DRIVER : 0) |
                    (pasynRec->tb4 ? ASYN_TRACE_FLOW : 0) |
                    (pasynRec->tb5 ? ASYN_TRACE_WARNING : 0);
        pasynTrace->setTraceMask(pasynUser, traceMask);
        return 0;
    case asynRecordTIOM:
        pasynTrace->setTraceIOMask(pasynUser, pasynRec->tiom);
        return 0;
    case asynRecordTIB0:
    case asynRecordTIB1:
    case asynRecordTIB2:
        traceMask = (pasynRec->tib0 ? ASYN_TRACEIO_ASCII : 0) |
                    (pasynRec->tib1 ? ASYN_TRACEIO_ESCAPE : 0) |
                    (pasynRec->tib2 ? ASYN_TRACEIO_HEX : 0);
        pasynTrace->setTraceIOMask(pasynUser, traceMask);
        return 0;
    case asynRecordTINM:
        pasynTrace->setTraceInfoMask(pasynUser, pasynRec->tinm);
        return 0;
    case asynRecordTINB0:
    case asynRecordTINB1:
    case asynRecordTINB2:
    case asynRecordTINB3:
        traceMask = (pasynRec->tinb0 ? ASYN_TRACEINFO_TIME : 0) |
                    (pasynRec->tinb1 ? ASYN_TRACEINFO_PORT : 0) |
                    (pasynRec->tinb2 ? ASYN_TRACEINFO_SOURCE : 0) |
                    (pasynRec->tinb3 ? ASYN_TRACEINFO_THREAD : 0);
        pasynTrace->setTraceInfoMask(pasynUser, traceMask);
        return 0;
    case asynRecordTSIZ:
        pasynTrace->setTraceIOTruncateSize(pasynUser, pasynRec->tsiz);
        return 0;
    case asynRecordTFIL:
        if(strlen(pasynRec->tfil)==0) {
            fd = stdout;
        } else if(strcmp(pasynRec->tfil,"<errlog>")==0) {
            fd = 0;
        } else if(strcmp(pasynRec->tfil,"<stdout>")==0) {
            fd = stdout;
        } else if(strcmp(pasynRec->tfil,"<stderr>")==0) {
            fd = stderr;
        } else {
            fd = fopen(pasynRec->tfil, "a+");
            if(!fd) {
                reportError(pasynRec, status,
                    "Error opening trace file: %s", pasynRec->tfil);
                return 0;
            }
        }
        /* Set old value to this, so monitorStatus won't think another
         * thread changed it */
        /* Must do this before calling setTraceFile, because it generates
         * an exeception, which calls monitorStatus() */
        pasynRecPvt->old.traceFd = fd;
        status = pasynTrace->setTraceFile(pasynUser, fd);
        if(status != asynSuccess) {
            reportError(pasynRec, status,
                "Error setting trace file: %s", pasynUser->errorMessage);
        }
        return 0;
    case asynRecordAUCT:
        pasynManager->autoConnect(pasynUser, pasynRec->auct);
        return 0;
    case asynRecordENBL:
        pasynManager->enable(pasynUser, pasynRec->enbl);
        return 0;
    case asynRecordREASON:
        pasynUser->reason = pasynRec->reason;
        strcpy(pasynRec->drvinfo, "");
        cancelIOInterruptScan(pasynRec);
        monitorStatus(pasynRec);
        return 0;
    case asynRecordIFACE:
        cancelIOInterruptScan(pasynRec);
        return 0;
    case asynRecordUI32MASK:
        cancelIOInterruptScan(pasynRec);
        return 0;
    default:
        break; /*handle other cases below*/
    }
    if(fieldIndex == asynRecordPORT || 
       fieldIndex == asynRecordADDR ||
       fieldIndex == asynRecordDRVINFO) {
        status = connectDevice(pasynRec);
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
                  "%s: special() port=%s, addr=%d, drvinfo=%s, connect status=%d\n",
                  pasynRec->name, pasynRec->port, pasynRec->addr, pasynRec->drvinfo,
                  status);
        if(status == asynSuccess) {
            pasynRecPvt->state = stateIdle;;
        } else {
            pasynRecPvt->state = stateNoDevice;
            reportError(pasynRec, asynSuccess,
                "connectDevice failed: %s", pasynUser->errorMessage);
        }
        return 0;
    }
    if(fieldIndex == asynRecordPCNCT) { 
        if (pasynRec->pcnct) {
            status = connectDevice(pasynRec);
        } else {
            pasynManager->exceptionCallbackRemove(pasynUser);
            pasynManager->disconnect(pasynUser);
            cancelIOInterruptScan(pasynRec);
        }
        return 0;
    }
            
    /* remaining cases must be handled by asynCallbackSpecial*/
    pasynUserSpecial = pasynManager->duplicateAsynUser(pasynUser,
                                                asynCallbackSpecial, 
                                                queueTimeoutCallbackSpecial);
    pmsg = pasynUserSpecial->userData = 
                (callbackMessage *)pasynManager->memMalloc(sizeof(*pmsg));
    switch (fieldIndex) {
    case asynRecordCNCT:
        pmsg->callbackType = callbackConnect;
        break;
    case asynRecordBAUD:
    case asynRecordLBAUD:
    case asynRecordPRTY:
    case asynRecordDBIT:
    case asynRecordSBIT:
    case asynRecordMCTL:
    case asynRecordFCTL:
    case asynRecordIXON:
    case asynRecordIXOFF:
    case asynRecordIXANY:
    case asynRecordDRTO:
    case asynRecordHOSTINFO:
        pmsg->fieldIndex = fieldIndex;
        pmsg->callbackType = callbackSetOption;
        break;
    case asynRecordIEOS:
    case asynRecordOEOS:
        pmsg->fieldIndex = fieldIndex;
        pmsg->callbackType = callbackSetEos;
        break;
    }
    if(pmsg->callbackType == callbackConnect) {
        priority = asynQueuePriorityConnect;
    } else {
        priority = asynQueuePriorityLow;
    }
    if (fieldIndex == asynRecordHOSTINFO) {
        /* Enable changing host:port when not connected */
        priority = asynQueuePriorityConnect;
        pasynUserSpecial->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
    }
    status = pasynManager->queueRequest(pasynUserSpecial,
                                        priority,QUEUE_TIMEOUT);
    if(status!=asynSuccess) {
        reportError(pasynRec,status,"queueRequest failed for special.");
        reportError(pasynRec,status,pasynUserSpecial->errorMessage);
        pasynManager->memFree(pmsg, sizeof(*pmsg));
        pasynManager->freeAsynUser(pasynUserSpecial);
    }
    return 0;
}

static long getIoIntInfo(int cmd, dbCommon *pr, IOSCANPVT *iopvt)
{
    asynRecord *pasynRec = (asynRecord *) pr;
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    asynStatus status;

    if (cmd == 0) {
        pasynRecPvt->gotValue = 0;  /* Clear the new data flag */
        status = registerInterrupts(pasynRec);
        if (status != asynSuccess) return(-1);
    } else {
        status = cancelInterrupts(pasynRec);
    }
    *iopvt = pasynRecPvt->ioScanPvt;
    return 0;
}

static asynStatus registerInterrupts(asynRecord *pasynRec)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    asynStatus status = asynError;

    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACE_FLOW,
        "%s getIoIntInfo registering interrupt\n",
        pasynRec->name);
    /* Add to scan list.  Register interrupts */
    switch(pasynRec->iface) {
    case asynINTERFACE_OCTET:
        if (pasynRec->octetiv) {
           status = pasynRecPvt->pasynOctet->registerInterruptUser(
                        pasynRecPvt->asynOctetPvt, pasynUser,
                        callbackInterruptOctet,pasynRecPvt,
                        &pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynOctet interface");
        }
        break;
    case asynINTERFACE_INT32:
        if (pasynRec->i32iv) {
           status = pasynRecPvt->pasynInt32->registerInterruptUser(
                        pasynRecPvt->asynInt32Pvt, pasynUser,
                        callbackInterruptInt32,pasynRecPvt,
                        &pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynInt32 interface");
        }
        break;
    case asynINTERFACE_UINT32:
        if (pasynRec->ui32iv) {
           status = pasynRecPvt->pasynUInt32->registerInterruptUser(
                        pasynRecPvt->asynUInt32Pvt, pasynUser,
                        callbackInterruptUInt32,pasynRecPvt,
                        pasynRec->ui32mask, &pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynUInt32Digital interface");
        }
        break;
    case asynINTERFACE_FLOAT64:
        if (pasynRec->f64iv) {
           status = pasynRecPvt->pasynFloat64->registerInterruptUser(
                        pasynRecPvt->asynFloat64Pvt, pasynUser,
                        callbackInterruptFloat64,pasynRecPvt,
                        &pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynFloat64 interface");
        }
        break;
    }
    if(status!=asynSuccess) {
        printf("%s registerInterrupts %s\n",
               pasynRec->name, pasynUser->errorMessage);
    }
    return(status);
}

static asynStatus cancelInterrupts(asynRecord *pasynRec)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    asynStatus status = asynError;

    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACE_FLOW,
        "%s getIoIntInfo cancelling interrupt\n",
        pasynRec->name);

    switch(pasynRec->iface) {
    case asynINTERFACE_OCTET:
        if (pasynRec->octetiv) {
            status = pasynRecPvt->pasynOctet->cancelInterruptUser(
                pasynRecPvt->asynOctetPvt,pasynUser,pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynOctet interface");
        }
        break;
    case asynINTERFACE_INT32:
        if (pasynRec->i32iv) {
            status = pasynRecPvt->pasynInt32->cancelInterruptUser(
                pasynRecPvt->asynInt32Pvt,pasynUser,pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynInt32 interface");
        }
        break;
    case asynINTERFACE_UINT32:
        if (pasynRec->ui32iv) {
            status = pasynRecPvt->pasynUInt32->cancelInterruptUser(
                pasynRecPvt->asynUInt32Pvt,pasynUser,pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynUInt32Digital interface");
        }
        break;
    case asynINTERFACE_FLOAT64:
        if (pasynRec->f64iv) {
            status = pasynRecPvt->pasynFloat64->cancelInterruptUser(
                pasynRecPvt->asynFloat64Pvt,pasynUser,pasynRecPvt->interruptPvt);
        } else {
            reportError(pasynRec, asynError, "No asynFloat64 interface");
        }
        break;
    }
    if(status!=asynSuccess) {
        printf("%s cancelInterrupts %s\n",
               pasynRec->name, pasynUser->errorMessage);
    }
    return(status);
}

static void callbackInterruptOctet(void *drvPvt, asynUser *pasynUser,
                 char *data,size_t numchars, int eomReason)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)drvPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;

    /* If the IOC has not finished initializing we must not call scanIoRequest */
    if (!interruptAccept) return;
    /* If gotValue==1 then the record has not yet finished processing
     * the previous interrupt, just return */
    if (pasynRecPvt->gotValue == 1) return;
    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s callbackInterruptOctet new value=%s numchars %lu eomReason %d\n",
        pasynRec->name, data, (unsigned long)numchars, eomReason);
    epicsMutexLock(pasynRecPvt->interruptLock);
    pasynRecPvt->gotValue = 1;
    epicsStrSnPrintEscaped(pasynRec->tinp,sizeof(pasynRec->tinp),
        data,numchars);
    epicsMutexUnlock(pasynRecPvt->interruptLock);
    scanIoRequest(pasynRecPvt->ioScanPvt);
}

static void callbackInterruptInt32(void *drvPvt, asynUser *pasynUser,
                epicsInt32 value)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)drvPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;

    /* If the IOC has not finished initializing we must not call scanIoRequest */
    if (!interruptAccept) return;
    /* If gotValue==1 then the record has not yet finished processing
     * the previous interrupt, just return */
    if (pasynRecPvt->gotValue == 1) return;
    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s callbackInterruptInt32 new value=%d\n",
        pasynRec->name, value);
    epicsMutexLock(pasynRecPvt->interruptLock);
    pasynRecPvt->gotValue = 1;
    pasynRec->i32inp = value;
    epicsMutexUnlock(pasynRecPvt->interruptLock);
    scanIoRequest(pasynRecPvt->ioScanPvt);
}

static void callbackInterruptUInt32(void *drvPvt, asynUser *pasynUser,
                epicsUInt32 value)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)drvPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;

    /* If the IOC has not finished initializing we must not call scanIoRequest */
    if (!interruptAccept) return;
    /* If gotValue==1 then the record has not yet finished processing
     * the previous interrupt, just return */
    if (pasynRecPvt->gotValue == 1) return;
    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s callbackInterruptUInt32 new value=%x\n",
        pasynRec->name, value);
    epicsMutexLock(pasynRecPvt->interruptLock);
    pasynRecPvt->gotValue = 1;
    pasynRec->ui32inp = value;
    epicsMutexUnlock(pasynRecPvt->interruptLock);
    scanIoRequest(pasynRecPvt->ioScanPvt);
}

static void callbackInterruptFloat64(void *drvPvt, asynUser *pasynUser,
                epicsFloat64 value)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)drvPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;

    /* If the IOC has not finished initializing we must not call scanIoRequest */
    if (!interruptAccept) return;
    /* If gotValue==1 then the record has not yet finished processing
     * the previous interrupt, just return */
    if (pasynRecPvt->gotValue == 1) return;
    asynPrint(pasynRecPvt->pasynUser, ASYN_TRACEIO_DEVICE,
        "%s callbackInterruptFloat64 new value=%f\n",
        pasynRec->name, value);
    epicsMutexLock(pasynRecPvt->interruptLock);
    pasynRecPvt->gotValue = 1;
    pasynRec->f64inp = value;
    epicsMutexUnlock(pasynRecPvt->interruptLock);
    scanIoRequest(pasynRecPvt->ioScanPvt);
}

static asynStatus cancelIOInterruptScan(asynRecord *pasynRec)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *)pasynRec->dpvt;
    int passiveScan = menuScanPassive;
    asynStatus status=asynSuccess;

    if (pasynRec->scan != menuScanI_O_Intr) return(status);
    /* Must not call dbPutField before interruptAccept */
    if (!interruptAccept) return status;
    /* Change to passive */
    dbPutField(&pasynRecPvt->scanAddr,DBR_LONG,&passiveScan,1);
    return(asynSuccess);
}

static void asynCallbackProcess(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    int        yesNo = 0;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s: asynCallbackProcess, state=%d\n",
              pasynRec->name, pasynRecPvt->state);
    resetError(pasynRec);
    pasynUser->timeout = pasynRec->tmot;
    if(pasynRec->ucmd != gpibUCMD_None) {
        gpibUniversalCmd(pasynUser);
        pasynRec->ucmd = gpibUCMD_None;
    }
    else if(pasynRec->acmd != gpibACMD_None) {
        gpibAddressedCmd(pasynUser);
        pasynRec->acmd = gpibUCMD_None;
    }
    else if(pasynRec->tmod != asynTMOD_NoIO) performIO(pasynUser);
    yesNo = 0;
    pasynManager->canBlock(pasynUser,&yesNo);
    if(yesNo) callbackRequestProcessCallback(&pasynRecPvt->callback,
                    pasynRec->prio, pasynRec);
}

static void asynCallbackSpecial(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    callbackMessage *pmsg = (callbackMessage *)pasynUser->userData;
    callbackType callbackType = pmsg->callbackType;
    asynStatus status=asynSuccess;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s: asynCallbackSpecial, type=%d\n",
              pasynRec->name, pmsg->callbackType);
    switch (callbackType) {
    case callbackSetOption:
        setOption(pasynUser);
        /* no break - every time an option is set call getOptions to verify */
    case callbackGetOption:
        getOptions(pasynUser);
        break;
    case callbackSetEos:
        setEos(pasynUser);
        /* no break - every time an option is set call getEos to verify */
    case callbackGetEos:
        getEos(pasynUser);
        break;
    case callbackConnect:{
            int isConnected;
            status = pasynManager->isConnected(pasynUser, &isConnected);
            if(status!=asynSuccess) {
                reportError(pasynRec, asynError,
                    "asynCallbackSpecial isConnected error");
                break;
            }
            if(pasynRec->cnct) {
                if(!isConnected) {
                    status = pasynRecPvt->pasynCommon->connect(
                                      pasynRecPvt->asynCommonPvt, pasynUser);
                    if (status!=asynSuccess) {
                        reportError(pasynRec, asynError,
                            "asynCallbackSpecial callbackConnect connect: %s",
                                                    pasynUser->errorMessage);

                        break;
                    }
                } else {
                    monitorStatus(pasynRec);
                }
            } else {
                if(isConnected) {
                    status = pasynRecPvt->pasynCommon->disconnect(
                                      pasynRecPvt->asynCommonPvt, pasynUser);
                    if (status!=asynSuccess) {
                        reportError(pasynRec, asynError,
                            "asynCallbackSpecial callbackConnect disconnect: %s",
                                                    pasynUser->errorMessage);

                        break;
                    }
                } else {
                    monitorStatus(pasynRec);
                }
            }
        }
        break;
    default:
        reportError(pasynRec, asynError,
            "asynCallbackSpecial illegal type %d\n", callbackType);
        status = asynError;
    }
    pasynManager->memFree(pmsg, sizeof(*pmsg));
    pasynManager->freeAsynUser(pasynUser);
    if (status == asynSuccess) pasynRecPvt->state = stateIdle;
}

static void exceptCallback(asynUser * pasynUser, asynException exception)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    int callLock = interruptAccept;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s: exception %d, %s\n",
              pasynRec->name, (int) exception, asynExceptionToString(exception) );
    if(callLock)
        dbScanLock((dbCommon *) pasynRec);
    /* There has been a change in connect or enable status */
    monitorStatus(pasynRec);
    if(callLock)
        dbScanUnlock((dbCommon *) pasynRec);
}

static void queueTimeoutCallbackProcess(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    reportError(pasynRec, asynError, "process queueRequest timeout");
    recGblSetSevr(pasynRec,STATE_ALARM,MAJOR_ALARM);
    callbackRequestProcessCallback(&pasynRecPvt->callback,
                    pasynRec->prio, pasynRec);
}

static void queueTimeoutCallbackSpecial(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    callbackMessage *pmsg = (callbackMessage *)pasynUser->userData;
    reportError(pasynRec, asynError, "special queueRequest timeout");
    pasynRecPvt->state = stateIdle;
    pasynManager->memFree(pmsg, sizeof(*pmsg));
    pasynManager->freeAsynUser(pasynUser);
}

static long cvt_dbaddr(struct dbAddr * paddr)
{
    asynRecord *pasynRec = (asynRecord *) paddr->precord;
    int fieldIndex = dbGetFieldIndex(paddr);
    if(fieldIndex == asynRecordBOUT) {
        paddr->pfield = (void *) (pasynRec->optr);
        paddr->no_elements = pasynRec->omax;
        paddr->field_type = DBF_CHAR;
        paddr->field_size = sizeof(char);
        paddr->dbr_field_type = DBF_CHAR;
    } else if(fieldIndex == asynRecordBINP) {
        paddr->pfield = (unsigned char *) (pasynRec->iptr);
        paddr->no_elements = pasynRec->imax;
        paddr->field_type = DBF_CHAR;
        paddr->field_size = sizeof(char);
        paddr->dbr_field_type = DBF_CHAR;
    } else if(fieldIndex == asynRecordERRS) {
        paddr->pfield = pasynRec->errs;
        paddr->no_elements = ERR_SIZE;
        paddr->field_type = DBF_CHAR;
        paddr->field_size = sizeof(char);
        paddr->dbr_field_type = DBR_CHAR;
        paddr->special = SPC_NOMOD;
    }
    return (0);
}
static long get_array_info(struct dbAddr * paddr, long *no_elements,
                               long *offset)
{
    asynRecord *pasynRec = (asynRecord *) paddr->precord;
    int fieldIndex = dbGetFieldIndex(paddr);
    if(fieldIndex == asynRecordBOUT) {
        *no_elements = pasynRec->nowt;
        *offset = 0;
    } else if(fieldIndex == asynRecordBINP) {
        *no_elements = pasynRec->nord;
        *offset = 0;
    } else if(fieldIndex == asynRecordERRS) {
        *no_elements = ERR_SIZE;
        *offset = 0;
    }
    return (0);
}
static long put_array_info(struct dbAddr * paddr, long nNew)
{
    asynRecord *pasynRec = (asynRecord *) paddr->precord;
    int fieldIndex = dbGetFieldIndex(paddr);
    if(fieldIndex == asynRecordBOUT) {
        pasynRec->nowt = nNew;
    } else if(fieldIndex == asynRecordBINP) {
        pasynRec->nord = nNew;
    }
    return (0);
}
static long get_precision(const struct dbAddr * paddr, long *precision)
{
    int fieldIndex = dbGetFieldIndex(paddr);
    *precision = 0;
    if(fieldIndex == asynRecordTMOT) {
        *precision = 4;
        return (0);
    }
    recGblGetPrec(paddr, precision);
    return (0);
}

static void monitor(asynRecord * pasynRec)
{
    /* Called when record processes for I/O operation */
    unsigned short monitor_mask;
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
    if((pasynRec->tmod == asynTMOD_Read) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        if(pasynRec->ifmt == asynFMT_ASCII)
            db_post_events(pasynRec, pasynRec->ainp, monitor_mask);
        else
            db_post_events(pasynRec, pasynRec->iptr, monitor_mask);
        db_post_events(pasynRec, pasynRec->tinp, monitor_mask);
    }
    POST_IF_NEW(nrrd);
    POST_IF_NEW(nord);
    POST_IF_NEW(nowt);
    POST_IF_NEW(nawt);
    POST_IF_NEW(spr);
    POST_IF_NEW(ucmd);
    POST_IF_NEW(acmd);
    POST_IF_NEW(eomr);
    POST_IF_NEW(i32inp);
    POST_IF_NEW(ui32inp);
    POST_IF_NEW(f64inp);
}

static void monitorStatus(asynRecord * pasynRec)
{
    /* Called to update trace and connect fields. */
    unsigned short monitor_mask;
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    int traceMask;
    asynStatus status;
    FILE *traceFd;
    int yesNo;
    /* For fields that could have been changed externally we need to remember
     * their current value */
    REMEMBER_STATE(tmsk);
    REMEMBER_STATE(tb0);
    REMEMBER_STATE(tb1);
    REMEMBER_STATE(tb2);
    REMEMBER_STATE(tb3);
    REMEMBER_STATE(tb4);
    REMEMBER_STATE(tb5);
    REMEMBER_STATE(tiom);
    REMEMBER_STATE(tib0);
    REMEMBER_STATE(tib1);
    REMEMBER_STATE(tib2);
    REMEMBER_STATE(tinm);
    REMEMBER_STATE(tinb0);
    REMEMBER_STATE(tinb1);
    REMEMBER_STATE(tinb2);
    REMEMBER_STATE(tinb3);
    REMEMBER_STATE(tsiz);
    REMEMBER_STATE(auct);
    REMEMBER_STATE(cnct);
    REMEMBER_STATE(enbl);
    monitor_mask = DBE_VALUE | DBE_LOG;
    traceMask = pasynTrace->getTraceMask(pasynUser);
    pasynRec->tmsk = traceMask;
    pasynRec->tb0 = (traceMask & ASYN_TRACE_ERROR) ? 1 : 0;
    pasynRec->tb1 = (traceMask & ASYN_TRACEIO_DEVICE) ? 1 : 0;
    pasynRec->tb2 = (traceMask & ASYN_TRACEIO_FILTER) ? 1 : 0;
    pasynRec->tb3 = (traceMask & ASYN_TRACEIO_DRIVER) ? 1 : 0;
    pasynRec->tb4 = (traceMask & ASYN_TRACE_FLOW) ? 1 : 0;
    pasynRec->tb5 = (traceMask & ASYN_TRACE_WARNING) ? 1 : 0;
    traceMask = pasynTrace->getTraceIOMask(pasynUser);
    pasynRec->tiom = traceMask;
    pasynRec->tib0 = (traceMask & ASYN_TRACEIO_ASCII) ? 1 : 0;
    pasynRec->tib1 = (traceMask & ASYN_TRACEIO_ESCAPE) ? 1 : 0;
    pasynRec->tib2 = (traceMask & ASYN_TRACEIO_HEX) ? 1 : 0;
    traceMask = pasynTrace->getTraceInfoMask(pasynUser);
    pasynRec->tinm = traceMask;
    pasynRec->tinb0 = (traceMask & ASYN_TRACEINFO_TIME) ? 1 : 0;
    pasynRec->tinb1 = (traceMask & ASYN_TRACEINFO_PORT) ? 1 : 0;
    pasynRec->tinb2 = (traceMask & ASYN_TRACEINFO_SOURCE) ? 1 : 0;
    pasynRec->tinb3 = (traceMask & ASYN_TRACEINFO_THREAD) ? 1 : 0;
    status = pasynManager->isAutoConnect(pasynUser, &yesNo);
    if(status == asynSuccess)
        pasynRec->auct = yesNo;
    else
        pasynRec->auct = 0;
    status = pasynManager->isConnected(pasynUser, &yesNo);
    if(status == asynSuccess)
        pasynRec->cnct = yesNo;
    else
        pasynRec->cnct = 0;
    status = pasynManager->isEnabled(pasynUser, &yesNo);
    if(status == asynSuccess)
        pasynRec->enbl = yesNo;
    else
        pasynRec->enbl = 0;
    pasynRec->tsiz = (int)pasynTrace->getTraceIOTruncateSize(pasynUser);
    traceFd = pasynTrace->getTraceFile(pasynUser);
    POST_IF_NEW(tmsk);
    POST_IF_NEW(tb0);
    POST_IF_NEW(tb1);
    POST_IF_NEW(tb2);
    POST_IF_NEW(tb3);
    POST_IF_NEW(tb4);
    POST_IF_NEW(tb5);
    POST_IF_NEW(tiom);
    POST_IF_NEW(tib0);
    POST_IF_NEW(tib1);
    POST_IF_NEW(tib2);
    POST_IF_NEW(tinm);
    POST_IF_NEW(tinb0);
    POST_IF_NEW(tinb1);
    POST_IF_NEW(tinb2);
    POST_IF_NEW(tinb3);
    POST_IF_NEW(tsiz);
    if(traceFd != pasynRecPvt->old.traceFd) {
        pasynRecPvt->old.traceFd = traceFd;
        /* Some other thread changed the trace file, we can't know file name */
        strcpy(pasynRec->tfil, "Unknown");
        db_post_events(pasynRec, pasynRec->tfil, monitor_mask);
    }
    POST_IF_NEW(auct);
    POST_IF_NEW(cnct);
    POST_IF_NEW(pcnct);
    POST_IF_NEW(reason);
    if (strcmp(pasynRec->drvinfo, pasynRecPvt->old.drvinfo) != 0) {
        strcpy(pasynRecPvt->old.drvinfo, pasynRec->drvinfo);
        db_post_events(pasynRec, pasynRec->drvinfo, monitor_mask);
    }
    POST_IF_NEW(enbl);
    POST_IF_NEW(octetiv);
    POST_IF_NEW(optioniv);
    POST_IF_NEW(gpibiv);
    POST_IF_NEW(i32iv);
    POST_IF_NEW(ui32iv);
    POST_IF_NEW(f64iv);
}

static asynStatus connectDevice(asynRecord * pasynRec)
{
    asynInterface *pasynInterface;
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    asynUser *pasynUserConnect, *pasynUserEos;
    asynStatus status;
    callbackMessage *pmsg;

    resetError(pasynRec);
    /* Disconnect any connected device.  Ignore error if there is no device
     * currently connected. */
    pasynManager->exceptionCallbackRemove(pasynUser);
    pasynManager->disconnect(pasynUser);
    /* Connect to the new device */
    status = pasynManager->connectDevice(pasynUser, pasynRec->port,
                                         pasynRec->addr);
    if(status != asynSuccess) {
        reportError(pasynRec, status, "Connect error, status=%d, %s",
                    status, pasynUser->errorMessage);
        goto bad;
    }
    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if(!pasynInterface) {
        pasynRecPvt->pasynCommon = 0;
        pasynRecPvt->asynCommonPvt = 0;
        reportError(pasynRec, status, "findInterface common: %s",
                    pasynUser->errorMessage);
        status = asynError;
        goto bad;
    }
    pasynRecPvt->pasynCommon = (asynCommon *) pasynInterface->pinterface;
    pasynRecPvt->asynCommonPvt = pasynInterface->drvPvt;
    /* Get asynOption interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser, asynOptionType, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynOption = (asynOption *) pasynInterface->pinterface;
        pasynRecPvt->asynOptionPvt = pasynInterface->drvPvt;
        pasynRec->optioniv = 1;
    } else {
        pasynRecPvt->pasynOption = 0;
        pasynRecPvt->asynOptionPvt = 0;
        pasynRec->optioniv = 0;
    }
    /* Get asynOctet interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynOctet = (asynOctet *) pasynInterface->pinterface;
        pasynRecPvt->asynOctetPvt = pasynInterface->drvPvt;
        pasynRec->octetiv = 1;
    } else {
        pasynRecPvt->pasynOctet = 0;
        pasynRecPvt->asynOctetPvt = 0;
        pasynRec->octetiv = 0;
    }
    /* Get asynInt32 interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynInt32 = (asynInt32 *) pasynInterface->pinterface;
        pasynRecPvt->asynInt32Pvt = pasynInterface->drvPvt;
        pasynRec->i32iv = 1;
    } else {
        pasynRecPvt->pasynInt32 = 0;
        pasynRecPvt->asynInt32Pvt = 0;
        pasynRec->i32iv = 0;
    }
    /* Get asynUInt32Digital interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser, asynUInt32DigitalType, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynUInt32 = (asynUInt32Digital *) pasynInterface->pinterface;
        pasynRecPvt->asynUInt32Pvt = pasynInterface->drvPvt;
        pasynRec->ui32iv = 1;
    } else {
        pasynRecPvt->pasynUInt32 = 0;
        pasynRecPvt->asynUInt32Pvt = 0;
        pasynRec->ui32iv = 0;
    }
    /* Get asynFloat64 interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser, asynFloat64Type, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynFloat64 = (asynFloat64 *) pasynInterface->pinterface;
        pasynRecPvt->asynFloat64Pvt = pasynInterface->drvPvt;
        pasynRec->f64iv = 1;
    } else {
        pasynRecPvt->pasynFloat64 = 0;
        pasynRecPvt->asynFloat64Pvt = 0;
        pasynRec->f64iv = 0;
    }
    /* Get asynGpib interface if it exists */
    pasynInterface = pasynManager->findInterface(pasynUser, asynGpibType, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynGpib = (asynGpib *) pasynInterface->pinterface;
        pasynRecPvt->asynGpibPvt = pasynInterface->drvPvt;
        pasynRec->gpibiv = 1;
    } else {
        pasynRecPvt->pasynGpib = 0;
        pasynRecPvt->asynGpibPvt = 0;
        pasynRec->gpibiv = 0;
    }
    /* Get asynDrvUser interface if it exists */
    pasynInterface = pasynManager->findInterface(pasynUser, asynDrvUserType, 1);
    if(pasynInterface) {
        pasynRecPvt->pasynDrvUser = (asynDrvUser *) pasynInterface->pinterface;
        pasynRecPvt->asynDrvUserPvt = pasynInterface->drvPvt;
        /* If the DRVINFO field is not zero-length then call drvUser->create */
        if (strlen(pasynRec->drvinfo) > 0) {
            status = pasynRecPvt->pasynDrvUser->create(pasynRecPvt->asynDrvUserPvt,
                                                       pasynUser, pasynRec->drvinfo,
                                                       0, 0);
            if (status == asynSuccess) {
                pasynRec->reason = pasynUser->reason;
            } else {
                reportError(pasynRec, status, "Error in asynDrvUser->create()");
            }
        }
    } else {
        pasynRecPvt->pasynDrvUser = 0;
        pasynRecPvt->asynDrvUserPvt = 0;
        pasynRec->reason = 0;
        /* If the DRVINFO field is not zero-length then print error */
        if (strlen(pasynRec->drvinfo) > 0) {
            reportError(pasynRec, asynError, 
                        "asynDrvUser not supported but drvInfo not blank");
        }
    }
    /* Add exception callback */
    pasynManager->exceptionCallbackAdd(pasynUser, exceptCallback);
    /* Get the trace and connect flags */
    monitorStatus(pasynRec);
    if(pasynRec->optioniv) {
        /* Queue a request to get the options */
        pasynUserConnect = pasynManager->duplicateAsynUser(pasynUser,
            asynCallbackSpecial, queueTimeoutCallbackSpecial);
        pasynUserConnect->userData = pasynManager->memMalloc(sizeof(*pmsg));
        pasynUserConnect->reason = ASYN_REASON_QUEUE_EVEN_IF_NOT_CONNECTED;
        pmsg = (callbackMessage *)pasynUserConnect->userData;
        pmsg->callbackType = callbackGetOption;
        status = pasynManager->queueRequest(pasynUserConnect,
                                        asynQueuePriorityConnect,QUEUE_TIMEOUT);
        if(status!=asynSuccess) {
            reportError(pasynRec, asynError, 
                "queueRequest failed\n");
            pasynManager->memFree(pmsg, sizeof(*pmsg));
            pasynManager->freeAsynUser(pasynUserConnect);
        }
    }
    if(pasynRec->octetiv) {
        /* Queue a request to get the EOS */
        pasynUserEos = pasynManager->duplicateAsynUser(pasynUser,
            asynCallbackSpecial, queueTimeoutCallbackSpecial);
        pasynUserEos->userData = pasynManager->memMalloc(sizeof(*pmsg));
        pmsg = (callbackMessage *)pasynUserEos->userData;
        pmsg->callbackType = callbackGetEos;
        status = pasynManager->queueRequest(pasynUserEos,
                                        asynQueuePriorityLow,QUEUE_TIMEOUT);
        if(status!=asynSuccess) {
            reportError(pasynRec, asynError, 
                "queueRequest failed\n");
            pasynManager->memFree(pmsg, sizeof(*pmsg));
            pasynManager->freeAsynUser(pasynUserEos);
        }
    }
    pasynRec->pcnct = 1; 
    status = asynSuccess;
    goto done;

    bad:
    /* Disconnect any connected device again, since failure could have happened
       after connect succeeded.  Ignore error if there is no device
     * currently connected. */
    pasynManager->exceptionCallbackRemove(pasynUser);
    pasynManager->disconnect(pasynUser);
    pasynRec->pcnct = 0;

    done:
    cancelIOInterruptScan(pasynRec);
    monitorStatus(pasynRec); 
    return(status);
}

static void performIO(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;

    switch(pasynRec->iface) {
    case asynINTERFACE_OCTET:
        if (pasynRec->octetiv) {
           performOctetIO(pasynUser);
        } else {
            reportError(pasynRec, asynError, "No asynOctet interface");
            recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        }
        break;
    case asynINTERFACE_INT32:
        if (pasynRec->i32iv) {
           performInt32IO(pasynUser);
        } else {
            reportError(pasynRec, asynError, "No asynInt32 interface");
            recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        }
        break;
    case asynINTERFACE_UINT32:
        if (pasynRec->ui32iv) {
           performUInt32DigitalIO(pasynUser);
        } else {
            reportError(pasynRec, asynError, "No asynUInt32Digital interface");
            recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        }
        break;
    case asynINTERFACE_FLOAT64:
        if (pasynRec->f64iv) {
           performFloat64IO(pasynUser);
        } else {
            reportError(pasynRec, asynError, "No asynFloat64 interface");
            recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        }
        break;
    }
}
        
static void performInt32IO(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynStatus status;

    if((pasynRec->tmod == asynTMOD_Write) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynInt32->write(pasynRecPvt->asynInt32Pvt,
                                                pasynUser, pasynRec->i32out);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, Int32 write data=%d\n", 
                  pasynRec->name, status, pasynRec->i32out);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "Int32 write error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
        }
    }
    if((pasynRec->tmod == asynTMOD_Read) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynInt32->read(pasynRecPvt->asynInt32Pvt,
                                               pasynUser, &pasynRec->i32inp);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, Int32 read data=%d\n", 
                  pasynRec->name, status, pasynRec->i32inp);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "Int32 read error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
        }
    }
}

static void performUInt32DigitalIO(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynStatus status;
    epicsUInt32 data;

    if((pasynRec->tmod == asynTMOD_Write) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynUInt32->write(pasynRecPvt->asynUInt32Pvt,
                                                 pasynUser, pasynRec->ui32out,
                                                 pasynRec->ui32mask);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, UInt32 write data=%d, mask=%d\n",        
                  pasynRec->name, status, pasynRec->ui32out, pasynRec->ui32mask);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "UInt32 write error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
        }
    }
    if((pasynRec->tmod == asynTMOD_Read) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynUInt32->read(pasynRecPvt->asynUInt32Pvt,
                                                pasynUser, &data,
                                                pasynRec->ui32mask);
        pasynRec->ui32inp = data;
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, UInt32 read data=%d, mask=%d\n",        
                  pasynRec->name, status, pasynRec->i32inp, pasynRec->ui32mask);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "UInt32 read error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
        }
    }
}

static void performFloat64IO(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynStatus status;
    
    if((pasynRec->tmod == asynTMOD_Write) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynFloat64->write(pasynRecPvt->asynFloat64Pvt,
                                                  pasynUser, pasynRec->f64out);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, Float64 write data=%f\n",        
                  pasynRec->name, status, pasynRec->f64out);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "Float64 write error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
        }
    }
    if((pasynRec->tmod == asynTMOD_Read) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        status = pasynRecPvt->pasynFloat64->read(pasynRecPvt->asynFloat64Pvt,
                                                 pasynUser, &pasynRec->f64inp);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
                  "%s: status=%d, Float64 read data=%f\n",        
                  pasynRec->name, status, pasynRec->f64inp);
        if(status != asynSuccess) {
            reportError(pasynRec, status, "Float64 read error, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
        }
    }
}

static void performOctetIO(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynStatus status = asynSuccess;
    size_t nbytesTransfered = 0;
    char *inptr;
    char *outptr;
    size_t inlen;
    size_t nread = 0;
    size_t nwrite = 0;
    int eomReason = 0;
    char saveEosBuf[5];
    int saveEosLen;
    int ntranslate;

    if(pasynRec->ofmt == asynFMT_ASCII) {
        /* ASCII output mode */
        /* Translate escape sequences */
        nwrite = dbTranslateEscape(pasynRecPvt->outbuff, pasynRec->aout);
        outptr = pasynRecPvt->outbuff;
    } else if(pasynRec->ofmt == asynFMT_Hybrid) {
        /* Hybrid output mode */
        /* Translate escape sequences */
        nwrite = dbTranslateEscape(pasynRecPvt->outbuff, pasynRec->optr);
        outptr = pasynRecPvt->outbuff;
    } else {
        /* Binary output mode */
        /* Check that nowt is not greated than omax */
        if(pasynRec->nowt > pasynRec->omax) pasynRec->nowt = pasynRec->omax;
        nwrite = pasynRec->nowt;
        outptr = pasynRec->optr;
    }
    if(pasynRec->ifmt == asynFMT_ASCII) {
        /* ASCII input mode */
        inptr = pasynRec->ainp;
        inlen = sizeof(pasynRec->ainp);
    } else {
        /* Binary or Hybrid input mode */
        inptr = pasynRec->iptr;
        inlen = pasynRec->imax;
    }
    /* Make sure nrrd is not more than inlen */
    if(pasynRec->nrrd > (int)inlen) pasynRec->nrrd = (int)inlen;
    if(pasynRec->nrrd != 0) 
        nread = pasynRec->nrrd;
    else
        nread = inlen;
    if((pasynRec->tmod == asynTMOD_Flush) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        /* Flush the input buffer */
        pasynRecPvt->pasynOctet->flush(pasynRecPvt->asynOctetPvt, pasynUser);
        asynPrint(pasynUser, ASYN_TRACE_FLOW, "%s flush\n", pasynRec->name);
    }
    if((pasynRec->tmod == asynTMOD_Write) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        /* Send the message */
        nbytesTransfered = 0;
        if(pasynRec->ofmt == asynFMT_Binary) {
            status = pasynRecPvt->pasynOctet->getOutputEos(
                                    pasynRecPvt->asynOctetPvt,pasynUser,
                                    saveEosBuf,sizeof saveEosBuf,&saveEosLen);
            /* getOutputEos can return an error if the driver does not implement it */
            if (status != asynSuccess) saveEosLen = 0;
            if (saveEosLen)
                pasynRecPvt->pasynOctet->setOutputEos(pasynRecPvt->asynOctetPvt,
                                                      pasynUser,NULL,0);
            status = pasynRecPvt->pasynOctet->write(pasynRecPvt->asynOctetPvt,
                                pasynUser, outptr, nwrite, &nbytesTransfered);
            if (saveEosLen)
                pasynRecPvt->pasynOctet->setOutputEos(pasynRecPvt->asynOctetPvt,
                                                      pasynUser,saveEosBuf,saveEosLen);
        } else {
            /* ASCII or Hybrid mode */
            status = pasynRecPvt->pasynOctet->write(pasynRecPvt->asynOctetPvt,
                                 pasynUser, outptr, nwrite, &nbytesTransfered);
        }
        pasynRec->nawt = (int)nbytesTransfered;
        asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, outptr, nbytesTransfered,
           "%s: nwrite=%lu, status=%d, nawt=%lu\n", pasynRec->name, (unsigned long)nwrite,
                    status, (unsigned long)nbytesTransfered);
        if(status != asynSuccess || nbytesTransfered != nwrite) {
            /* Something is wrong if we couldn't write everything */
            reportError(pasynRec, status, "Write error, nout=%d, %s",
                        nbytesTransfered, pasynUser->errorMessage);
        }
    }
    if((pasynRec->tmod == asynTMOD_Read) ||
        (pasynRec->tmod == asynTMOD_Write_Read)) {
        /* Set the input buffer to all zeros */
        memset(inptr, 0, inlen);
        /* Read the message  */
        nbytesTransfered = 0;
        status = asynSuccess;
        if(pasynRec->ifmt == asynFMT_Binary) {
            status = pasynRecPvt->pasynOctet->getInputEos(
                                    pasynRecPvt->asynOctetPvt,pasynUser,
                                    saveEosBuf,sizeof saveEosBuf,&saveEosLen);
            /* getInputEos can return an error if the driver does not implement it */
            if (status != asynSuccess) saveEosLen = 0;
            if (saveEosLen) 
                pasynRecPvt->pasynOctet->setInputEos(pasynRecPvt->asynOctetPvt,
                                                     pasynUser,NULL,0);
            status = pasynRecPvt->pasynOctet->read(pasynRecPvt->asynOctetPvt,
                  pasynUser, inptr, nread, &nbytesTransfered,&eomReason);
            if (saveEosLen) 
                pasynRecPvt->pasynOctet->setInputEos(pasynRecPvt->asynOctetPvt,
                                                     pasynUser,saveEosBuf,saveEosLen);
        } else {
            /* ASCII or Hybrid mode */
            status = pasynRecPvt->pasynOctet->read(pasynRecPvt->asynOctetPvt,
                        pasynUser, inptr, nread, &nbytesTransfered, &eomReason);
        }
        if(status!=asynSuccess) {
            reportError(pasynRec, status,
                "Error %s", pasynUser->errorMessage);
        } else {
            asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, inptr, nbytesTransfered,
             "%s: inlen=%lu, status=%d, ninp=%lu\n", pasynRec->name, (unsigned long)inlen,
                    status, (unsigned long)nbytesTransfered);
        }
        pasynRec->eomr = eomReason;
        inlen = nbytesTransfered;
        if(status != asynSuccess) {
            reportError(pasynRec, status,
                "%s  nread %d %s",
                ((status==asynTimeout) ? "timeout" :
                 (status==asynOverflow) ? "overflow" : "error"),
                 nbytesTransfered, pasynUser->errorMessage);
            recGblSetSevr(pasynRec,READ_ALARM, MAJOR_ALARM);
        }
        /* Check for input buffer overflow */
        if((pasynRec->ifmt == asynFMT_ASCII) &&
            (nbytesTransfered >= (int) sizeof(pasynRec->ainp))) {
            reportError(pasynRec, status, "Overflow nread %d %s",
                nbytesTransfered, pasynUser->errorMessage);
            recGblSetSevr(pasynRec,READ_ALARM, MINOR_ALARM);
            /* terminate response with \0 */
            inptr[sizeof(pasynRec->ainp) - 1] = '\0';
        } else if((pasynRec->ifmt == asynFMT_Hybrid) &&
                   ((int)nbytesTransfered >= pasynRec->imax)) {
            reportError(pasynRec, status, "Overflow nread %d %s",
                nbytesTransfered, pasynUser->errorMessage);
            recGblSetSevr(pasynRec,READ_ALARM, MINOR_ALARM);
            /* terminate response with \0 */
            inptr[pasynRec->imax - 1] = '\0';
        } else if((pasynRec->ifmt == asynFMT_Binary) &&
                   ((int)nbytesTransfered > pasynRec->imax)) {
            /* This should not be able to happen */
            reportError(pasynRec, status, "Overflow nread %d %s",
                nbytesTransfered, pasynUser->errorMessage);
            recGblSetSevr(pasynRec,READ_ALARM, MINOR_ALARM);
        } else if(pasynRec->ifmt != asynFMT_Binary) {
            /* Not binary and no input buffer overflow has occurred */
            /* Add null at end of input.  This is safe because of tests above */
            inptr[nbytesTransfered] = '\0';
        }
        pasynRec->nord = (int)nbytesTransfered;    /* Number of bytes read */
        /* Copy to tinp with dbTranslateEscape */
        ntranslate = epicsStrSnPrintEscaped(pasynRec->tinp, 
                                           sizeof(pasynRec->tinp),
                                           inptr, inlen);
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE,
             "%s: inlen=%lu, nbytesTransfered=%lu, ntranslate=%d\n",
             pasynRec->name, (unsigned long)inlen, (unsigned long)nbytesTransfered, ntranslate);
    }
}

static void gpibUniversalCmd(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynGpib   *pasynGpib = pasynRecPvt->pasynGpib;
    void       *asynGpibPvt = pasynRecPvt->asynGpibPvt;
    asynStatus status;
    char       cmd_char = 0;

    if (!pasynRec->gpibiv) {
        reportError(pasynRec, asynError, "No asynGpib interface");
        recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        return;
    }
    switch (pasynRec->ucmd) {
    case gpibUCMD_Device_Clear__DCL_:
        cmd_char = IBDCL;
        break;
    case gpibUCMD_Local_Lockout__LL0_:
        cmd_char = IBLLO;
        break;
    case gpibUCMD_Serial_Poll_Disable__SPD_:
        cmd_char = IBSPD;
        break;
    case gpibUCMD_Serial_Poll_Enable__SPE_:
        cmd_char = IBSPE;
        break;
    case gpibUCMD_Unlisten__UNL_:
        cmd_char = IBUNL;
        break;
    case gpibUCMD_Untalk__UNT_:
        cmd_char = IBUNT;
        break;
    }
    status = pasynGpib->universalCmd(asynGpibPvt, pasynUser, cmd_char);
    if(status) {
        /* Something is wrong if we couldn't write */
        reportError(pasynRec, status,"GPIB Universal command %s",
                    pasynUser->errorMessage);
        recGblSetSevr(pasynRec,WRITE_ALARM, MAJOR_ALARM);
    }
}

static void gpibAddressedCmd(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynGpib   *pasynGpib = pasynRecPvt->pasynGpib;
    void       *asynGpibPvt = pasynRecPvt->asynGpibPvt;
    asynStatus status;
    size_t     nbytesTransfered;
    char       acmd[6];
    char       cmd_char = 0;
    int lenCmd = 6;

    if (!pasynRec->gpibiv) {
        reportError(pasynRec, asynError, "No asynGpib interface");
        recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        return;
    }
    acmd[0] = IBUNT; acmd[1] = IBUNL;
    acmd[2] = pasynRec->addr + LADBASE;    /* GPIB address + Listen Base */
    acmd[4] = IBUNT; acmd[5] = IBUNL;
    switch (pasynRec->acmd) {
    case gpibACMD_Group_Execute_Trig___GET_:
        acmd[3] = IBGET[0];
        break;
    case gpibACMD_Go_To_Local__GTL_:
        acmd[3] = IBGTL[0];
        break;
    case gpibACMD_Selected_Dev__Clear__SDC_:
        acmd[3] = IBSDC[0];
        break;
    case gpibACMD_Take_Control__TCT_:
        /* This command requires Talker Base */
        acmd[2] = pasynRec->addr + TADBASE;
        acmd[3] = IBTCT[0];
        lenCmd = 4;
        break;
    case gpibACMD_Serial_Poll:
        /* Serial poll. Requires 3 operations */
        /* Serial Poll Enable */
        cmd_char = IBSPE;
        status = pasynGpib->universalCmd(asynGpibPvt, pasynUser, cmd_char);
        if(status != asynSuccess) {
            reportError(pasynRec, status,
                        "Error in GPIB Serial Poll write, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec,WRITE_ALARM, MAJOR_ALARM);
        }
        /* Read the response byte  */
        status = pasynRecPvt->pasynOctet->read(pasynRecPvt->asynOctetPvt,
                pasynUser, (char *) &pasynRec->spr, 1, &nbytesTransfered,0);
        if(status != asynSuccess || nbytesTransfered != 1) {
            reportError(pasynRec, status,
                        "Error in GPIB Serial Poll read, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec,READ_ALARM, MAJOR_ALARM);
        }
        /* Serial Poll Disable */
        cmd_char = IBSPD;
        status = pasynGpib->universalCmd(asynGpibPvt, pasynUser, cmd_char);
        if(status != asynSuccess) {
            reportError(pasynRec, status,
                        "Error in GPIB Serial Poll disable write, %s",
                        pasynUser->errorMessage);
            recGblSetSevr(pasynRec,WRITE_ALARM, MAJOR_ALARM);
        }
        return;
    }
    status = pasynRecPvt->pasynGpib->addressedCmd(pasynRecPvt->asynGpibPvt,
                                    pasynUser, acmd, lenCmd);
    if(status) {
        reportError(pasynRec, status,
                    "Error in GPIB Addressed Command write, %s",
                    pasynUser->errorMessage);
        recGblSetSevr(pasynRec,WRITE_ALARM, MAJOR_ALARM);
    }
}

static void setOption(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
    callbackMessage *pmsg = (callbackMessage *)pasynUser->userData;
    asynRecord *pasynRec = pasynRecPvt->prec;
    asynStatus status = asynSuccess;
    char optionString[20];

    /* If port does not have an asynOption interface report error and return */
    if (!pasynRec->optioniv) {
        reportError(pasynRec, asynError, "No asynOption interface");
        recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        return;
    }

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s: setOptionCallback port=%s, addr=%d index=%d\n",
              pasynRec->name, pasynRec->port, pasynRec->addr,
              pmsg->fieldIndex);
    switch (pmsg->fieldIndex) {
    case asynRecordBAUD:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
            pasynUser, "baud", baud_choices[pasynRec->baud]);
        break;
    case asynRecordLBAUD:
        sprintf(optionString, "%d", pasynRec->lbaud);
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
            pasynUser, "baud", optionString);
        break;
    case asynRecordPRTY:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
            pasynUser, "parity", parity_choices[pasynRec->prty]);
        break;
    case asynRecordSBIT:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
            pasynUser, "stop", stop_bit_choices[pasynRec->sbit]);
        break;
    case asynRecordDBIT:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "bits", data_bit_choices[pasynRec->dbit]);
        break;
    case asynRecordMCTL:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "clocal", modem_control_choices[pasynRec->mctl]);
        break;
    case asynRecordFCTL:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "crtscts", flow_control_choices[pasynRec->fctl]);
        break;
    case asynRecordIXON:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "ixon", ix_control_choices[pasynRec->ixon]);
        break;
    case asynRecordIXOFF:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "ixoff", ix_control_choices[pasynRec->ixoff]);
        break;
    case asynRecordIXANY:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "ixany", ix_control_choices[pasynRec->ixany]);
        break;
    case asynRecordDRTO:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "disconnectOnReadTimeout", drto_choices[pasynRec->drto]);
        break;
    case asynRecordHOSTINFO:
        status = pasynRecPvt->pasynOption->setOption(pasynRecPvt->asynOptionPvt,
           pasynUser, "hostInfo", pasynRec->hostinfo);
        break;
    }
    if(status != asynSuccess) {
        reportError(pasynRec, status,
            "Error setting option, %s", pasynUser->errorMessage);
    }
}

static void getOptions(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    char optbuff[OPT_SIZE];
    int i;
    unsigned short monitor_mask = DBE_VALUE | DBE_LOG;

    /* If port does not have an asynOption interface return */
    if (!pasynRec->optioniv) return;

    asynPrint(pasynUser, ASYN_TRACE_FLOW,
              "%s: getOptionCallback() port=%s, addr=%d\n",
              pasynRec->name, pasynRec->port, pasynRec->addr);
    /* For fields that could have been changed externally we need to remember
     * their current value */
    REMEMBER_STATE(baud);
    REMEMBER_STATE(lbaud);
    REMEMBER_STATE(prty);
    REMEMBER_STATE(sbit);
    REMEMBER_STATE(dbit);
    REMEMBER_STATE(mctl);
    REMEMBER_STATE(fctl);
    REMEMBER_STATE(ixon);
    REMEMBER_STATE(ixoff);
    REMEMBER_STATE(ixany);
    REMEMBER_STATE(drto);
    strncpy(pasynRecPvt->old.hostinfo, pasynRec->hostinfo, sizeof(pasynRec->hostinfo));
    /* Get port options */
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "baud", optbuff, OPT_SIZE);
    pasynRec->baud = 0;
    sscanf(optbuff, "%d", &pasynRec->lbaud);
    for (i = 0; i < NUM_BAUD_CHOICES; i++)
        if(strcmp(optbuff, baud_choices[i]) == 0)
            pasynRec->baud = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "parity", optbuff, OPT_SIZE);
    pasynRec->prty = 0;
    for (i = 0; i < NUM_PARITY_CHOICES; i++)
        if(strcmp(optbuff, parity_choices[i]) == 0)
            pasynRec->prty = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "stop", optbuff, OPT_SIZE);
    pasynRec->sbit = 0;
    for (i = 0; i < NUM_SBIT_CHOICES; i++)
        if(strcmp(optbuff, stop_bit_choices[i]) == 0)
            pasynRec->sbit = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "bits", optbuff, OPT_SIZE);
    pasynRec->dbit = 0;
    for (i = 0; i < NUM_DBIT_CHOICES; i++)
        if(strcmp(optbuff, data_bit_choices[i]) == 0)
            pasynRec->dbit = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "clocal", optbuff, OPT_SIZE);
    pasynRec->mctl = 0;
    for (i = 0; i < NUM_MODEM_CHOICES; i++)
        if(strcmp(optbuff, modem_control_choices[i]) == 0)
            pasynRec->mctl = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "crtscts", optbuff, OPT_SIZE);
    pasynRec->fctl = 0;
    for (i = 0; i < NUM_FLOW_CHOICES; i++)
        if(strcmp(optbuff, flow_control_choices[i]) == 0)
            pasynRec->fctl = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "ixon", optbuff, OPT_SIZE);
    pasynRec->ixon = 0;
    for (i = 0; i < NUM_IX_CHOICES; i++)
        if(strcmp(optbuff, ix_control_choices[i]) == 0)
            pasynRec->ixon = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "ixoff", optbuff, OPT_SIZE);
    pasynRec->ixoff = 0;
    for (i = 0; i < NUM_IX_CHOICES; i++)
        if(strcmp(optbuff, ix_control_choices[i]) == 0)
            pasynRec->ixoff = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "ixany", optbuff, OPT_SIZE);
    pasynRec->ixany = 0;
    for (i = 0; i < NUM_IX_CHOICES; i++)
        if(strcmp(optbuff, ix_control_choices[i]) == 0)
            pasynRec->ixany = i;
    pasynRec->drto = 0;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "disconnectOnReadTimeout", optbuff, OPT_SIZE);
    for (i = 0; i < NUM_DRTO_CHOICES; i++)
        if(strcmp(optbuff, drto_choices[i]) == 0)
            pasynRec->drto = i;
    pasynRecPvt->pasynOption->getOption(pasynRecPvt->asynOptionPvt, pasynUser,
                                        "hostinfo", optbuff, OPT_SIZE);
    strncpy(pasynRec->hostinfo, optbuff, sizeof(pasynRec->hostinfo));
    POST_IF_NEW(baud);
    POST_IF_NEW(lbaud);
    POST_IF_NEW(prty);
    POST_IF_NEW(sbit);
    POST_IF_NEW(dbit);
    POST_IF_NEW(mctl);
    POST_IF_NEW(fctl);
    POST_IF_NEW(ixon);
    POST_IF_NEW(ixoff);
    POST_IF_NEW(ixany);
    POST_IF_NEW(drto);
    if (strncmp(pasynRec->hostinfo, pasynRecPvt->old.hostinfo, sizeof(pasynRec->hostinfo)) != 0) {
        if(interruptAccept)
           db_post_events(pasynRec, &pasynRec->hostinfo, monitor_mask);
        strncpy(pasynRecPvt->old.hostinfo, pasynRec->hostinfo, sizeof(pasynRec->hostinfo));
    }
}


static void setEos(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
    callbackMessage *pmsg = (callbackMessage *)pasynUser->userData;
    asynRecord *pasynRec = pasynRecPvt->prec;
    char eosBuff[EOS_SIZE];
    int eoslen;
    asynStatus status;

    /* If port does not have an asynOctet interface report error and return */
    if (!pasynRec->octetiv) {
        reportError(pasynRec, asynError, "No asynOctet interface");
        recGblSetSevr(pasynRec,COMM_ALARM, MAJOR_ALARM);
        return;
    }

    switch (pmsg->fieldIndex) {
    case asynRecordIEOS:
        eoslen = dbTranslateEscape(eosBuff, pasynRec->ieos);
        status = pasynRecPvt->pasynOctet->setInputEos(
                     pasynRecPvt->asynOctetPvt, pasynUser, eosBuff, eoslen);
        if (status!=asynSuccess) {
            reportError(pasynRec, status,
                        "Error setting input eos, %s", pasynUser->errorMessage);
        }
        break;
    case asynRecordOEOS:
        eoslen = dbTranslateEscape(eosBuff, pasynRec->oeos);
        status = pasynRecPvt->pasynOctet->setOutputEos(
                     pasynRecPvt->asynOctetPvt, pasynUser, eosBuff, eoslen);
        if(status!=asynSuccess) {
            reportError(pasynRec, status,
                       "Error setting output eos, %s", pasynUser->errorMessage);
        }
        break;
    }
}

static void getEos(asynUser * pasynUser)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    char eosBuff[EOS_SIZE];
    char outputEosTranslate[EOS_SIZE];
    char inputEosTranslate[EOS_SIZE];
    int eosSize;
    asynStatus status;
    unsigned short monitor_mask = DBE_VALUE | DBE_LOG;

    /* Set the eos strings to 0 length */
    outputEosTranslate[0] = 0;
    inputEosTranslate[0] = 0;
    /* If port does not have an asynOctet interface skip */
    if (!pasynRec->octetiv) goto post;

    status = pasynRecPvt->pasynOctet->getInputEos(pasynRecPvt->asynOctetPvt, 
                                          pasynUser, eosBuff, EOS_SIZE, &eosSize);
    if ((status == asynSuccess) && (eosSize > 0)) {
        epicsStrSnPrintEscaped(inputEosTranslate, 
                               sizeof(inputEosTranslate),
                               eosBuff, eosSize);
    }
    status = pasynRecPvt->pasynOctet->getOutputEos(pasynRecPvt->asynOctetPvt, 
                                          pasynUser, eosBuff, EOS_SIZE, &eosSize);
    if ((status == asynSuccess) && (eosSize > 0)) {
        epicsStrSnPrintEscaped(outputEosTranslate, 
                               sizeof(outputEosTranslate),
                               eosBuff, eosSize);
    }
    post:
    if (strcmp(inputEosTranslate, pasynRec->ieos) != 0) {
        strncpy(pasynRec->ieos, inputEosTranslate, sizeof(pasynRec->ieos));
        db_post_events(pasynRec, pasynRec->ieos, monitor_mask);
    }
    if (strcmp(outputEosTranslate, pasynRec->oeos) != 0) {
        strncpy(pasynRec->oeos, outputEosTranslate, sizeof(pasynRec->oeos));
        db_post_events(pasynRec, pasynRec->oeos, monitor_mask);
    }
}


static void reportError(asynRecord * pasynRec, asynStatus status,
    const char *pformat,...)
{
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    unsigned short monitor_mask;
    char buffer[ERR_SIZE + 1];
    va_list pvar;
    va_start(pvar, pformat);
    epicsVsnprintf(buffer, ERR_SIZE, pformat, pvar);
    va_end(pvar);
    if(status != asynSuccess && status != asynTimeout) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s: %s\n",
                  pasynRec->name, buffer);
    }
    strncpy(pasynRec->errs, buffer, ERR_SIZE);
    if(strncmp(pasynRec->errs, pasynRecPvt->old.errs, ERR_SIZE) != 0) {
        strncpy(pasynRecPvt->old.errs, pasynRec->errs, ERR_SIZE);
        monitor_mask = DBE_VALUE | DBE_LOG;
        db_post_events(pasynRec, pasynRec->errs, monitor_mask);
    }
}
static void resetError(asynRecord * pasynRec)
{
    asynRecPvt *pasynRecPvt = pasynRec->dpvt;
    unsigned short monitor_mask;
    pasynRec->errs[0] = 0;
    if(strncmp(pasynRec->errs, pasynRecPvt->old.errs, ERR_SIZE) != 0) {
        strncpy(pasynRecPvt->old.errs, pasynRec->errs, ERR_SIZE);
        monitor_mask = DBE_VALUE | DBE_LOG;
        db_post_events(pasynRec, pasynRec->errs, monitor_mask);
    }
}


static const char * asynExceptionStrings[] = { ASYN_EXCEPTION_STRINGS };
const char * asynExceptionToString( asynException e )
{
    if ( e < 0 || e > asynExceptionTraceIOTruncateSize )
        return "Invalid Exception Number!";
    return asynExceptionStrings[e];
}

