/* devTestGpib.c */
/*
 *      Author: Marty Kraimer
 *      Date:   25OCT2002
 */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#define DSET_AI   devAiTestGpib
#define DSET_AO   devAoTestGpib
#define DSET_BI   devBiTestGpib
#define DSET_BO   devBoTestGpib
#define DSET_LI   devLiTestGpib
#define DSET_LO   devLoTestGpib
#define DSET_MBBO devMbboTestGpib
#define DSET_MBBI devMbbiTestGpib
#define DSET_SI   devSiTestGpib
#define DSET_SO   devSoTestGpib

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#include <alarm.h>
#include <errlog.h>
#include <cvtTable.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <devSup.h>
#include <recSup.h>
#include <drvSup.h>
#include <link.h>
#include <dbCommon.h>
#include <aiRecord.h>
#include <aoRecord.h>
#include <biRecord.h>
#include <boRecord.h>
#include <mbbiRecord.h>
#include <mbboRecord.h>
#include <stringinRecord.h>
#include <stringoutRecord.h>
#include <longinRecord.h>
#include <longoutRecord.h>

#include <devCommonGpib.h>
#include <devGpib.h>
#include <epicsExport.h>

#define TIMEOUT 1.0
#define TIMEWINDOW  2.0

static int readString(gpibDpvt *pdpvt,int P1, int P2, char **P3);

/*
 * Define end-of-string character(s) here to allow
 * easier changes when testing the driver.
 */
#define EOSNL "\n"

static struct gpibCmd gpibCmds[] = 
{
  /* Param 0, */
  {&DSET_BO, GPIBIFC, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 1, */
  {&DSET_BO, GPIBREN, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 2, */
  {&DSET_BO, GPIBDCL, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 3, */
  {&DSET_BO, GPIBLLO, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 4, */
  {&DSET_BO, GPIBSDC, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 5, */
  {&DSET_BO, GPIBGTL, IB_Q_LOW, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 6, This was deleted */
  {&DSET_BO, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0},

  /* Param 7, */
  {&DSET_LI, GPIBSRQHANDLER, IB_Q_LOW, 0,  0, 0, 0, 0, 0, 0, 0, 0, 0},
  /* Param 8 */
  {&DSET_LO, GPIBWRITE, IB_Q_LOW, 0, "DESE %ld"EOSNL, 0, 20, 0, 0, 0, 0, 0, 0},
  /* Param 9 */
  {&DSET_LO, GPIBWRITE, IB_Q_LOW, 0, "*ESE %ld"EOSNL, 0, 20, 0, 0, 0, 0, 0, 0},
  /* Param 10 */
  {&DSET_LO, GPIBWRITE, IB_Q_LOW, 0, "*SRE %ld"EOSNL, 0, 20, 0, 0, 0, 0, 0, 0},
  /* Param 11 */
  {&DSET_LI, GPIBREAD, IB_Q_LOW, "DESE?"EOSNL,  "%ld", 0, 20, 0, 0, 0, 0, 0, EOSNL},
  /* Param 12 */
  {&DSET_LI, GPIBREAD, IB_Q_LOW, "*ESR?"EOSNL,  "%ld", 0, 20, 0, 0, 0, 0, 0, EOSNL},
  /* Param 13 */
  {&DSET_LI, GPIBREAD, IB_Q_LOW, "*ESE?"EOSNL,  "%ld", 0, 20, 0, 0, 0, 0, 0, EOSNL},
  /* Param 14 */
  {&DSET_LI, GPIBREAD, IB_Q_LOW, "*STB?"EOSNL,  "%ld", 0, 20, 0, 0, 0, 0, 0, EOSNL},
  /* Param 15 */
  {&DSET_LI, GPIBREAD, IB_Q_LOW, "*SRE?"EOSNL,  "%ld", 0, 20, 0, 0, 0, 0, 0, EOSNL},

  /* Param 16 (id)   */
  {&DSET_SI, GPIBREAD, IB_Q_LOW, "*IDN?"EOSNL, 0, 0, 200, readString, 0, 0, 0, 0, EOSNL},
  /* Param 17 */
  {&DSET_SI, GPIBREADW, IB_Q_LOW, "*IDN?"EOSNL, 0, 0, 200, readString, 0, 0, 0, 0, 0},
  /* Param 18, */
  {&DSET_BO, GPIBCMD, IB_Q_LOW, "XXXJUNK"EOSNL,  0, 0, 0, 0, 0, 0, 0, 0, 0}
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
    devSupParms.name = "devTestGpib";
    devSupParms.gpibCmds = gpibCmds;
    devSupParms.numparams = NUMPARAMS;
    devSupParms.timeout = TIMEOUT;
    devSupParms.timeWindow = TIMEWINDOW;
    devSupParms.respond2Writes = -1;
  }
  return(0);
}

static int readString(gpibDpvt *pdpvt,int P1, int P2, char **P3)
{
    stringinRecord *prec = (stringinRecord*)pdpvt->precord;
    strncpy(prec->val,pdpvt->msg,sizeof(prec->val));
    prec->val[sizeof(prec->val) - 1] = 0;
    return(0);
}
