/*asynUtils.c*/
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

#include "asynUtils.h"

#define epicsExportSharedSymbols

static asynStatus parseVmeIo(asynUser *pasynUser, DBLINK *plink, int *card, int *signal, 
                             char **port, char **userParam);

static asynUtils utils = {
    parseVmeIo
};

epicsShareDef asynUtils *pasynUtils = &utils;

static asynStatus parseVmeIo(asynUser *pasynUser, DBLINK *plink, int *card, int *signal, 
                             char **port, char **userParam)
{
    struct vmeio *pvmeio;
    char name[100];
    int i;

    *card=0; *signal=0; *port=NULL; *userParam=NULL;

    if(plink->type!=VME_IO) {
         epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Link is not a VME link");
        return(asynError);
    }
    pvmeio=(struct vmeio*)&(plink->value);
    *card = pvmeio->card;
    *signal = pvmeio->signal;
    /* first field of parm is always the asyn port name */
    for(i=0;
       pvmeio->parm[i] && pvmeio->parm[i]!=',' && pvmeio->parm[i]!=' ';
       i++) name[i]=pvmeio->parm[i];
    if(pvmeio->parm[i]) *userParam = epicsStrDup(&pvmeio->parm[i+1]);
    name[i]='\0';
    *port = epicsStrDup(name);
    if (strlen(*port) == 0) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "Missing port name");
        return(asynError);
    }
    return(asynSuccess);
}

