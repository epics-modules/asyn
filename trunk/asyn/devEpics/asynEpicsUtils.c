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
#include <alarm.h>
#include <epicsAssert.h>
#include <epicsString.h>
#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynEpicsUtils.h"

static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                   char **port, int *addr, char **userParam);
static asynStatus parseLinkMask(asynUser *pasynUser, DBLINK *plink, 
                   char **port, int *addr, epicsUInt32 *mask,char **userParam);
static asynStatus parseLinkFree(asynUser *pasynUser,
                   char **port, char **userParam);
static void asynStatusToEpicsAlarm(asynStatus status, 
                                   epicsAlarmCondition defaultStat, epicsAlarmCondition *pStat, 
                                   epicsAlarmSeverity defaultSevr, epicsAlarmSeverity *pSevr);

static asynEpicsUtils utils = {
    parseLink,parseLinkMask,parseLinkFree,asynStatusToEpicsAlarm
};

epicsShareDef asynEpicsUtils *pasynEpicsUtils = &utils;

/* parseLink syntax is:
   VME_IO "C<ignore> S<addr> @<portName> userParams
   INST_IO @asyn(<portName>ws<addr>ws<timeout>)userParams
   INST_IO @asynMask(<portName>ws<addr>ws<mask>ws<timeout>)userParams
*/

static char *skipWhite(char *pstart,int commaOk){
    char *p = pstart;
    while(*p && (isspace((int)*p) || (commaOk && (*p==',')))) p++;
    return p;
}

static asynStatus parseLink(asynUser *pasynUser, DBLINK *plink, 
                             char **port, int *addr, char **userParam)
{
    struct vmeio *pvmeio;
    struct instio *pinstio;
    size_t len;
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
        p = skipWhite(p,0);
        for(len=0; *p && !isspace((int)*p) && *p!=','  ; len++, p++){}
        /* first field of parm is always the asyn port name */
        if(len==0) {
            epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                      "parm is null. Must be <port> <addr> ...");
            return(asynError);
        }
        *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
        (*port)[len] = 0;
        strncpy(*port,pvmeio->parm,len);
        *userParam = 0; /*initialize to null string*/
        if(*p) {
            p = skipWhite(p,0);
            if(*p) {
                len = strlen(p);
                *userParam = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
                strncpy(*userParam,p,len);
                (*userParam)[len] = 0;
            }
        }
        break;
    case INST_IO:
        pinstio=(struct instio*)&(plink->value);
        p = pinstio->string;
        pnext = strstr(p,"asyn(");
        if(!pnext) goto error;
        pnext+=5;
        pnext = skipWhite(pnext,0);
        p = pnext;
        for(len=0; *p && !isspace((int)*p) && (*p!=',') && (*p!=')')  ; len++, p++){}
        if(*p==0) goto error;
        *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
        (*port)[len] = 0;
        strncpy(*port,pnext,len);
        /*next is addr*/
        pnext = p;
        pnext = skipWhite(pnext,1);
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
        pnext = skipWhite(pnext,1);
        if(*pnext==0) goto error;
        if(*pnext==')') {
            pasynUser->timeout = 1.0;
            goto userParams;
        }
        errno = 0;
        pasynUser->timeout = strtod(pnext,&endp);
        if(errno) goto error;
        pnext = endp;
        pnext = skipWhite(pnext,0);
        if(*pnext!=')') goto error;
userParams:
        if(userParam) *userParam = 0; /*initialize to null string*/
        pnext++; /*skip over )*/
        p = pnext;
        if(*p) {
            p = skipWhite(p,0);
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
                     "Link must be INST_IO or VME_IO");
        return(asynError);
    }
    return(asynSuccess);
}

static asynStatus parseLinkMask(asynUser *pasynUser, DBLINK *plink, 
                   char **port, int *addr, epicsUInt32 *mask,char **userParam)
{
    struct instio *pinstio;
    size_t len;
    char *p;
    char *endp;
    char *pnext;

    assert(addr && port && userParam);
    *addr=0; *port=NULL; *userParam=NULL;
    if(plink->type!=INST_IO) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
                     "Link must be INST_IO");
        return(asynError);
    }
    pinstio=(struct instio*)&(plink->value);
    p = pinstio->string;
    pnext = strstr(p,"asynMask(");
    if(!pnext) goto error;
    pnext+=9;
    pnext = skipWhite(pnext,0);
    p = pnext;
    for(len=0; *p && !isspace((int)*p) && (*p!=',') && (*p!=')')  ; len++, p++){}
    if(*p==0) goto error;
    *port = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
    (*port)[len] = 0;
    strncpy(*port,pnext,len);
    /*next is addr*/
    pnext = p;
    pnext = skipWhite(pnext,1);
    if(*pnext==0 || *pnext==')') goto error;
    errno = 0;
    *addr = strtol(pnext,&endp,0);
    if(errno) goto error;
    /*next is mask*/
    pnext = endp;
    pnext = skipWhite(pnext,1);
    if(*pnext==0 || *pnext==')') goto error;
    errno = 0;
    *mask = strtoul(pnext,&endp,0);
    if(errno) goto error;
    /*next is timeout*/
    pnext = endp;
    pnext = skipWhite(pnext,1);
    if(*pnext==0) goto error;
    if(*pnext==')') {
        pasynUser->timeout = 1.0;
        goto userParams;
    }
    errno = 0;
    pasynUser->timeout = strtod(pnext,&endp);
    if(errno) goto error;
    pnext = endp;
    pnext = skipWhite(pnext,0);
    if(*pnext!=')') goto error;
userParams:
    if(userParam) *userParam = 0; /*initialize to null string*/
    pnext++; /*skip over )*/
    p = pnext;
    if(*p) {
        p = skipWhite(p,0);
        if(userParam&& *p) {
            len = strlen(p);
            *userParam = mallocMustSucceed(len+1,"asynEpicsUtils:parseLink");
            strncpy(*userParam,p,len);
            (*userParam)[len] = 0;
        }
    }
    return(asynSuccess);
error:
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "invalid INST_IO Must be asynMask(<port> <addr> <mask> <timeout>)userParams");
        return(asynError);
}

static asynStatus parseLinkFree(asynUser *pasynUser,
                            char **port, char **userParam)
{
    free(*port); *port = 0;
    free(*userParam); *userParam = 0;
    return(asynSuccess);
}

static void asynStatusToEpicsAlarm(asynStatus status, 
                                   epicsAlarmCondition defaultStat, epicsAlarmCondition *pStat, 
                                   epicsAlarmSeverity defaultSevr, epicsAlarmSeverity *pSevr)
{
    switch (status) {
        case asynSuccess:
            *pStat = epicsAlarmNone;
            *pSevr = epicsSevNone;
            break;
        case asynTimeout:
            *pStat = epicsAlarmTimeout;
            *pSevr = defaultSevr;
            break;
        case asynOverflow:
            *pStat = epicsAlarmHwLimit;
            *pSevr = defaultSevr;
            break;
        case asynError:
            *pStat = defaultStat;
            *pSevr = defaultSevr;
            break;
        case asynDisconnected:
            *pStat = epicsAlarmComm;
            *pSevr = defaultSevr;
            break;
        case asynDisabled:
            *pStat = epicsAlarmDisable;
            *pSevr = defaultSevr;
            break;
        default:
            *pStat = defaultStat;
            *pSevr = defaultSevr;
            break;
    }
}
