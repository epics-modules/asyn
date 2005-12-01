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
 * Parm 0 allows devGpib to do everythimg
 * Parm 1 allows devGpib to do all I/O but readString puts result into record
 * Parm 2 has cvtIoExample do all I/O and put result into record
 *
 * All three have the same result
 *
 *
*/

#define	DSET_AI		devAiGpibConvertExample
#define	DSET_SI		devSiGpibConvertExample

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <dbAccess.h>
#include <devSup.h>
#include <recSup.h>
#include <drvSup.h>
#include <dbCommon.h>
#include <aiRecord.h>
#include <stringinRecord.h>

#include <devCommonGpib.h>
#include <devSupportGpib.h>
#include <devGpib.h>

#define	TIMEOUT	1.0
#define TIMEWINDOW  2.0

static int readString(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);
static int cvtIoExample(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3);

/*
 * Define end-of-string character(s) here to allow
 * easier changes when testing the driver.
 */
#define EOSNL "\n"

static struct gpibCmd gpibCmds[] = 
{
  /* Param 0 */
  {&DSET_SI, GPIBREAD, IB_Q_LOW, "*IDN?"EOSNL,  0, 0, 200, 0, 0, 0, 0, 0, EOSNL},
  /* Param 1 simple convert   */
  {&DSET_SI, GPIBREAD, IB_Q_LOW, "*IDN?"EOSNL, 0, 0, 200, readString, 0, 0, 0, 0, EOSNL},
  /* Param 2, example of GPIBCVTIO */
  {&DSET_SI, GPIBCVTIO, IB_Q_LOW, "*IDN?"EOSNL, 0, 0, 200, cvtIoExample, 0, 0, 0, 0, 0}
};


/* The following is the number of elements in the command array above.  */
#define NUMPARAMS	sizeof(gpibCmds)/sizeof(struct gpibCmd)

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
    stringinRecord *prec = (stringinRecord*)pgpibDpvt->precord;
    strncpy(prec->val,pgpibDpvt->msg,sizeof(prec->val));
    prec->val[sizeof(prec->val) - 1] = 0;
    return(0);
}

static int cvtIoExample(gpibDpvt *pgpibDpvt,int P1, int P2, char **P3)
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
                                            "%s cvtIoExample\n",precord->name);
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
    asynPrint(pasynUser,ASYN_TRACE_FLOW,"%s cvtIoExample nchars %d\n",
        precord->name,nchars);
    if(nchars > 0) {
        asynPrintIO(pasynUser,ASYN_TRACEIO_DEVICE,pgpibDpvt->msg,nchars,
            "%s cvtIoExample\n",precord->name);
    } else {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s read status \"%s\" nin %d\n",
            precord->name, pasynUser->errorMessage,nchars);
        pgpibDpvt->msgInputLen = 0;
        return -1;
    }
    pgpibDpvt->msgInputLen = nchars;
    if(nchars<pgpibCmd->msgLen) pgpibDpvt->msg[nchars] = 0;
    strncpy(precord->val,pgpibDpvt->msg,sizeof(precord->val));
    precord->val[sizeof(precord->val) - 1] = 0;
    return 0;
}
