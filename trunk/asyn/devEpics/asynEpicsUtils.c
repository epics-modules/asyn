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

/* parseLink syntax is:
   VME_IO "C<ignore> S<addr> @<portName> userParams
   INST_IO @asyn(<portName>ws<addr>ws<timeout>)usetParams
*/
static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                             char **port, int *addr, char **userParam)
{
    struct vmeio *pvmeio;
    struct instio *pinstio;
    int len;
    char *p;
    char *endp;
    char *pnext;

    assert(addr && port && userParam);
    *addr=0; *port=NULL; *userParam=NULL;
    switch (plink->type) {
    case VME_IO:
        pvmeio=(struct vmeio*)&(plink->value);
        *addr = pvmeio->signal;
        p = pvmeio->parm;
        /* first field of parm is always the asyn port name */
        for(len=0; *p && !isspace(*p) && *p!=','; len++, p++) {}
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
            for(len=0; *p && (isspace(*p) || *p==','); len++, p++){} /*skip whitespace*/
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
        pnext = strstr(p,"asyn");
        if(!pnext) goto error;
        pnext = strchr(pnext,'(');
        if(!pnext) goto error;
        pnext++;
        while(*pnext && isspace(*pnext)) pnext++; /*skip white space*/
        if(!pnext) goto error;
        p = pnext;
        for(len=0; *p && !isspace(*p) && (*p!=',') && (*p!=')')  ; len++, p++){}
        if(*p==0) goto error;
        *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
        (*port)[len] = 0;
        strncpy(*port,pnext,len);
        /*next is addr*/
        pnext = p;
        while(*pnext && isspace(*pnext)) pnext++;
        if(*pnext==',') {
            pnext++;
            while(*pnext && isspace(*pnext)) pnext++;
        }
        if(*pnext==0) goto error;
        if(*pnext==')') {
            *addr = 0;
            pasynUser->timeout = 1.0;
            goto userParams;
        }
        errno = 0;
        *addr = strtol(pnext,&endp,0);
        if(errno) goto error;
        /*next is timeout*/
        pnext = endp;
        if(*pnext==0) goto error;
        while(*pnext && isspace(*pnext)) pnext++;
        if(*pnext==',') {
            pnext++;
            while(*pnext && isspace(*pnext)) pnext++;
        }
        if(*pnext==0) goto error;
        if(*pnext==')') {
            pasynUser->timeout = 1.0;
            goto userParams;
        }
        errno = 0;
        pasynUser->timeout = strtod(pnext,&endp);
        if(errno) goto error;
        while(*pnext && isspace(*pnext)) pnext++;
        if(*pnext!=')') goto error;
        pnext = endp;
userParams:
        if(userParam) *userParam = 0; /*initialize to null string*/
        pnext++; /*skip over )*/
        p = pnext;
        if(*p) {
            for(len=0; *p && isspace(*p) ; len++, p++){} /*skip whitespace*/
            if(userParam&& *p) {
                len = strlen(p);
                *userParam = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");	
                strncpy(*userParam,p,len);
                (*userParam)[len] = 0;
            }
        }
        break;
error:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "invalid INST_IO Must be asyn(<port> <addr> <timeout>)userParams");
        return(asynError);
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
