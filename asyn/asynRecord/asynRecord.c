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
#include <devSup.h>
#include <drvSup.h>
#include <errMdef.h>
#include <recSup.h>
#include <recGbl.h>
#include <epicsString.h>
#include <epicsExport.h>
#include <asynGpibDriver.h>
#include <asynDriver.h>
#include <drvAsynTCPPort.h>
#define GEN_SIZE_OFFSET
#include "asynRecord.h"
#undef GEN_SIZE_OFFSET

/* These should be in a header file*/
#define NUM_BAUD_CHOICES 12
static char *baud_choices[NUM_BAUD_CHOICES]={"unknown",
                                             "300","600","1200","2400","4800",
                                             "9600","19200","38400","57600",
                                             "115200","230400"};
#define NUM_PARITY_CHOICES 4
static char *parity_choices[NUM_PARITY_CHOICES]={"unknown","none","even","odd"};
#define NUM_DBIT_CHOICES 5
static char *data_bit_choices[NUM_DBIT_CHOICES]={"unknown","5","6","7","8"};
#define NUM_SBIT_CHOICES 3
static char *stop_bit_choices[NUM_SBIT_CHOICES]={"unknown","1","2"};
#define NUM_FLOW_CHOICES 3
static char *flow_control_choices[NUM_FLOW_CHOICES]={"unknown","Y","N"};

#define OPT_SIZE 80  /* Size of buffer for setting and getting port options */
#define EOS_SIZE 10  /* Size of buffer for EOS */


/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
static long init_record(asynRecord *pasynRec, int pass);
static long process(asynRecord *pasynRec);
static long special(struct dbAddr *paddr, int after);
#define get_value NULL
static long cvt_dbaddr(struct dbAddr *paddr);
static long get_array_info(struct dbAddr *paddr, long *no_elements, 
                           long *offset);
static long put_array_info(struct dbAddr *paddr, long nNew);
#define get_units NULL
static long get_precision(struct dbAddr *paddr, long *precision);
#define get_enum_str NULL
#define get_enum_strs NULL
#define put_enum_str NULL
#define get_graphic_double NULL
#define get_control_double NULL
#define get_alarm_double NULL

static void monitor(asynRecord *pasynRec);
static void monitorStatus(asynRecord *pasynRec);
static asynStatus connectDevice(asynRecord *pasynRec);
static void GPIB_command(asynUser *pasynUser);

static void asynCallback(asynUser *pasynUser);
static void exceptCallback(asynUser *pasynUser, asynException exception);
static void performIO(asynUser *pasynUser);
static void setOption(asynUser *pasynUser);
static void getOptions(asynUser *pasynUser);
static void reportError(asynRecord *pasynRec, int status, int severity,
                        const char *errorMessage, const char *pformat, ...);
static void resetError(asynRecord *pasynRec);

struct rset asynRSET={
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
   get_alarm_double };
epicsExportAddress(rset, asynRSET);


typedef struct oldValues {  /* Used in monitor() and monitorStatus() */
    epicsInt32      addr;   /*asyn address*/
    epicsInt32      nowt;   /*Number of bytes to write*/
    epicsInt32      nawt;   /*Number of bytes actually written*/
    epicsInt32      nrrd;   /*Number of bytes to read*/
    epicsInt32      nord;   /*Number of bytes read*/
    epicsEnum16     baud;   /*Baud rate*/
    epicsEnum16     prty;   /*Parity*/
    epicsEnum16     dbit;   /*Data bits*/
    epicsEnum16     sbit;   /*Stop bits*/
    epicsEnum16     fctl;   /*Flow control*/
    epicsEnum16     ucmd;   /*Universal command*/
    epicsEnum16     acmd;   /*Addressed command*/
    unsigned char   spr;    /*Serial poll response*/
    epicsEnum16     tb0;    /*Trace error*/
    epicsEnum16     tb1;    /*Trace IO device*/
    epicsEnum16     tb2;    /*Trace IO filter*/
    epicsEnum16     tb3;    /*Trace IO driver*/
    epicsEnum16     tb4;    /*Trace flow*/
    epicsEnum16     tib0;   /*Trace IO ASCII*/
    epicsEnum16     tib1;   /*Trace IO escape*/
    epicsEnum16     tib2;   /*Trace IO hex*/
    epicsInt32      tsiz;   /*Trace IO truncate size*/
    char            tfil[40]; /*Trace IO file*/
    FILE*           traceFd; /*Trace file descriptor*/
    epicsEnum16     auct;   /*Autoconnect*/
    epicsEnum16     cnct;   /*Connect/Disconnect*/
    epicsEnum16     enbl;   /*Enable/Disable*/
    char            errs[40]; /*Error string*/
} oldValues;

#define REMEMBER_STATE(FIELD) pasynRecPvt->old.FIELD = pasynRec->FIELD
#define POST_IF_NEW(FIELD) \
    if (pasynRec->FIELD != pasynRecPvt->old.FIELD) { \
        if(interruptAccept) db_post_events(pasynRec, &pasynRec->FIELD, monitor_mask); \
        pasynRecPvt->old.FIELD = pasynRec->FIELD; }

typedef enum {
    stateIdle,stateInitalIO,stateConnect,
    stateIO,stateGetOption,stateSetOption
} callbackState;

typedef struct asynRecPvt {
    CALLBACK        callback;
    asynRecord      *prec;  /* Pointer to record */
    callbackState   state; 
    int             fieldIndex; /* For special */
    asynUser        *pasynUser;
    asynOctet       *pasynOctet;
    void            *asynOctetPvt;
    asynCommon      *pasynCommon;
    void            *asynCommonPvt;
    asynGpib        *pasynGpib;
    void *          asynGpibPvt;
    char            *outbuff;
    oldValues       old;
} asynRecPvt;

static long init_record(asynRecord *pasynRec, int pass)
{
    asynRecPvt *pasynRecPvt;
    asynUser   *pasynUser;
    asynStatus status;

    if (pass != 0) return(0);

    /* Allocate and initialize private structure used by this record */
    pasynRecPvt = (asynRecPvt*) callocMustSucceed(
        1, sizeof(asynRecPvt),"asynRecord");
    pasynRec->dpvt = pasynRecPvt;

    pasynRecPvt->prec = pasynRec;

    /* Initialize asyn, connect to device */
    pasynUser = pasynManager->createAsynUser(asynCallback,0);
    pasynRecPvt->pasynUser = pasynUser;
    pasynUser->userPvt = pasynRecPvt;
    if (strlen(pasynRec->port) != 0) {
        status = connectDevice(pasynRec);
        if(status==asynSuccess) {
           pasynRecPvt->state =
               (pasynRec->pini) ? stateInitalIO : stateGetOption;
           pasynRec->pini = 1;
        }
    }
    /* Allocate the space for the binary/hybrid output and binary/hybrid 
     * input arrays */
    if (pasynRec->omax <= 0) pasynRec->omax=MAX_STRING_SIZE;
    if (pasynRec->imax <= 0) pasynRec->imax=MAX_STRING_SIZE;
    pasynRec->optr = (char *)callocMustSucceed(
        pasynRec->omax, sizeof(char),"asynRecord");
    pasynRec->iptr = (char *)callocMustSucceed(
        pasynRec->imax, sizeof(char),"asynRecord");
    pasynRecPvt->outbuff =  (char *)callocMustSucceed(
        pasynRec->omax, sizeof(char),"asynRecord");
    strcpy(pasynRec->tfil, "Unknown");
    pasynRec->udf = 0;
    return(0);
}

static long process(asynRecord *pasynRec)
{
   asynRecPvt* pasynRecPvt = pasynRec->dpvt;
   asynStatus status;

   /* This routine is called under the following possible states:
    * state                 pact
    * stateIdle             0     Queue request, callback will performIOi
    *                             unless TMOD=NoI/O
    * stateSetOption        0     Queue request, callback will setOption
    * stateGetOption        0     Queue request, callback will getOption
    * stateConnect          0     Queue request with asynQueuePriorityConnect,
    *                             callback will connect or disconnect
    * stateConnect          1     Connect/disconnect request when there is
    *                             already a queueRequest.  Set pact=0 and
    *                             process record again.
    * Any                   1     Callback, finish record processing
    */

   if(!pasynRec->pact) {
       if(pasynRecPvt->state==stateIdle && pasynRec->tmod!=asynTMOD_NoIO) { 
          pasynRecPvt->state = stateIO;
          /* Need to store state of fields that could have been changed from
           * outside since the last time the values were posted by monitor()
           * and which process() or routines it calls can also change */
          REMEMBER_STATE(ucmd);
          REMEMBER_STATE(acmd);
          REMEMBER_STATE(nowt);
       }
      /* Make sure nrrd and nowt are valid */
      if (pasynRec->nrrd > pasynRec->imax) pasynRec->nrrd = pasynRec->imax;
      if (pasynRec->nowt > pasynRec->omax) pasynRec->nowt = pasynRec->omax;
      pasynManager->queueRequest(pasynRecPvt->pasynUser, 
          ((pasynRecPvt->state==stateConnect)
           ? asynQueuePriorityConnect : asynQueuePriorityLow),
           0.0);
      pasynRec->pact = TRUE;
      return(0);
   } else if(pasynRecPvt->state==stateConnect) {
       /*special cancelRequest and callbackRequestProcessCallback*/
       /*Just process the record again */
       scanOnce(pasynRec);
   } 
   recGblGetTimeStamp(pasynRec);
   monitor(pasynRec);
   recGblFwdLink(pasynRec);
   pasynRec->pact=FALSE;
   return(0);
}

/* special() is called when many fields are changed */

static long special(struct dbAddr *paddr, int after)
{
    asynRecord *pasynRec=(asynRecord *)paddr->precord;
    int fieldIndex = dbGetFieldIndex(paddr);
    asynRecPvt *pasynRecPvt=pasynRec->dpvt;
    asynUser *pasynUser=pasynRecPvt->pasynUser;
    asynStatus status=asynSuccess;
    unsigned short monitor_mask;
    int traceMask;
    FILE *fd;

    if (!after) return(status);
    /* The first set of fields can be handled even if state != stateIdle */
    switch (fieldIndex) {
       case asynRecordTB0: 
       case asynRecordTB1: 
       case asynRecordTB2: 
       case asynRecordTB3: 
       case asynRecordTB4:
          traceMask = (pasynRec->tb0 ? ASYN_TRACE_ERROR    : 0) |
                      (pasynRec->tb1 ? ASYN_TRACEIO_DEVICE : 0) |
                      (pasynRec->tb2 ? ASYN_TRACEIO_FILTER : 0) |
                      (pasynRec->tb3 ? ASYN_TRACEIO_DRIVER : 0) |
                      (pasynRec->tb4 ? ASYN_TRACE_FLOW     : 0);
          pasynTrace->setTraceMask(pasynUser, traceMask);
          goto done;
       case asynRecordTIB0: 
       case asynRecordTIB1: 
       case asynRecordTIB2: 
          traceMask = (pasynRec->tib0 ? ASYN_TRACEIO_ASCII  : 0) |
                      (pasynRec->tib1 ? ASYN_TRACEIO_ESCAPE : 0) |
                      (pasynRec->tib2 ? ASYN_TRACEIO_HEX    : 0);
          pasynTrace->setTraceIOMask(pasynUser, traceMask);
          goto done;
       case asynRecordTSIZ:
          pasynTrace->setTraceIOTruncateSize(pasynUser, pasynRec->tsiz);
          goto done;
       case asynRecordTFIL:
          if (strlen(pasynRec->tfil) == 0) {
             /* Zero length, use stdout */
             fd = stdout;
          } else {
             fd = fopen(pasynRec->tfil, "a+");
          }
          if (fd) {
             /* Set old value to this, so monitorStatus won't think another
              * thread changed it */
             /* Must do this before calling setTraceFile, because it 
              * generates an exeception, which calls monitorStatus() */
             pasynRecPvt->old.traceFd = fd;
             status = pasynTrace->setTraceFile(pasynUser, fd);
             if (status != asynSuccess) {
                reportError(pasynRec, WRITE_ALARM, MINOR_ALARM,
                            "Error opening trace file",
                            "setTraceFile failed, %s", 
                            pasynUser->errorMessage);
                }
          } else {    
             reportError(pasynRec, WRITE_ALARM, MINOR_ALARM,
                         "Error opening trace file",
                         "fopen failed for %s", pasynRec->tfil);
          }
          goto done;
       case asynRecordAUCT:
          pasynManager->autoConnect(pasynUser, pasynRec->auct);
          goto done;
       case asynRecordENBL:
          pasynManager->enable(pasynUser, pasynRec->enbl);
          goto done;
    }
    if(fieldIndex==asynRecordCNCT) {
        if(pasynRec->pact) {
            int wasQueued;
            asynPrint(pasynUser,ASYN_TRACE_FLOW,
                "%s:special connect/disconnect calling cancelRequest\n",
                pasynRec->name);
            status = pasynManager->cancelRequest(pasynUser,&wasQueued);
            if(status!=asynSuccess || !wasQueued) {
                reportError(pasynRec, COMM_ALARM, MINOR_ALARM,
                    "special connect/disconnect",
                    "asynCallback is active");
                return 0;
            }
            pasynRecPvt->state = stateConnect;
            callbackRequestProcessCallback(&pasynRecPvt->callback,
                pasynRec->prio,pasynRec);
            goto done;
         }
         asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "%s:special connect/disconnect\n", pasynRec->name);
         pasynRecPvt->state = stateConnect;
         scanOnce(pasynRec);
         goto done;
    }
    /* These cases require idle state */
    if(pasynRecPvt->state!=stateIdle) {
       reportError(pasynRec, COMM_ALARM, MINOR_ALARM,
           "special",
           " state!=stateIdle, state=%d, try again later",pasynRecPvt->state);
       goto done;
    } 
    switch (fieldIndex) {
       case asynRecordSOCK:
          strcpy(pasynRec->port, pasynRec->sock);
          pasynRec->addr = 0;
          status = drvAsynTCPPortConfigure(pasynRec->port, pasynRec->sock, 
                                             0, 0, 0);
          if (status != asynSuccess) goto done;
          monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
          db_post_events(pasynRec, pasynRec->port, monitor_mask);
          db_post_events(pasynRec, &pasynRec->addr, monitor_mask);
          /* Now do same thing as when port or addr change */
       case asynRecordPORT: 
       case asynRecordADDR: 
          /* If the PORT or ADDR fields changed then reconnect to new device */
          status = connectDevice(pasynRec);
          asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "%s: special() port=%s, addr=%d, connect status=%d\n",
             pasynRec->name, pasynRec->port, pasynRec->addr, status);
          if(status==asynSuccess) getOptions(pasynUser);
          break;
       case asynRecordBAUD: 
       case asynRecordPRTY: 
       case asynRecordDBIT: 
       case asynRecordSBIT: 
       case asynRecordFCTL: 
          /* One of the serial port parameters */
          pasynRecPvt->fieldIndex = fieldIndex;
          pasynRecPvt->state = stateSetOption;
          asynPrint(pasynUser,ASYN_TRACE_FLOW,
              "%s: special() port=%s, addr=%d scanOnce request\n",
              pasynRec->name, pasynRec->port, pasynRec->addr);
          scanOnce(pasynRec);
          break;
    }
done:
    return(status);
}

static long cvt_dbaddr(struct dbAddr *paddr)
{
   asynRecord *pasynRec=(asynRecord *)paddr->precord;
   int fieldIndex = dbGetFieldIndex(paddr);

   if (fieldIndex == asynRecordBOUT) {
      paddr->pfield = (void *)(pasynRec->optr);
      paddr->no_elements = pasynRec->omax;
      paddr->field_type = DBF_CHAR;
      paddr->field_size = sizeof(char);
      paddr->dbr_field_type = DBF_CHAR;
   } else if (fieldIndex == asynRecordBINP) {
      paddr->pfield = (unsigned char *)(pasynRec->iptr);
      paddr->no_elements = pasynRec->imax;
      paddr->field_type = DBF_CHAR;
      paddr->field_size = sizeof(char);
      paddr->dbr_field_type = DBF_CHAR;
   }
   return(0);
}

static long get_array_info(struct dbAddr *paddr, long *no_elements, 
                           long *offset)
{
   asynRecord *pasynRec=(asynRecord *)paddr->precord;

   *no_elements =  pasynRec->nord;
   *offset = 0;
   return(0);
}

static long put_array_info(struct dbAddr *paddr, long nNew)
{
   asynRecord *pasynRec=(asynRecord *)paddr->precord;

   pasynRec->nowt = nNew;
   if (pasynRec->nowt > pasynRec->omax) pasynRec->nowt = pasynRec->omax;
   return(0);
}

static long get_precision(struct dbAddr *paddr, long *precision)
{
   int fieldIndex = dbGetFieldIndex(paddr);

   *precision = 0;
   if (fieldIndex == asynRecordTMOT) {
      *precision = 4;
      return(0);
   }
   recGblGetPrec(paddr, precision);
   return(0);
}


static void monitor(asynRecord *pasynRec)
{
    /* Called when record processes for I/O operation */
    unsigned short  monitor_mask;
    asynRecPvt* pasynRecPvt = pasynRec->dpvt;

    monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
    
    if (pasynRec->ifmt == asynFMT_ASCII)
       db_post_events(pasynRec,pasynRec->ainp, monitor_mask);
    else
       db_post_events(pasynRec, pasynRec->iptr, monitor_mask);

    POST_IF_NEW(nrrd);
    POST_IF_NEW(nord);
    POST_IF_NEW(nowt);
    POST_IF_NEW(nawt);
    POST_IF_NEW(spr);
    POST_IF_NEW(ucmd);
    POST_IF_NEW(acmd);
}


static void monitorStatus(asynRecord *pasynRec)
{
    /* Called to update trace and connect fields. */
    unsigned short  monitor_mask;
    asynRecPvt* pasynRecPvt = pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    int trace_mask;
    FILE *traceFd;
    int yesNo;

    monitor_mask = DBE_VALUE | DBE_LOG;
    
    trace_mask = pasynTrace->getTraceMask(pasynUser);
    pasynRec->tb0 = (trace_mask & ASYN_TRACE_ERROR)    ? 1 : 0;
    pasynRec->tb1 = (trace_mask & ASYN_TRACEIO_DEVICE) ? 1 : 0;
    pasynRec->tb2 = (trace_mask & ASYN_TRACEIO_FILTER) ? 1 : 0;
    pasynRec->tb3 = (trace_mask & ASYN_TRACEIO_DRIVER) ? 1 : 0;
    pasynRec->tb4 = (trace_mask & ASYN_TRACE_FLOW)     ? 1 : 0;
    trace_mask = pasynTrace->getTraceIOMask(pasynUser);
    pasynRec->tib0 = (trace_mask & ASYN_TRACEIO_ASCII)  ? 1 : 0;
    pasynRec->tib1 = (trace_mask & ASYN_TRACEIO_ESCAPE) ? 1 : 0;
    pasynRec->tib2 = (trace_mask & ASYN_TRACEIO_HEX)    ? 1 : 0;

    pasynManager->isAutoConnect(pasynUser,&yesNo);
    pasynRec->auct = yesNo;
    pasynManager->isConnected(pasynUser,&yesNo);
    pasynRec->cnct = yesNo;
    pasynManager->isEnabled(pasynUser,&yesNo);
    pasynRec->enbl = yesNo;
    pasynRec->tsiz = pasynTrace->getTraceIOTruncateSize(pasynUser);
    traceFd        = pasynTrace->getTraceFile(pasynUser);
    POST_IF_NEW(tb0);
    POST_IF_NEW(tb1);
    POST_IF_NEW(tb2);
    POST_IF_NEW(tb3);
    POST_IF_NEW(tb4);
    POST_IF_NEW(tib0);
    POST_IF_NEW(tib1);
    POST_IF_NEW(tib2);
    POST_IF_NEW(tsiz);
    if (traceFd != pasynRecPvt->old.traceFd) {
       pasynRecPvt->old.traceFd = traceFd;
       /* Some other thread changed the trace file, we can't know file name */
       strcpy(pasynRec->tfil, "Unknown");
       db_post_events(pasynRec, pasynRec->tfil, monitor_mask);
    }
    POST_IF_NEW(auct);
    POST_IF_NEW(cnct);
    POST_IF_NEW(enbl);
}

static asynStatus connectDevice(asynRecord *pasynRec)
{
    asynInterface *pasynInterface;
    asynRecPvt *pasynRecPvt=pasynRec->dpvt;
    asynUser *pasynUser = pasynRecPvt->pasynUser;
    asynStatus status;
    unsigned short monitor_mask = DBE_VALUE | DBE_LOG;

    resetError(pasynRec);
    /* Disconnect any connected device.  Ignore error if there
     * is no device currently connected.
     */
    pasynManager->exceptionCallbackRemove(pasynUser);
    pasynManager->disconnect(pasynUser);
    /*set all option fields to unknown*/
    pasynRec->baud = 0; POST_IF_NEW(baud);
    pasynRec->prty = 0; POST_IF_NEW(prty);
    pasynRec->sbit = 0; POST_IF_NEW(sbit);
    pasynRec->dbit = 0; POST_IF_NEW(dbit);
    pasynRec->fctl = 0; POST_IF_NEW(fctl);

    /* Connect to the new device */
    status = pasynManager->connectDevice(pasynUser,pasynRec->port,
                                         pasynRec->addr);
    if (status!=asynSuccess) {
        reportError(pasynRec, COMM_ALARM, MAJOR_ALARM,
                    "Error connecting to device",
                    "connect error, status=%d, %s",
                    status, pasynUser->errorMessage);
        return(status);
    }

    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        reportError(pasynRec, COMM_ALARM, MAJOR_ALARM,
                    "Error finding common interface",
                    "error finding common interface, %s",
                    pasynUser->errorMessage);
        return(asynError);
    }
    pasynRecPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pasynRecPvt->asynCommonPvt = pasynInterface->drvPvt;

    /* Get asynOctet interface */
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        reportError(pasynRec, COMM_ALARM, MAJOR_ALARM,
                    "Error finding octet interface",
                    "error finding octet interface, %s",
                    pasynUser->errorMessage);
        return(asynError);
    }
    pasynRecPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pasynRecPvt->asynOctetPvt = pasynInterface->drvPvt;

    /* Get asynGpib interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynGpibType,1);
    if(pasynInterface) {
        /* This device has an asynGpib interface, not serial or socket */
        pasynRecPvt->pasynGpib = (asynGpib *)pasynInterface->pinterface;
        pasynRecPvt->asynGpibPvt = pasynInterface->drvPvt;
    }

    /* Add exception callback */
    pasynManager->exceptionCallbackAdd(pasynUser, exceptCallback);
    return(asynSuccess);
}

static void GPIB_command(asynUser *pasynUser)
{
   /* This function handles GPIB-specific commands */
   asynRecPvt *pasynRecPvt=pasynUser->userPvt;
   asynRecord *pasynRec = pasynRecPvt->prec;
   asynStatus status;
   int     nbytesTransfered;
   char    cmd_char=0;
   char    acmd[6];

   /* See asynRecord.dbd for definitions of constants gpibXXXX_Abcd */
   if (pasynRec->ucmd != gpibUCMD_None)
   {
      switch (pasynRec->ucmd)
      {
        case gpibUCMD_Device_Clear__DCL_:
            cmd_char = 20;
            break;
        case gpibUCMD_Local_Lockout__LL0_:
            cmd_char = 17;
            break;
        case gpibUCMD_Serial_Poll_Disable__SPD_:
            cmd_char = 25;
            break;
        case gpibUCMD_Serial_Poll_Enable__SPE_:
            cmd_char = 24;
            break;
        case gpibUCMD_Unlisten__UNL_: 
            cmd_char = 63;
            break;
        case gpibUCMD_Untalk__UNT_:
            cmd_char = 95;
            break;
      }
      status = pasynRecPvt->pasynGpib->universalCmd(pasynRecPvt->asynGpibPvt, 
                                             pasynRecPvt->pasynUser, cmd_char);
      if (status)
         /* Something is wrong if we couldn't write */
         reportError(pasynRec, WRITE_ALARM, MAJOR_ALARM,
                     "GPIB Universal Command write error",
                     "error in GPIB Universal command, %s", 
                     pasynUser->errorMessage);
   }

   /* See if an Addressed Command is to be done */
   if (pasynRec->acmd != gpibACMD_None)
   {
      acmd[0] = 95; /* Untalk */
      acmd[1] = 63; /* Unlisten */
      acmd[2] = pasynRec->addr + LADBASE;  /* GPIB address + Listen Base */
      acmd[4] = 95; /* Untalk */
      acmd[5] = 63; /* Unlisten */
      switch (pasynRec->acmd)
      {
        case gpibACMD_Group_Execute_Trig___GET_:
            acmd[3] = 8;
            break;
        case gpibACMD_Go_To_Local__GTL_:
            acmd[3] = 1;
            break;
        case gpibACMD_Selected_Dev__Clear__SDC_:
            acmd[3] = 4;
            break;
        case gpibACMD_Take_Control__TCT_:
            /* This command requires Talker Base */
            acmd[2] = pasynRec->addr + TADBASE;
            acmd[3] = 9;
            break;
        case gpibACMD_Serial_Poll:
            /* Serial poll. Requires 3 operations */
            /* Serial Poll Enable */
            cmd_char = IBSPE;
            status = pasynRecPvt->pasynOctet->write(pasynRecPvt->asynGpibPvt, 
                        pasynRecPvt->pasynUser, &cmd_char, 1,&nbytesTransfered);
            if (status!=asynSuccess || nbytesTransfered!=1)
                /* Something is wrong if we couldn't write */
                reportError(pasynRec, WRITE_ALARM, MAJOR_ALARM,
                            "GPIB Serial Poll write error",
                            "error in GPIB Serial Poll write, %s", 
                            pasynUser->errorMessage);
            /* Read the response byte  */
            status = pasynRecPvt->pasynOctet->read(pasynRecPvt->asynGpibPvt, 
                  pasynRecPvt->pasynUser, (char *)&pasynRec->spr,
                  1,&nbytesTransfered);
            if (status!=asynSuccess || nbytesTransfered!=1) 
                /* Something is wrong if we couldn't read */
                reportError(pasynRec, READ_ALARM, MAJOR_ALARM,
                            "GPIB Serial Poll read error",
                            "error in GPIB Serial Poll read, %s", 
                            pasynUser->errorMessage);
            /* Serial Poll Disable */
            cmd_char = IBSPD;
            status = pasynRecPvt->pasynOctet->write(pasynRecPvt->asynGpibPvt, 
                        pasynRecPvt->pasynUser, &cmd_char, 1,&nbytesTransfered);
            if (status!=asynSuccess || nbytesTransfered!=1)
                /* Something is wrong if we couldn't write */
                reportError(pasynRec, WRITE_ALARM, MAJOR_ALARM,
                            "GPIB Serial Poll Disable write error",
                            "error in GPIB Serial Poll disable write, %s", 
                            pasynUser->errorMessage);
            return;
      }
      status = pasynRecPvt->pasynGpib->addressedCmd(pasynRecPvt->asynGpibPvt, 
                                             pasynRecPvt->pasynUser, acmd, 6);
      if (status)
         /* Something is wrong if we couldn't write */
         reportError(pasynRec, WRITE_ALARM, MAJOR_ALARM,
                     "GPIB Address Commmand write error",
                     "error in GPIB Addressed Command write, %s", 
                     pasynUser->errorMessage);
   }
}

static void asynCallback(asynUser *pasynUser)
{
   asynRecPvt *pasynRecPvt=pasynUser->userPvt;
   asynRecord *pasynRec = pasynRecPvt->prec;
  
   asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s: asynCallback, state=%d\n",
        pasynRec->name, pasynRecPvt->state);
   switch(pasynRecPvt->state) {
      case stateIdle:
         /* This happens when TMOD=NoI/O */
         break;    
      case stateInitalIO:
         getOptions(pasynUser);
         /* no break */
      case stateIO:
         performIO(pasynUser); 
         break;
      case stateSetOption: 
         setOption(pasynUser); 
         break;
      case stateGetOption:
         getOptions(pasynUser); 
         break;
      case stateConnect:
          if(pasynRec->cnct) {
              pasynRecPvt->pasynCommon->connect(
                  pasynRecPvt->asynCommonPvt,pasynUser);
          } else {
               pasynRecPvt->pasynCommon->disconnect(
                   pasynRecPvt->asynCommonPvt,pasynUser);
          }
          break;
      default:
          printf("%s asynCallback illegal state %d\n",
                pasynRec->name,pasynRecPvt->state);
          return;
   }
   dbScanLock( (dbCommon*) pasynRec);
   process(pasynRec);
   pasynRecPvt->state = stateIdle;
   dbScanUnlock( (dbCommon*) pasynRec);
}

static void exceptCallback(asynUser *pasynUser,asynException exception)
{
   asynRecPvt *pasynRecPvt=pasynUser->userPvt;
   asynRecord *pasynRec = pasynRecPvt->prec;
   int        callLock = interruptAccept;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,
        "%s: exception %d\n",
        pasynRec->name,(int)exception);
    if(callLock) dbScanLock( (dbCommon*) pasynRec);
    /* There has been a changed in connect or enable status */
    monitorStatus(pasynRec);
    if(callLock) dbScanUnlock( (dbCommon*) pasynRec);
}

static void performIO(asynUser *pasynUser)
{
   asynRecPvt *pasynRecPvt=pasynUser->userPvt;
   asynRecord *pasynRec = pasynRecPvt->prec;
   asynStatus status;
   int        nbytesTransfered;
   char    *inptr;
   char    *outptr;
   int     inlen;
   int     nread;
   int     nwrite;
   int     eoslen;
   char eos[EOS_SIZE];

   resetError(pasynRec);
   pasynUser->timeout = pasynRec->tmot;

   /* See if the record is processing because a GPIB Universal or Addressed 
    * Command is being done */
   if ((pasynRec->ucmd != gpibUCMD_None) || (pasynRec->acmd != gpibACMD_None)) {
      if (pasynRecPvt->pasynGpib == NULL) {
         reportError(pasynRec, COMM_ALARM, MAJOR_ALARM,
                     "GPIB operation, not GPIB device",
                     "GPIB operation but not GPIB interface, port=%s",
                     pasynRec->port);
      } else {
         GPIB_command(pasynUser);
      }
      pasynRec->acmd = gpibACMD_None;  /* Reset to no Addressed Command */
      pasynRec->ucmd = gpibUCMD_None;  /* Reset to no Universal Command */
      return;
   }

   if (pasynRec->ofmt == asynFMT_ASCII) {
      /* ASCII output mode */
      /* Translate escape sequences */
      nwrite = dbTranslateEscape(pasynRecPvt->outbuff, pasynRec->aout);
      outptr = pasynRecPvt->outbuff;
   } else if (pasynRec->ofmt == asynFMT_Hybrid) {
      /* Hybrid output mode */
      /* Translate escape sequences */
      nwrite = dbTranslateEscape(pasynRecPvt->outbuff, pasynRec->optr);
      outptr = pasynRecPvt->outbuff;
   } else {   
      /* Binary output mode */
      nwrite = pasynRec->nowt;
      outptr = pasynRec->optr;
   }
   /* If not binary mode, append the terminator */
   if (pasynRec->ofmt != asynFMT_Binary) {
      eoslen = dbTranslateEscape(eos, pasynRec->oeos);
      /* Make sure there is room for terminator */
      if ((nwrite + eoslen) < pasynRec->omax) {
         strncat(outptr, eos, eoslen);
         nwrite += eoslen;
      }
   }

   if (pasynRec->ifmt == asynFMT_ASCII) {
      /* ASCII input mode */
      inptr = pasynRec->ainp;
      inlen = sizeof(pasynRec->ainp);
   } else {
      /* Binary or Hybrid input mode */
      inptr = pasynRec->iptr;
      inlen = pasynRec->imax;
   }
   if (pasynRec->nrrd != 0)
      nread = pasynRec->nrrd;
   else
      nread = inlen;

   if ((pasynRec->tmod == asynTMOD_Flush) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
      /* Flush the input buffer */
      pasynRecPvt->pasynOctet->flush(pasynRecPvt->asynOctetPvt, pasynUser);
   }

   if ((pasynRec->tmod == asynTMOD_Write) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
      /* Send the message */ 
      nbytesTransfered = 0;
      status = pasynRecPvt->pasynOctet->write(pasynRecPvt->asynOctetPvt, 
                   pasynUser, outptr, nwrite,&nbytesTransfered);
      pasynRec->nawt = nbytesTransfered;
      if (status!=asynSuccess || nbytesTransfered != nwrite) 
         /* Something is wrong if we couldn't write everything */
         reportError(pasynRec, WRITE_ALARM, MAJOR_ALARM,
                     "Write error",
                     "write error in performIO, nout=%d, %s", 
                     nbytesTransfered, pasynUser->errorMessage);
   }
    
   if ((pasynRec->tmod == asynTMOD_Read) ||
       (pasynRec->tmod == asynTMOD_Write_Read)) {
      /* Set the input buffer to all zeros */
      memset(inptr, 0, inlen);
      /* Read the message  */
      if (pasynRec->ifmt == asynFMT_Binary) {
         eos[0] = '\0';
         eoslen = 0;
      } else {
         /* ASCII or Hybrid mode */
         eoslen = dbTranslateEscape(eos, pasynRec->ieos);
      }
      status = pasynRecPvt->pasynOctet->setEos(pasynRecPvt->asynOctetPvt, 
                                        pasynUser, eos, eoslen);
      if (status) 
         /* Something is wrong if we didn't get any response */
         reportError(pasynRec, READ_ALARM, MAJOR_ALARM,
                     "Error setting EOS",
                     "error setting EOS in performIO, %s", 
                     pasynUser->errorMessage);
      nbytesTransfered = 0;
      status = pasynRecPvt->pasynOctet->read(pasynRecPvt->asynOctetPvt, 
                   pasynUser, inptr, nread,&nbytesTransfered);

      asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, inptr, nbytesTransfered,
            "%s: inlen=%d, status=%d, ninp=%d\n", pasynRec->name, inlen, 
            status, nbytesTransfered);
      if (status!=asynSuccess) {
         if (nbytesTransfered == 0) {
            /* Nothing transfered */
            reportError(pasynRec, READ_ALARM, MAJOR_ALARM,
                        "Timeout, nothing received",
                        "timeout in performIO, ninp=%d, %s", 
                        nbytesTransfered, pasynUser->errorMessage);
         } else {
            reportError(pasynRec, READ_ALARM, MINOR_ALARM,
                        "Timeout",
                        "timeout in performIO, ninp=%d, %s", 
                        nbytesTransfered, pasynUser->errorMessage);
         }
      }
      /* Check for input buffer overflow */
      if ((pasynRec->ifmt == asynFMT_ASCII) && 
          (nbytesTransfered >= (int)sizeof(pasynRec->ainp))) {
         reportError(pasynRec, READ_ALARM, MINOR_ALARM,
                     "Buffer overflow",
                     "buffer overflow in performIO, ninp=%d, %s", 
                     nbytesTransfered, pasynUser->errorMessage);
         /* terminate response with \0 */
         inptr[sizeof(pasynRec->ainp)-1] = '\0';
      }
      else if ((pasynRec->ifmt == asynFMT_Hybrid) && 
               (nbytesTransfered >= pasynRec->imax)) {
         reportError(pasynRec, READ_ALARM, MINOR_ALARM,
                     "Buffer overflow",
                     "buffer overflow in performIO, ninp=%d, %s", 
                     nbytesTransfered, pasynUser->errorMessage);
         /* terminate response with \0 */
         inptr[pasynRec->imax-1] = '\0';
      }
      else if ((pasynRec->ifmt == asynFMT_Binary) && 
               (nbytesTransfered > pasynRec->imax)) {
         /* This should not be able to happen */
         reportError(pasynRec, READ_ALARM, MAJOR_ALARM,
                     "Buffer overflow",
                     "buffer overflow in performIO, ninp=%d, %s", 
                     nbytesTransfered, pasynUser->errorMessage);
      }
      else if (pasynRec->ifmt != asynFMT_Binary) {
         /* Not binary and no input buffer overflow has occurred */
         /* Add null at end of input.  This is safe because of tests above */
         inptr[nbytesTransfered] = '\0';
         /* If the string is terminated by the requested terminator */
         /* remove it. */
         if ((eoslen > 0) && (nbytesTransfered >= eoslen) && 
                             (strcmp(&inptr[nbytesTransfered-eoslen], eos) == 0)) {
            memset(&inptr[nbytesTransfered-eoslen], 0, eoslen);
         }
      } 
      pasynRec->nord = nbytesTransfered; /* Number of bytes read */
   }
}

static void setOption(asynUser *pasynUser)
{
   asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
   asynRecord *pasynRec = pasynRecPvt->prec;

   asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "%s: setOptionCallback port=%s, addr=%d index=%d\n",
             pasynRec->name, pasynRec->port, pasynRec->addr, 
             pasynRecPvt->fieldIndex);

   switch (pasynRecPvt->fieldIndex) {
   
   case asynRecordBAUD:
      pasynRecPvt->pasynCommon->setOption(pasynRecPvt->asynCommonPvt, 
                                          pasynRecPvt->pasynUser, "baud", 
                                          baud_choices[pasynRec->baud]);
      break;
   case asynRecordPRTY:
      pasynRecPvt->pasynCommon->setOption(pasynRecPvt->asynCommonPvt, 
                                          pasynRecPvt->pasynUser, "parity", 
                                          parity_choices[pasynRec->prty]);
      break;
   case asynRecordSBIT:
      pasynRecPvt->pasynCommon->setOption(pasynRecPvt->asynCommonPvt, 
                                          pasynRecPvt->pasynUser, "stop", 
                                          stop_bit_choices[pasynRec->sbit]);
      break;
   case asynRecordDBIT:
      pasynRecPvt->pasynCommon->setOption(pasynRecPvt->asynCommonPvt, 
                                          pasynRecPvt->pasynUser, "bits", 
                                          stop_bit_choices[pasynRec->dbit]);
      break;
   case asynRecordFCTL:
      pasynRecPvt->pasynCommon->setOption(pasynRecPvt->asynCommonPvt, 
                                          pasynRecPvt->pasynUser, "clocal", 
                                          flow_control_choices[pasynRec->fctl]);
      break;
   }
}

static void getOptions(asynUser *pasynUser)
{
    asynRecPvt *pasynRecPvt = (asynRecPvt *) pasynUser->userPvt;
    asynRecord *pasynRec = pasynRecPvt->prec;
    char optbuff[OPT_SIZE];
    int i;
    unsigned short monitor_mask;

    asynPrint(pasynUser,ASYN_TRACE_FLOW,
           "%s: getOptionCallback() port=%s, addr=%d\n",
           pasynRec->name, pasynRec->port, pasynRec->addr);
    /* Get port options */
    pasynRecPvt->pasynCommon->getOption(pasynRecPvt->asynCommonPvt, pasynUser, 
                                     "baud", optbuff, OPT_SIZE);
    for (i=0; i<NUM_BAUD_CHOICES; i++)
       if (strcmp(optbuff, baud_choices[i]) == 0) pasynRec->baud = i;

    pasynRecPvt->pasynCommon->getOption(pasynRecPvt->asynCommonPvt, pasynUser, 
                                     "parity", optbuff, OPT_SIZE);
    for (i=0; i<NUM_PARITY_CHOICES; i++)
       if (strcmp(optbuff, parity_choices[i]) == 0) pasynRec->prty = i;

    pasynRecPvt->pasynCommon->getOption(pasynRecPvt->asynCommonPvt, pasynUser, 
                                     "stop", optbuff, OPT_SIZE);
    for (i=0; i<NUM_SBIT_CHOICES; i++)
       if (strcmp(optbuff, stop_bit_choices[i]) == 0) pasynRec->sbit = i;

    pasynRecPvt->pasynCommon->getOption(pasynRecPvt->asynCommonPvt, pasynUser, 
                                     "bits", optbuff, OPT_SIZE);
    for (i=0; i<NUM_DBIT_CHOICES; i++)
       if (strcmp(optbuff, data_bit_choices[i]) == 0) pasynRec->dbit = i;

    pasynRecPvt->pasynCommon->getOption(pasynRecPvt->asynCommonPvt, pasynUser, 
                                     "clocal", optbuff, OPT_SIZE);
    for (i=0; i<NUM_FLOW_CHOICES; i++)
       if (strcmp(optbuff, flow_control_choices[i]) == 0) pasynRec->fctl = i;

    monitor_mask = DBE_VALUE | DBE_LOG;
    
    POST_IF_NEW(baud);
    POST_IF_NEW(prty);
    POST_IF_NEW(sbit);
    POST_IF_NEW(dbit);
    POST_IF_NEW(fctl);
}

static void reportError(asynRecord *pasynRec, int status, int severity,
                 const char *errorMessage, const char *pformat, ...)
{
   asynRecPvt* pasynRecPvt = pasynRec->dpvt;
   asynUser *pasynUser = pasynRecPvt->pasynUser;
   unsigned short monitor_mask;
   char buffer[200];
   va_list  pvar;

   va_start(pvar,pformat);
   vsprintf(buffer, pformat, pvar);
   pasynTrace->print(pasynUser, ASYN_TRACE_ERROR, "%s: %s\n", 
                     pasynRec->name, buffer);
   va_end(pvar);
 
   strcpy(pasynRec->errs, errorMessage);
   if (strcmp(pasynRec->errs, pasynRecPvt->old.errs) != 0) {
       strcpy(pasynRecPvt->old.errs, pasynRec->errs);
       monitor_mask = DBE_VALUE | DBE_LOG;
       db_post_events(pasynRec, pasynRec->errs, monitor_mask);
   }
   recGblSetSevr(pasynRec, status, severity);
}

static void resetError(asynRecord *pasynRec)
{
   asynRecPvt* pasynRecPvt = pasynRec->dpvt;
   unsigned short monitor_mask;

   strcpy(pasynRec->errs, "");
   if (strcmp(pasynRec->errs, pasynRecPvt->old.errs) != 0) {
       strcpy(pasynRecPvt->old.errs, pasynRec->errs);
       monitor_mask = DBE_VALUE | DBE_LOG;
       db_post_events(pasynRec, pasynRec->errs, monitor_mask);
   }
}
