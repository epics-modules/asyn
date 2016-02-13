/* devGpibConvertExample.c */
/*
 *      Author: Marty Kraimer
 *      Date:   30NOV2005
 */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/* This is an example of how to use convert
 *
 * Parm 0,1,2 are for stringin
 * Parm 0 allows devGpib to do everythimg
 * Parm 1 allows devGpib to do all I/O but readString puts result into record
 * Parm 2 has readCvtio do all I/O and put result into record
 * All three have the same result
 *
 * Parm 3,4,5 are for stringout
 * Parm 3 allows devGpib to do everythimg
 * Parm 4 allows devGpib to do all I/O but readString puts result into record
 * Parm 5 has readCvtio do all I/O and put result into record
 * All three have the same result
 *
*/

#define DSET_AI devAiGpibConvertExample
#define DSET_SI devSiGpibConvertExample
#define DSET_SO devSoGpibConvertExample

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>
#include <recGbl.h>
#include <recSup.h>
#include <drvSup.h>
#include <dbCommon.h>
#include <aiRecord.h>
#include <stringinRecord.h>

#include <devCommonGpib.h>
#include <devSupportGpib.h>
#include <devGpib.h>

#define TIMEOUT 1.0
#define TIMEWINDOW  2.0

static int readString(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);
static int readCvtio(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);
static int writeString(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);
static int writeCvtio(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);

/*
 * Define end-of-string character(s) here to allow
 * easier changes when testing the driver.
 */

static struct gpibCmd gpibCmds[] = 
{
  /* Param 0 */
  {&DSET_SI,GPIBREAD,IB_Q_LOW,"*IDN?", 0,0,200,0,0,0,0,0,0},
  /* Param 1 simple convert   */
  {&DSET_SI,GPIBREAD,IB_Q_LOW,"*IDN?",0,0,200,readString,0,0,0,0,0},
  /* Param 2,example of GPIBCVTIO */
  {&DSET_SI,GPIBCVTIO,IB_Q_LOW,"*IDN?",0,0,200,readCvtio,0,0,0,0,0},
  /* Param 3 */
  {&DSET_SO,GPIBWRITE,IB_Q_LOW,0,0,0,200,0,0,0,0,0,0},
  /* Param 4 simple convert   */
  {&DSET_SO,GPIBWRITE,IB_Q_LOW,0,0,0,200,writeString,0,0,0,0,0},
  /* Param 5,example of GPIBCVTIO */
  {&DSET_SO,GPIBCVTIO,IB_Q_LOW,0,0,0,200,writeCvtio,0,0,0,0,0}
};


/* The following is the number of elements in the command array above.  */
#define NUMPARAMS sizeof(gpibCmds)/sizeof(struct gpibCmd)

/******************************************************************************
 *
 * Initialization for device support
 * This is called one time before any records are initialized with a parm
 * value of 0.  And then again AFTER all record-level init is complete
 * with a param value of 1.
 *
 ******************************************************************************/
static long init_ai(int parm)
{
  if(parm==0)  {
    devSupParms.name = "devGpibConvertExample";
    devSupParms.gpibCmds = gpibCmds;
    devSupParms.numparams = NUMPARAMS;
    devSupParms.timeout = TIMEOUT;
    devSupParms.timeWindow = TIMEWINDOW;
    devSupParms.respond2Writes = -1;
  }
  return(0);
}

static int readString(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3)
{
    stringinRecord *precord = (stringinRecord*)pgpibDpvt->precord;
    strncpy(precord->val,pgpibDpvt->msg,sizeof(precord->val));
    precord->val[sizeof(precord->val) - 1] = 0;
    return(0);
}

static int readCvtio(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3)
{
    stringinRecord *precord = (stringinRecord*)pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    asynStatus status;
    size_t nchars = 0, lenmsg = 0;
    pgpibDpvt->msgInputLen = 0;

    assert(pgpibCmd->cmd);
    lenmsg = strlen(pgpibCmd->cmd);
    status  = pasynOctet->write(asynOctetPvt,pasynUser,
        pgpibCmd->cmd,lenmsg,&nchars);
    if(nchars==lenmsg) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibCmd->cmd,nchars,
                                            "%s readCvtio\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s write status \"%s\" requested %d but sent %d bytes\n",
                precord->name,pasynUser->errorMessage,lenmsg,nchars);
            return -1;
    }
    if(!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s pgpibDpvt->msg is null\n",precord->name);
        nchars = 0; return -1;
    } else {
        status = pasynOctet->read(asynOctetPvt,pasynUser,
            pgpibDpvt->msg,pgpibCmd->msgLen,&nchars,0);
    }
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s readCvtio nchars %d\n",
        precord->name,nchars);
    if(nchars > 0) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,nchars,
            "%s readCvtio\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s read status \"%s\" nin %d\n",
            precord->name, pasynUser->errorMessage,nchars);
        pgpibDpvt->msgInputLen = 0;
        return -1;
    }
    pgpibDpvt->msgInputLen = nchars;
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    readString(pgpibDpvt,P1,P2,P3);
    return 0;
}

static int writeString(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3)
{
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    stringoutRecord *precord = (stringoutRecord*)pgpibDpvt->precord;
    int            nchars;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";

    if(!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer. Must define gpibCmd.msgLen > 0.\n",
            precord->name);
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM);
        return -1;
    }
    nchars = epicsSnprintf(pgpibDpvt->msg,pgpibCmd->msgLen,format,precord->val);
    if(nchars>pgpibCmd->msgLen) { 
        asynPrint(pasynUser,ASYN_TRACE_ERROR, 
            "%s msg buffer too small. msgLen %d message length %d\n", 
            precord->name,pgpibCmd->msgLen,nchars); 
        recGblSetSevr(precord,WRITE_ALARM, INVALID_ALARM); 
        return -1; 
    } 
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s writeMsgString\n",precord->name);
    return nchars;
}

static int writeCvtio(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3)
{
    stringoutRecord *precord = (stringoutRecord*)pgpibDpvt->precord;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    asynOctet *pasynOctet = pgpibDpvt->pasynOctet;
    void *asynOctetPvt = pgpibDpvt->asynOctetPvt;
    asynStatus status;
    size_t nsent = 0, lenmsg = 0;
    pgpibDpvt->msgInputLen = 0;

    lenmsg = writeString(pgpibDpvt,P1,P2,P3);
    if(lenmsg <= 0) return -1;
    status  = pasynOctet->write(asynOctetPvt,pasynUser,
        pgpibDpvt->msg,lenmsg,&nsent);
    if(nsent==lenmsg) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,lenmsg,
                                            "%s writeCvtio\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s write status \"%s\" requested %d but sent %d bytes\n",
                precord->name,pasynUser->errorMessage,lenmsg,nsent);
            return -1;
    }
    return 0;
}
