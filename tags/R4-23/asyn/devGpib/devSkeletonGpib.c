/* devSkeletonGpib.c */
/* $Id: devSkeletonGpib.c,v 1.6 2004-02-16 15:02:13 mrk Exp $ */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago,as Operator of Argonne
* National Laboratory,and the Regents of the University of
* California,as Operator of Los Alamos National Laboratory,and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * Current Author: Marty Kraimer
 * Original Authors: John Winans and Benjamin Franksen
 */
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <alarm.h>
#include <recGbl.h>
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
#define DSET_AI     devAiSkeletonGpib
#define DSET_AO     devAoSkeletonGpib
#define DSET_LI     devLiSkeletonGpib
#define DSET_LO     devLoSkeletonGpib
#define DSET_BI     devBiSkeletonGpib
#define DSET_BO     devBoSkeletonGpib
#define DSET_MBBI   devMbbiSkeletonGpib
#define DSET_MBBO   devMbboSkeletonGpib
#define DSET_SI     devSiSkeletonGpib
#define DSET_SO     devSoSkeletonGpib
#define DSET_EV     devEvSkeletonGpib

#include <devGpib.h> /* must be included after DSET defines */


#define TIMEOUT     1.0
#define TIMEWINDOW  2.0

/******************************************************************************
 * Strings used by the init routines to fill in the znam,onam,...
 * fields in BI and BO record types.
 ******************************************************************************/

static char  *offOnList[] = { "Off","On" };
static struct devGpibNames   offOn = { NELEMENTS(offOnList),offOnList,0,1 };

static char  *initNamesList[] = { "Init","Init" };
static struct devGpibNames initNames = { NELEMENTS(initNamesList),initNamesList,0,1 };

static char  *disableEnableList[] = { "Disable","Enable" };
static struct devGpibNames disableEnable = { NELEMENTS(disableEnableList),disableEnableList,0,1 };

static char  *resetList[] = { "Reset","Reset" };
static struct devGpibNames reset = { NELEMENTS(resetList),resetList,0,1 };

static char  *lozHizList[] = { "50 OHM","IB_Q_HIGH Z" };
static struct devGpibNames lozHiz = {NELEMENTS(lozHizList),lozHizList,0,1};

static char  *invertNormList[] = { "INVERT","NORM" };
static struct devGpibNames invertNorm = { NELEMENTS(intvertNormList),invertNormList,0,1 };

static char  *fallingRisingList[] = { "FALLING","RISING" };
static struct devGpibNames fallingRising = { NELEMENTS(fallingRisingList),fallingRisingList,0,1 };

static char  *clearList[] = { "CLEAR","CLEAR" };
static struct devGpibNames clear = { NELEMENTS(clearList),clearList,0,1 };

/******************************************************************************
 * Structures used by the init routines to fill in the onst,twst,... and the
 * onvl,twvl,... fields in MBBI and MBBO record types.
 *
 * Note that the intExtSsBm and intExtSsBmStop structures use the same
 * intExtSsBmStopList and intExtSsBmStopVal lists but have a different number
 * of elements in them that they use... The intExtSsBm structure only represents
 * 4 elements,while the intExtSsBmStop structure represents 5.
 ******************************************************************************/

static char *intExtSsBmStopList[] = {
    "INTERNAL","EXTERNAL","SINGLE SHOT","BURST MODE","STOP" };
static unsigned long intExtSsBmStopVal[] = { 0,1,2,3,2 };
static struct devGpibNames intExtSsBm = {
    4,intExtSsBmStopList, intExtSsBmStopVal,2 };
static struct devGpibNames intExtSsBmStop = {
    5,intExtSsBmStopList,intExtSsBmStopVal,3 };

/******************************************************************************
 * String arrays for EFAST operations. The last entry must be 0.
 *
 * On input operations,only as many bytes as are found in the string array
 * elements are compared.  Additional bytes are ignored.
 * The first matching string  will be used as a match.
 *
 * For the input operations,the strings are compared literally!  This
 * can cause problems if the instrument is returning things like \r and \n
 * characters.  When defining input strings so you include them as well.
 ******************************************************************************/

static char *userOffOn[] = {"USER OFF;","USER ON;",0};

/******************************************************************************
 * Array of structures that define all GPIB messages
 * supported for this type of instrument.
 ******************************************************************************/

static struct gpibCmd gpibCmds[] = {
    /* Param 0 */
    {&DSET_BO,GPIBCMD,IB_Q_HIGH,"init",0,0,32,NULL,0,0,NULL,&initNames,0},
    /* Param 1 */
    {&DSET_BO,GPIBEFASTO,IB_Q_HIGH,0,NULL,0,32,0,0,0,userOffOn,&offOn,0},
    /* Param 2 */
    {&DSET_BI,GPIBEFASTI,IB_Q_HIGH,"user?",0,0,32,0,0,0,userOffOn,&offOn,0},
    /* Param 3 */
    {&DSET_AI,GPIBREAD,IB_Q_LOW,"send","%lf",0,32,0,0,0,NULL,NULL,0}
};

/* The following is the number of elements in the command array above.  */
#define NUMPARAMS sizeof(gpibCmds)/sizeof(struct gpibCmd)

/******************************************************************************
 * Initialize device support parameters
 *
 *****************************************************************************/
static long init_ai(int parm)
{
    if(parm==0) {
        devSupParms.name = "devSkeletonGpib";
        devSupParms.gpibCmds = gpibCmds;
        devSupParms.numparams = NUMPARAMS;
        devSupParms.timeout = TIMEOUT;
        devSupParms.timeWindow = TIMEWINDOW;
        devSupParms.respond2Writes = -1;
    }
    return(0);
}
