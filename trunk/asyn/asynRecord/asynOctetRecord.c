/* asynOctetRecord.c - Record Support Routines for asynOctet record */
/*
 * Based on earlier asynOctetRecord.c and serialRecord.c.  
 * - Contains most fields from those records with the following differences:
 *   - IDEL and EOS have been renamed IEOS, and changed from integer to string
 *     data type to allow multi-character terminator
 *   - ODEL has been renamed OEOS, and changed from integer to string
 *     data type to allow multi-character terminator
 *   - Added the PORT field, so the asyn port can be dynamically changed.
 *   - The AOUT, IEOS and OEOS fields are passed through dbTranslateEscape
 *   - The record always post monitors on the input data (AINP or BINP) even 
 *     if the values are the same.  This is very useful for CA clients 
 *     (e.g. SNL programs) that use monitors on these fields to process a 
 *     response.
 *   - The units of the TMOT field have changed from milliseconds to seconds,
 *     and the data type has changed from int to double.
 *
 *      Author:    Mark Rivers
 *      Date:      3/8/2004
 *
 * Modification Log:
 * -----------------
 * .01  08-Mar-2004 mlr  Based on gpibRecord and serialRecord.
 */


#include        <string.h>
#include        <stdio.h>
#include        <stdlib.h>

#include        <asynDriver.h>
#include        <asynGpibDriver.h>
#include        <alarm.h>
#include        <dbDefs.h>
#include        <dbEvent.h>
#include        <dbAccess.h>
#include        <dbFldTypes.h>
#include        <devSup.h>
#include        <drvSup.h>
#include        <errMdef.h>
#include        <recSup.h>
#include        <epicsString.h>
#include        <recGbl.h>
#include        <epicsExport.h>
#define GEN_SIZE_OFFSET
#include        "asynOctetRecord.h"
#undef GEN_SIZE_OFFSET


/* Create RSET - Record Support Entry Table*/
#define report NULL
#define initialize NULL
static long init_record(asynOctetRecord *pasynRec, int pass);
static long process(asynOctetRecord *pasynRec);
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

/* There are no header files for the following definition.  
 * Must be careful to make sure these following remains consistent with the 
 * source files
 */
int drvGenericSerialConfigure(char *portName,
                     char *ttyName,
                     unsigned int priority,
                     int openOnlyOnDisconnect);
static void  asynOctetCallback(asynUser *pasynUser);
static void  setPortCallback(asynUser *pasynUser);
static int   getOptions(asynOctetRecord *pasynRec);
static void  getPortCallback(asynUser *pasynUser);
static int   connectDevice(asynOctetRecord *pasynRec);
static void  monitor();
static void  GPIB_command(asynUser *pasynUser);

struct rset asynOctetRSET={
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
epicsExportAddress(rset, asynOctetRSET);

struct old_field_values {              /* Used in monitor() */
    int   nowt;               
    int   nrrd;               
    int   nord;
    int   spr;               
    int   ucmd;
    int   acmd;
    int   addr;
};

#define REMEMBER_STATE(FIELD) pPvt->old.FIELD = pasynRec->FIELD
#define POST_IF_NEW(FIELD) \
    if (pasynRec->FIELD != pPvt->old.FIELD) { \
        db_post_events(pasynRec, &pasynRec->FIELD, monitor_mask); \
        pPvt->old.FIELD = pasynRec->FIELD; }

typedef struct asynRecPvt {
    asynOctetRecord *prec;      /* Pointer to record */
    asynUser *pasynUser;
    asynOctet *pasynOctet;
    void *asynOctetPvt;
    asynCommon *pasynCommon;
    void *asynCommonPvt;
    asynGpib *pasynGpib;
    void *asynGpibPvt;
    char *outbuff;
    struct old_field_values old;
} asynRecPvt;

typedef struct asynPortPvt {
    asynOctetRecord *prec;      /* Pointer to record */
    int fieldIndex;
} asynPortPvt;

/* These really belong in a header file.
 * They used to be in drvHwGpibInterface.h */
#define TADBASE    0x40   /* offset to GPIB listen address 0 */
#define LADBASE    0x20   /* offset to GPIB talk address 0 */
#define SADBASE    0x60   /* offset to GPIB secondary address 0 */

/* These should be in a header file too */
#define NUM_BAUD_CHOICES 11
static char *baud_choices[NUM_BAUD_CHOICES]={"300","600","1200","2400","4800",
                                             "9600","19200","38400","57600",
                                             "115200","230400"};
#define NUM_PARITY_CHOICES 3
static char *parity_choices[NUM_PARITY_CHOICES]={"none","even","odd"};
#define NUM_DBIT_CHOICES 4
static char *data_bit_choices[NUM_DBIT_CHOICES]={"5","6","7","8"};
#define NUM_SBIT_CHOICES 2
static char *stop_bit_choices[NUM_SBIT_CHOICES]={"1","2"};
#define NUM_FLOW_CHOICES 2
static char *flow_control_choices[NUM_FLOW_CHOICES]={"Y","N"};

#define OPT_SIZE 80  /* Size of buffer for setting and getting port options */
#define EOS_SIZE 10  /* Size of buffer for EOS */


static long init_record(asynOctetRecord *pasynRec, int pass)
{
    asynRecPvt *pPvt;
    asynUser *pasynUser;
    int status;

    if (pass != 0) return(0);

    /* Allocate and initialize private structure used by this record */
    pPvt = (asynRecPvt*) calloc(1, sizeof(asynRecPvt)); 
    pasynRec->dpvt = pPvt;

    pPvt->prec = pasynRec;

    /* Initialize asyn, connect to device */
    pasynUser = pasynManager->createAsynUser(asynOctetCallback,0);
    pPvt->pasynUser = pasynUser;
    pasynUser->userPvt = pPvt;
    status = connectDevice(pasynRec);
    if (status) {
       printf("asynOctetRecord, error connecting to device, status=%d\n", 
               status);
    }

    /* Allocate the space for the binary/hybrid output and binary/hybrid 
     * input arrays */
    if (pasynRec->omax <= 0) pasynRec->omax=MAX_STRING_SIZE;
    if (pasynRec->imax <= 0) pasynRec->imax=MAX_STRING_SIZE;
    pasynRec->optr = (char *)calloc(pasynRec->omax, sizeof(char));
    pasynRec->iptr = (char *)calloc(pasynRec->imax, sizeof(char));
    pPvt->outbuff =  (char *)calloc(pasynRec->omax, sizeof(char));

    return(0);
}


static int connectDevice(asynOctetRecord *pasynRec)
{
    asynInterface *pasynInterface;
    asynRecPvt *pPvt=pasynRec->dpvt;
    asynUser *pasynUser = pPvt->pasynUser;
    int status;

    /* Disconnect any connected device.  Ignore error if there
     * is no device currently connected.
     */
    status = pasynManager->disconnect(pasynUser);

    /* Connect to the new device */
    status = pasynManager->connectDevice(pasynUser,pasynRec->port,
                                         pasynRec->addr);
    if (status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                  "asynOctetRecord connect error, status=%d, %s\n",
                  status, pasynUser->errorMessage);
        return(status);
    }

    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser,asynCommonType,1);
    if(!pasynInterface) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, "%s %s\n",
                  asynCommonType, pasynUser->errorMessage);
        return(status);
    }
    pPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pPvt->asynCommonPvt = pasynInterface->drvPvt;

    /* Get asynOctet interface */
    pasynInterface = pasynManager->findInterface(pasynUser,asynOctetType,1);
    if(!pasynInterface) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s %s\n",
            asynOctetType,pasynUser->errorMessage);
        return(status);
    }
    pPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pPvt->asynOctetPvt = pasynInterface->drvPvt;

    /* Get asynGpib interface if it exists*/
    pasynInterface = pasynManager->findInterface(pasynUser,asynGpibType,1);
    if(pasynInterface) {
        /* This device has an asynGpib interface, not serial or socket */
        pPvt->pasynGpib = (asynGpib *)pasynInterface->pinterface;
        pPvt->asynGpibPvt = pasynInterface->drvPvt;
    }
    getOptions(pasynRec);
    return(asynSuccess);
}


static long cvt_dbaddr(struct dbAddr *paddr)
{
   asynOctetRecord *pasynRec=(asynOctetRecord *)paddr->precord;
   int fieldIndex = dbGetFieldIndex(paddr);

   if (fieldIndex == asynOctetRecordBOUT) {
      paddr->pfield = (void *)(pasynRec->optr);
      paddr->no_elements = pasynRec->omax;
      paddr->field_type = DBF_CHAR;
      paddr->field_size = sizeof(char);
      paddr->dbr_field_type = DBF_CHAR;
   } else if (fieldIndex == asynOctetRecordBINP) {
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
   asynOctetRecord *pasynRec=(asynOctetRecord *)paddr->precord;

   *no_elements =  pasynRec->nord;
   *offset = 0;
   return(0);
}

static long put_array_info(struct dbAddr *paddr, long nNew)
{
   asynOctetRecord *pasynRec=(asynOctetRecord *)paddr->precord;

   pasynRec->nowt = nNew;
   if (pasynRec->nowt > pasynRec->omax) pasynRec->nowt = pasynRec->omax;
   return(0);
}

static long get_precision(struct dbAddr *paddr, long *precision)
{
   int fieldIndex = dbGetFieldIndex(paddr);

   *precision = 0;
   if (fieldIndex == asynOctetRecordTMOT) {
      *precision = 4;
      return(0);
   }
   recGblGetPrec(paddr, precision);
   return(0);
}


static long process(asynOctetRecord *pasynRec)
{
   asynRecPvt* pPvt = pasynRec->dpvt;

   /* If pact is FALSE then queue message to driver and return */
   if (!pasynRec->pact)
   {
      /* Remember current state of fields for monitor() */
      REMEMBER_STATE(nrrd);
      REMEMBER_STATE(nord);
      REMEMBER_STATE(nowt);
      REMEMBER_STATE(spr);
      REMEMBER_STATE(ucmd);
      REMEMBER_STATE(acmd);
      /* Make sure nrrd and nowt are valid */
      if (pasynRec->nrrd > pasynRec->imax) pasynRec->nrrd = pasynRec->imax;
      if (pasynRec->nowt > pasynRec->omax) pasynRec->nowt = pasynRec->omax;
      pasynManager->queueRequest(pPvt->pasynUser, asynQueuePriorityLow, 0.0);
      pasynRec->pact = TRUE;
      return(0);
   }

   /* pact was TRUE, so we were called from asynOctetCallback.
    * The I/O is complete, so finish up.
    */

   recGblGetTimeStamp(pasynRec);

   /* check event list */
   monitor(pasynRec);
   /* process the forward scan link record */
   recGblFwdLink(pasynRec);

   pasynRec->pact=FALSE;
   return(0);
}



static void monitor(asynOctetRecord *pasynRec)
{
    unsigned short  monitor_mask;
    asynRecPvt* pPvt = pasynRec->dpvt;

    monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
    
    if (pasynRec->ifmt == asynOctetFMT_ASCII)
       db_post_events(pasynRec,pasynRec->ainp, monitor_mask);
    else
       db_post_events(pasynRec, pasynRec->iptr, monitor_mask);

    POST_IF_NEW(nrrd);
    POST_IF_NEW(nord);
    POST_IF_NEW(nowt);
    POST_IF_NEW(spr);
    POST_IF_NEW(ucmd);
    POST_IF_NEW(acmd);
    POST_IF_NEW(addr);
}

/* special() is called when any of the serial port parameters (baud rate, 
 * parity, etc.) are changed.  
 * It queues a request to write the new port parameters
 */
static long special(struct dbAddr *paddr, int after)
{
    asynOctetRecord *pasynRec=(asynOctetRecord *)paddr->precord;
    int fieldIndex = dbGetFieldIndex(paddr);
    asynRecPvt *pPvt=pasynRec->dpvt;
    asynUser *pasynUser=pPvt->pasynUser;
    asynUser *pasynUserPort;
    asynPortPvt *pPortPvt;
    asynStatus status=asynSuccess;
    unsigned short monitor_mask;

    if (!after) return(0);

    switch (fieldIndex) {

       case asynOctetRecordSOCK:
          strcpy(pasynRec->port, pasynRec->sock);
          pasynRec->addr = 0;
          drvGenericSerialConfigure(pasynRec->port, pasynRec->sock, 0, 0);
          monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
          db_post_events(pasynRec, pasynRec->port, monitor_mask);
          db_post_events(pasynRec, &pasynRec->addr, monitor_mask);
          /* Now do same thing as when port or addr change */
       case asynOctetRecordPORT: 
       case asynOctetRecordADDR: 
          /* If the PORT or ADDR fields changed then reconnect to new device */
          status = connectDevice(pasynRec);
          asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "asynOctetRecord special() port=%s, addr=%d, connect status=%d\n",
             pasynRec->port, pasynRec->addr, status);
          break;
       case asynOctetRecordBAUD: 
       case asynOctetRecordPRTY: 
       case asynOctetRecordDBIT: 
       case asynOctetRecordSBIT: 
       case asynOctetRecordFCTL: 
          /* One of the serial port parameters */
          pasynUserPort = pasynManager->createAsynUser(setPortCallback, 0);
          pasynManager->connectDevice(pasynUserPort, pasynRec->port, 
                                      pasynRec->addr);
          pPortPvt = (asynPortPvt *) calloc(1, sizeof(asynPortPvt));
          pasynUserPort->userPvt = pPortPvt;
          pPortPvt->prec = pasynRec;
          pPortPvt->fieldIndex = fieldIndex;
          asynPrint(pasynUser,ASYN_TRACE_FLOW,
              "asynOctetRecord special() port=%s, addr=%d queuing request\n",
              pasynRec->port, pasynRec->addr);
          status = pasynManager->queueRequest(pasynUserPort, 
                                              asynQueuePriorityLow, 0.0);
          break;
       case asynOctetRecordTB0: 
       case asynOctetRecordTB1: 
       case asynOctetRecordTB2: 
       case asynOctetRecordTB3: 
       case asynOctetRecordTB4: 
          pasynTrace->setTraceMask(pasynUser, 
                                   pasynRec->tb0    | pasynRec->tb1 << 1 |
                                   pasynRec->tb2<<2 | pasynRec->tb3 << 3 |
                                   pasynRec->tb4 << 4);
          break;
       case asynOctetRecordTIB0: 
       case asynOctetRecordTIB1: 
       case asynOctetRecordTIB2: 
          pasynTrace->setTraceIOMask(pasynUser, 
                                     pasynRec->tib0 | pasynRec->tib1 << 1 |
                                     pasynRec->tib2 << 2);
          break;
    }
    return(status);
}

static void setPortCallback(asynUser *pasynUser)
{
   asynPortPvt *pPortPvt = (asynPortPvt *) pasynUser->userPvt;
   asynOctetRecord *pasynRec = pPortPvt->prec;
   asynRecPvt *pPvt = pasynRec->dpvt;

   dbScanLock( (struct dbCommon*) pasynRec);
   asynPrint(pasynUser,ASYN_TRACE_FLOW,
             "asynOctetRecord setPortCallback port=%s, addr=%d index=%d\n",
             pasynRec->port, pasynRec->addr, pPortPvt->fieldIndex);

   switch (pPortPvt->fieldIndex) {
   
   case asynOctetRecordBAUD:
      pPvt->pasynCommon->setOption(pPvt->asynCommonPvt, pPvt->pasynUser, 
                                       "baud", baud_choices[pasynRec->baud]);
      break;
   case asynOctetRecordPRTY:
      pPvt->pasynCommon->setOption(pPvt->asynCommonPvt, pPvt->pasynUser, 
                                      "parity", parity_choices[pasynRec->prty]);
      break;
   case asynOctetRecordSBIT:
      pPvt->pasynCommon->setOption(pPvt->asynCommonPvt, pPvt->pasynUser, 
                                      "stop", stop_bit_choices[pasynRec->sbit]);
      break;
   case asynOctetRecordDBIT:
      pPvt->pasynCommon->setOption(pPvt->asynCommonPvt, pPvt->pasynUser, 
                                      "bits", stop_bit_choices[pasynRec->dbit]);
      break;
   case asynOctetRecordFCTL:
      pPvt->pasynCommon->setOption(pPvt->asynCommonPvt, pPvt->pasynUser, 
                                       "clocal", 
                                       flow_control_choices[pasynRec->fctl]);
      break;
   }

   /* Free the asynPortPvt and asynUser structures */
   free(pPortPvt);
   pasynManager->freeAsynUser(pasynUser);
   dbScanUnlock( (struct dbCommon*) pasynRec);
}


static int getOptions(asynOctetRecord *pasynRec)
{
    asynUser *pasynUserPort;
    asynPortPvt *pPortPvt;
    asynStatus status;

    /* Queue a request to get the port options */
    pasynUserPort = pasynManager->createAsynUser(getPortCallback, 0);
    status = pasynManager->connectDevice(pasynUserPort, 
                                         pasynRec->port, pasynRec->addr);
    if (status != asynSuccess) {
        asynPrint(pasynUserPort,ASYN_TRACE_ERROR,
           "asynOctetRecord getOptions port=%s, addr=%d, error=%d, %s\n",
           pasynRec->port, pasynRec->addr, status, pasynUserPort->errorMessage);
    }
    pPortPvt = (asynPortPvt *) calloc(1, sizeof(asynPortPvt));
    pasynUserPort->userPvt = pPortPvt;
    pPortPvt->prec = pasynRec;
    pasynManager->queueRequest(pasynUserPort, asynQueuePriorityLow, 0.0);
    asynPrint(pasynUserPort,ASYN_TRACE_FLOW,
           "asynOctetRecord getOptions() port=%s, addr=%d queued request\n",
           pasynRec->port, pasynRec->addr);
    return(asynSuccess);
}

static void getPortCallback(asynUser *pasynUserPort)
{
    asynPortPvt *pPortPvt = (asynPortPvt *) pasynUserPort->userPvt;
    asynOctetRecord *pasynRec = pPortPvt->prec;
    asynRecPvt *pPvt = pasynRec->dpvt;
    asynUser *pasynUser=pPvt->pasynUser;
    char optbuff[OPT_SIZE];
    int i;
    int trace_mask;
    unsigned short monitor_mask;

    if (interruptAccept) dbScanLock( (struct dbCommon*) pasynRec);
    asynPrint(pasynUserPort,ASYN_TRACE_FLOW,
           "asynOctetRecord getPortCallback() port=%s, addr=%d\n",
           pasynRec->port, pasynRec->addr);
    /* Get port options */
    pPvt->pasynCommon->getOption(pPvt->asynCommonPvt, pasynUser, 
                                     "baud", optbuff, OPT_SIZE);
    for (i=0; i<NUM_BAUD_CHOICES; i++)
       if (strcmp(optbuff, baud_choices[i]) == 0) pasynRec->baud = i;

    pPvt->pasynCommon->getOption(pPvt->asynCommonPvt, pasynUser, 
                                     "parity", optbuff, OPT_SIZE);
    for (i=0; i<NUM_PARITY_CHOICES; i++)
       if (strcmp(optbuff, parity_choices[i]) == 0) pasynRec->prty = i;

    pPvt->pasynCommon->getOption(pPvt->asynCommonPvt, pasynUser, 
                                     "stop", optbuff, OPT_SIZE);
    for (i=0; i<NUM_SBIT_CHOICES; i++)
       if (strcmp(optbuff, stop_bit_choices[i]) == 0) pasynRec->sbit = i;

    pPvt->pasynCommon->getOption(pPvt->asynCommonPvt, pasynUser, 
                                     "bits", optbuff, OPT_SIZE);
    for (i=0; i<NUM_DBIT_CHOICES; i++)
       if (strcmp(optbuff, data_bit_choices[i]) == 0) pasynRec->dbit = i;

    pPvt->pasynCommon->getOption(pPvt->asynCommonPvt, pasynUser, 
                                     "clocal", optbuff, OPT_SIZE);
    for (i=0; i<NUM_FLOW_CHOICES; i++)
       if (strcmp(optbuff, flow_control_choices[i]) == 0) pasynRec->fctl = i;

    trace_mask = pasynTrace->getTraceMask(pasynUser);
    pasynRec->tb0 = (trace_mask>>0) & 1;
    pasynRec->tb1 = (trace_mask>>1) & 1;
    pasynRec->tb2 = (trace_mask>>2) & 1;
    pasynRec->tb3 = (trace_mask>>3) & 1;
    pasynRec->tb4 = (trace_mask>>4) & 1;
    trace_mask = pasynTrace->getTraceIOMask(pasynUser);
    pasynRec->tib0 = (trace_mask>>0) & 1;
    pasynRec->tib1 = (trace_mask>>1) & 1;
    pasynRec->tib2 = (trace_mask>>2) & 1;

    monitor_mask = recGblResetAlarms(pasynRec) | DBE_VALUE | DBE_LOG;
    
    db_post_events(pasynRec, &pasynRec->baud, monitor_mask);
    db_post_events(pasynRec, &pasynRec->prty, monitor_mask);
    db_post_events(pasynRec, &pasynRec->sbit, monitor_mask);
    db_post_events(pasynRec, &pasynRec->dbit, monitor_mask);
    db_post_events(pasynRec, &pasynRec->fctl, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tb0, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tb1, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tb2, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tb3, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tb4, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tib0, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tib1, monitor_mask);
    db_post_events(pasynRec, &pasynRec->tib2, monitor_mask);

   /* Free the asynPortPvt and asynUser structures */
   free(pPortPvt);
   pasynManager->freeAsynUser(pasynUser);
   if (interruptAccept) dbScanUnlock( (struct dbCommon*) pasynRec);
}


static void asynOctetCallback(asynUser *pasynUser)
  /* This routine is called by the driver when the message gets to the head of
   * the queue.  It calls the driver to do the actual I/O and then calls
   * the record process() function again to signal that the I/O is complete
   */
{
   asynRecPvt *pPvt=pasynUser->userPvt;
   asynOctetRecord *pasynRec = pPvt->prec;
   int     nout;
   int     ninp;
   int     status;
   char    *inptr;
   char    *outptr;
   int     inlen;
   int     nread;
   int     nwrite;
   int     eoslen;
   char eos[EOS_SIZE];

   dbScanLock( (struct dbCommon*) pasynRec);
   pasynUser->timeout = pasynRec->tmot;

   /* See if the record is processing because a GPIB Universal or Addressed 
    * Command is being done */
   if ((pasynRec->ucmd != gpibUCMD_None) || (pasynRec->acmd != gpibACMD_None)) {
      if (pPvt->pasynGpib == NULL) {
         asynPrint(pasynUser, ASYN_TRACE_ERROR, 
            "GPIB specific operation but not GPIB interface, port=%s\n",
            pasynRec->port);
      } else {
         GPIB_command(pasynUser);
      }
      goto finish;
   }

   if (pasynRec->ofmt == asynOctetFMT_ASCII) {
      /* ASCII output mode */
      /* Translate escape sequences */
      nwrite = dbTranslateEscape(pPvt->outbuff, pasynRec->aout);
      outptr = pPvt->outbuff;
   } else if (pasynRec->ofmt == asynOctetFMT_Hybrid) {
      /* Hybrid output mode */
      /* Translate escape sequences */
      nwrite = dbTranslateEscape(pPvt->outbuff, pasynRec->optr);
      outptr = pPvt->outbuff;
   } else {   
      /* Binary output mode */
      nwrite = pasynRec->nowt;
      outptr = pasynRec->optr;
   }
   /* If not binary mode, append the terminator */
   if (pasynRec->ofmt != asynOctetFMT_Binary) {
      eoslen = dbTranslateEscape(eos, pasynRec->oeos);
      /* Make sure there is room for terminator */
      if ((nwrite + eoslen) < pasynRec->omax) {
         strncat(outptr, eos, eoslen);
         nwrite += eoslen;
      }
   }

   if (pasynRec->ifmt == asynOctetFMT_ASCII) {
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

   if ((pasynRec->tmod == asynOctetTMOD_Flush) ||
       (pasynRec->tmod == asynOctetTMOD_Write_Read)) {
      /* Flush the input buffer */
      pPvt->pasynOctet->flush(pPvt->asynOctetPvt, pasynUser);
   }

   if ((pasynRec->tmod == asynOctetTMOD_Write) ||
       (pasynRec->tmod == asynOctetTMOD_Write_Read)) {
      /* Send the message */
      nout = pPvt->pasynOctet->write(pPvt->asynOctetPvt, 
                                     pasynUser, outptr, nwrite);
      if (nout != nwrite) 
         /* Something is wrong if we couldn't write everything */
         recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
   }
    
   if ((pasynRec->tmod == asynOctetTMOD_Read) ||
       (pasynRec->tmod == asynOctetTMOD_Write_Read)) {
      /* Set the input buffer to all zeros */
      memset(inptr, 0, inlen);
      /* Read the message  */
      if (pasynRec->ifmt == asynOctetFMT_Binary) {
         eos[0] = '\0';
         eoslen = 0;
      } else {
         /* ASCII or Hybrid mode */
         eoslen = dbTranslateEscape(eos, pasynRec->ieos);
      }
      status = pPvt->pasynOctet->setEos(pPvt->asynOctetPvt, 
                                        pasynUser, eos, eoslen);
      if (status) 
         /* Something is wrong if we didn't get any response */
         recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
      ninp = pPvt->pasynOctet->read(pPvt->asynOctetPvt, 
                                    pasynUser, inptr, nread);

      asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, inptr, inlen,
            "asynOctetRecord: inlen=%d, ninp=%d\n", inlen, ninp);
      if (ninp <= 0) 
         /* Something is wrong if we didn't get any response */
         recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
      /* Check for input buffer overflow */
      if ((pasynRec->ifmt == asynOctetFMT_ASCII) && 
          (ninp >= sizeof(pasynRec->ainp))) {
         recGblSetSevr(pasynRec, READ_ALARM, MINOR_ALARM);
         /* terminate response with \0 */
         inptr[sizeof(pasynRec->ainp)-1] = '\0';
      }
      else if ((pasynRec->ifmt == asynOctetFMT_Hybrid) && 
               (ninp >= pasynRec->imax)) {
         recGblSetSevr(pasynRec, READ_ALARM, MINOR_ALARM);
         /* terminate response with \0 */
         inptr[pasynRec->imax-1] = '\0';
      }
      else if ((pasynRec->ifmt == asynOctetFMT_Binary) && 
               (ninp > pasynRec->imax)) {
         /* This should not be able to happen */
         recGblSetSevr(pasynRec, READ_ALARM, MINOR_ALARM);
      }
      else if (pasynRec->ifmt != asynOctetFMT_Binary) {
         /* Not binary and no input buffer overflow has occurred */
         /* Add null at end of input.  This is safe because of tests above */
         inptr[ninp] = '\0';
         /* If the string is terminated by the requested terminator */
         /* remove it. */
         if ((eoslen > 0) && (ninp >= eoslen) && 
                             (strcmp(&inptr[ninp-eoslen], eos) == 0)) {
            memset(&inptr[ninp-eoslen], 0, eoslen);
         }
      } 
      pasynRec->nord = ninp; /* Number of bytes read */
   }

   /* Call process, and unlock the record */
   finish:
   process(pasynRec);
   dbScanUnlock( (struct dbCommon*) pasynRec);
   return;
}


static void GPIB_command(asynUser *pasynUser)
{
   /* This function handles GPIB-specific commands */
   asynRecPvt *pPvt=pasynUser->userPvt;
   asynOctetRecord *pasynRec = pPvt->prec;
   int     status;
   int     ninp;
   char    cmd_char=0;
   char    acmd[6];

   /* See asynOctetRecord.dbd for definitions of constants gpibXXXX_Abcd */
   if ((pPvt->pasynGpib) && (pasynRec->ucmd != gpibUCMD_None))
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
      status = pPvt->pasynGpib->universalCmd(pPvt->asynGpibPvt, 
                                             pPvt->pasynUser, cmd_char);
      if (status)
         /* Something is wrong if we couldn't write */
         recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
      pasynRec->ucmd = gpibUCMD_None;  /* Reset to no Universal Command */
   }

   /* See if an Addressed Command is to be done */
   if ((pPvt->pasynGpib) && (pasynRec->acmd != gpibACMD_None))
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
            status = pPvt->pasynOctet->write(pPvt->asynGpibPvt, 
                                             pPvt->pasynUser, &cmd_char, 1);
            if (status)
                /* Something is wrong if we couldn't write */
                recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
            /* Read the response byte  */
            ninp = pPvt->pasynOctet->read(pPvt->asynGpibPvt, 
                                          pPvt->pasynUser, &pasynRec->spr, 1);
            if (ninp < 0) /* Is this right? */
                /* Something is wrong if we couldn't read */
                recGblSetSevr(pasynRec, READ_ALARM, MAJOR_ALARM);
            /* Serial Poll Disable */
            cmd_char = IBSPD;
            status = pPvt->pasynOctet->write(pPvt->asynGpibPvt, 
                                             pPvt->pasynUser, &cmd_char, 1);
            if (status)
                /* Something is wrong if we couldn't write */
                recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
            pasynRec->acmd = gpibACMD_None;  /* Reset to no Addressed Command */
            return;
      }
      status = pPvt->pasynGpib->addressedCmd(pPvt->asynGpibPvt, 
                                             pPvt->pasynUser, acmd, 6);
      if (status)
         /* Something is wrong if we couldn't write */
         recGblSetSevr(pasynRec, WRITE_ALARM, MAJOR_ALARM);
      pasynRec->acmd = gpibACMD_None;  /* Reset to no Addressed Command */
   }
}
