/*************************************************************************\
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE Versions 3.13.7
* and higher are distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/
/* $Id: devAB300.c,v 1.2 2004-01-29 19:33:55 norume Exp $ */
#include <epicsStdio.h>
#include <devCommonGpib.h>


/******************************************************************************
 *
 * The following define statements are used to declare the names to be used
 * for the dset tables.   
 *
 * A DSET_AI entry must be declared here and referenced in an application
 * database description file even if the device provides no AI records.
 *
 ******************************************************************************/
#define DSET_AI     devAB300_ai
#define DSET_LI     devAB300_li
#define DSET_LO     devAB300_lo

#include <devGpib.h> /* must be included after DSET defines */


#define TIMEWINDOW 2.0       /* wait 2 seconds after device timeout */
#define TIMEOUT    5.0       /* I/O must complete within 5 seconds */


/*
 * Custom conversion routines
 */
static int
convertPositionReply(struct gpibDpvt *pdpvt, int P1, int P2, char **P3)
{
    struct longinRecord *pli = ((struct longinRecord *)(pdpvt->precord));

    if ((pdpvt->msgInputLen != 3) || (pdpvt->msg[2] != '\030')) {
        epicsSnprintf(pdpvt->pasynUser->errorMessage,
                      pdpvt->pasynUser->errorMessageSize,
                      "Invalid reply");
        return -1;
    }
    pli->val = pdpvt->msg[0];
    return 0;
}
static int
convertStatusReply(struct gpibDpvt *pdpvt, int P1, int P2, char **P3)
{
    struct longinRecord *pli = ((struct longinRecord *)(pdpvt->precord));

    if ((pdpvt->msgInputLen != 3) || (pdpvt->msg[2] != '\030')) {
        epicsSnprintf(pdpvt->pasynUser->errorMessage,
                      pdpvt->pasynUser->errorMessageSize,
                      "Invalid reply");
        return -1;
    }
    pli->val = pdpvt->msg[1];
    return 0;
}

/******************************************************************************
 *
 * Array of structures that define all GPIB messages
 * supported for this type of instrument.
 *
 ******************************************************************************/

static struct gpibCmd gpibCmds[] = {
    /* Param 0 -- Device Reset */
    {&DSET_LO, GPIBWRITE, IB_Q_HIGH, NULL, "\377\377\033", 10, 10,
        NULL, 0, 0, NULL, NULL, "\033"},

    /* Param 1 -- Go to new filter position */
    {&DSET_LO, GPIBWRITE, IB_Q_LOW, NULL, "\017%c", 10, 10,
        NULL, 0, 0, NULL, NULL, "\030"},

    /* Param 2 -- Query filter position */
    {&DSET_LI, GPIBREAD, IB_Q_LOW, "\035", NULL, 0, 10,
        convertPositionReply, 0, 0, NULL, NULL, "\030"},

    /* Param 3 -- Query controller status */
    {&DSET_LI, GPIBREAD, IB_Q_LOW, "\035", NULL, 0, 10,
        convertStatusReply, 0, 0, NULL, NULL, "\030"}
};

/* The following is the number of elements in the command array above.  */
#define NUMPARAMS        sizeof(gpibCmds)/sizeof(struct gpibCmd)

/******************************************************************************
 *
 * Initialize device support parameters
 *
 *****************************************************************************/
static long init_ai(int parm)
{
  if(parm==0)  {
    devSupParms.name = "devAB300";
    devSupParms.gpibCmds = gpibCmds;
    devSupParms.numparams = NUMPARAMS;
    devSupParms.timeout = TIMEOUT;
    devSupParms.timeWindow = TIMEWINDOW;
    devSupParms.respond2Writes = 0;
  }
  return(0);
}
