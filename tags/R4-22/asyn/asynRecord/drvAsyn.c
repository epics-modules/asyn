/* drvAsyn.c */
/*
 *      Author: Marty Kraimer
 *      Date:   02DEC2003
 */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <cantProceed.h>
#include <epicsStdio.h>
#include <epicsStdioRedirect.h>
#include <epicsAssert.h>
#include <link.h>
#include <epicsMutex.h>
#include <alarm.h>
#include <callback.h>
#include <dbDefs.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <link.h>
#include <drvSup.h>
#include <registryFunction.h>
#include <epicsExport.h>
#include "asynDriver.h"

/* add support so that dbior generates asynDriver reports */
static long drvAsynReport(int level);
struct {
        long     number;
        DRVSUPFUN report;
        DRVSUPFUN init;
} drvAsyn={
        2,
        drvAsynReport,
        0
};
epicsExportAddress(drvet,drvAsyn);

static long drvAsynReport(int level)
{
    pasynManager->report(stdout,level,0);
    return(0);
}
