/*asynEpicsUtils.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/* Author: Mark Rivers */

#include <stddef.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include <link.h>
#include <epicsExport.h>
#include <epicsString.h>

#include "asynEpicsUtils.h"

#define epicsExportSharedSymbols

static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                            char **port, int *addr, char **userParam);

static asynEpicsUtils utils = {
    parseLink
};

epicsShareDef asynEpicsUtils *pasynEpicsUtils = &utils;

static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                             char **port, int *addr, char **userParam)
{
    struct vmeio *pvmeio;
    struct gpibio *pgpibio;
    struct instio *pinstio;
    char temp[100];
    int i;
    char *p;

    *addr=0; *port=NULL; *userParam=NULL;

    switch (plink->type) {
    case VME_IO:
        pvmeio=(struct vmeio*)&(plink->value);
        *addr = pvmeio->signal;
        p = pvmeio->parm;
        /* first field of parm is always the asyn port name */
        for(i=0; *p && *p!=',' && *p!=' '; i++, p++) temp[i]=*p;
        temp[i]='\0';
        *port = epicsStrDup(temp);
        if(*p) *userParam = epicsStrDup(++p);
        break;
    case GPIB_IO:
        pgpibio=(struct gpibio*)&(plink->value);
        *addr = pgpibio->addr;
        p = pgpibio->parm;
        /* first field of parm is always the asyn port name */
        for(i=0; *p && *p!=',' && *p!=' '; i++, p++) temp[i]=*p;
        temp[i]='\0';
        *port = epicsStrDup(temp);
        if(*p) *userParam = epicsStrDup(++p);
        break;
    case INST_IO:
        pinstio=(struct instio*)&(plink->value);
        p = pinstio->string;
        /* first field of string is always the asyn port name */
        for(i=0; *p && *p!=',' && *p!=' '; i++, p++) temp[i]=*p;
        temp[i]='\0';
        *port = epicsStrDup(temp);
        /* Next comes the addr */
        if (*p++) {
           for(i=0; *p && *p!=',' && *p!=' '; i++, p++) temp[i]=*p;
           temp[i]='\0';
           *addr = atoi(temp);
        }
        if(*p) *userParam = epicsStrDup(++p);
        break;
    default:
         epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Link must be INST_IO, VME_IO or GPIB_IO");
        return(asynError);
    }
    if (strlen(*port) == 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Missing port name");
        return(asynError);
    }
    return(asynSuccess);
}

