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
#include <ctype.h>
#include <errno.h>

#include <link.h>
#include <epicsAssert.h>
#include <epicsExport.h>
#include <epicsString.h>
#include <cantProceed.h>

#include "asynEpicsUtils.h"

#define epicsExportSharedSymbols

static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                            char **port, int *addr, char **userParam);
static asynStatus parseLinkFree(asynUser *pasynUser,
                            char **port, char **userParam);

static asynEpicsUtils utils = {
    parseLink,parseLinkFree
};

epicsShareDef asynEpicsUtils *pasynEpicsUtils = &utils;


static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                             char **port, int *addr, char **userParam)
{
    struct vmeio *pvmeio;
    struct instio *pinstio;
    int len;
    char *p;
    char *endp;

    assert(addr && port && userParam);
    *addr=0; *port=NULL; *userParam=NULL;
    switch (plink->type) {
    case VME_IO:
        pvmeio=(struct vmeio*)&(plink->value);
        *addr = pvmeio->signal;
        p = pvmeio->parm;
        /* first field of parm is always the asyn port name */
        for(len=0; *p && !isspace(*p) ; len++, p++);
        if(len==0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "parm is null. Must be <port> <addr> ...");
            return(asynError);
        }
        *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
        *port[len] = 0;
        strncpy(*port,pvmeio->parm,len);
        *userParam = 0; /*initialize to null string*/
        if(*p) {
            for(len=0; *p && isspace(*p) ; len++, p++){} /*skip whitespace*/
            if(*p) {
                len = strlen(p);
                *userParam = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");	
                strncpy(*userParam,p,len);
                *userParam[len] = 0;
            }
        }
        break;
    case INST_IO:
        pinstio=(struct instio*)&(plink->value);
        p = pinstio->string;
        /* first field of parm is always the asyn port name */
        for(len=0; *p && !isspace(*p) ; len++, p++){}
        if(len==0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "parm is null. Must be <port> <addr> ...");
            return(asynError);
        }
        *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
        (*port)[len] = 0;
        strncpy(*port,pinstio->string,len);
        /* Next comes the addr */
        errno = 0;
        *addr = strtol(p,&endp,0);
        if(errno) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "while converting addr strtol error %s",
                      strerror(errno));
            return(asynError);
        }
        /* Next is userParam*/
        *userParam = 0; /*initialize to null string*/
        p = endp;
        if(*p) {
            for(len=0; *p && isspace(*p) ; len++, p++){} /*skip whitespace*/
            if(*p) {
                len = strlen(p);
                *userParam = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");	
                strncpy(*userParam,p,len);
                (*userParam)[len] = 0;
            }
        }
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                     "Link must be INST_IO, VME_IO or GPIB_IO");
        return(asynError);
    }
    return(asynSuccess);
}

static asynStatus parseLinkFree(asynUser *pasynUser,
                            char **port, char **userParam)
{
    free(*port); *port = 0;
    free(*userParam); *userParam = 0;
    return(asynSuccess);
}
