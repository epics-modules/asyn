/* devCommonGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/

/*
 * Current Author: Marty Kraimer
 * Original Authors: John Winans and Benjamin Franksen
*/

#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <epicsAssert.h>
#include <epicsStdio.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbScan.h>
#include <alarm.h>
#include <devSup.h>
#include <recSup.h>
#include <callback.h>
#include <drvSup.h>
#include <link.h>
#include <errlog.h>
#include <menuFtype.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynGpibDriver.h"
#include "devSupportGpib.h"
#include "devCommonGpib.h"


/* The following is a generic finish routine for output records */
static void genericFinish(gpibDpvt * pgpibDpvt,int failure)
{
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static void aiFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initAi(aiRecord * pai)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    DEVSUPFUN  got_special_linconv = ((gDset *) pai->dset)->funPtr[5];

    result = pdevSupportGpib->initRecord((dbCommon *) pai, &pai->inp);
    if(result) return result;
    pgpibDpvt = gpibDpvtGet(pai);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for AI record in param %d\n",
            pai->name, pgpibDpvt->parm);
        pai->pact = TRUE;
        return S_db_badField;
    }
    if(got_special_linconv) (*got_special_linconv)(pai,TRUE);
    return 0;
}

long  devGpib_readAi(aiRecord * pai)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pai);
    int cmdType;
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];
 
    if(pai->pact) return (got_special_linconv ? 0 : 2);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,aiFinish);
    }
    return (got_special_linconv ? 0 : 2);
}

static void aiFinish(gpibDpvt * pgpibDpvt,int failure)
{
    double value;
    long rawvalue;
    aiRecord *pai = ((aiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pai->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR, "%s no msg buffer\n",pai->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        int result = 0;
        if(got_special_linconv) {
            char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%ld";
            result = sscanf(pgpibDpvt->msg, format, &rawvalue);
            if(result==1) {pai->rval = rawvalue; pai->udf = FALSE;}
        } else {
            char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%lf";
            result = sscanf(pgpibDpvt->msg, format, &value);
            if(result==1) {pai->val = value; pai->udf = FALSE;}
        }
        if(result!=1) failure = -1;
    }
    if(failure==-1) recGblSetSevr(pai, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static int aoStart(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initAo(aoRecord * pao)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pao, &pao->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pao);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for AO record in param %d\n",
            pao->name, pgpibDpvt->parm);
        pao->pact = TRUE;
        return S_db_badField;
    }
    if(got_special_linconv) (*got_special_linconv)(pao,TRUE);
    return (got_special_linconv ? 0 : 2);
}

long  devGpib_writeAo(aoRecord * pao)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pao);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pao->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,aoStart,genericFinish);
    }
    return 0;
}

static int aoStart(gpibDpvt *pgpibDpvt,int failure)
{
    aoRecord *pao = ((aoRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            if (got_special_linconv) {
                failure = pdevSupportGpib->writeMsgLong(pgpibDpvt,pao->rval);
            } else {
                failure = pdevSupportGpib->writeMsgDouble(pgpibDpvt,pao->oval);
            }
        }
    }
    return failure;
}

static void biFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initBi(biRecord * pbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbi, &pbi->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbi);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO|GPIBEFASTI|GPIBEFASTIW))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for BI record in param %d\n",
            pbi->name, pgpibDpvt->parm);
        pbi->pact = TRUE;
        return  S_db_badField;
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if(pdevGpibNames) {
        if (pbi->znam[0] == 0)
            strncpy(pbi->znam, pdevGpibNames->item[0], sizeof(pbi->znam));
        if (pbi->onam[0] == 0)
            strncpy(pbi->onam, pdevGpibNames->item[1], sizeof(pbi->onam));
    }
    return  0;
}

long  devGpib_readBi(biRecord * pbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbi);
    int cmdType;
 
    if(pbi->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,biFinish);
    }
    return 0;
}

static void biFinish(gpibDpvt * pgpibDpvt,int failure)
{
    biRecord *pbi = ((biRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    unsigned long value;
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pbi->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
        if(pgpibDpvt->efastVal>=0) {
            pbi->rval = pgpibDpvt->efastVal;
        } else {
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",pbi->name);
        failure = -1;
    } else {
        char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%lu";
        if(sscanf(pgpibDpvt->msg, format, &value) == 1) {
            pbi->rval = value;
        } else {
            /* sscanf did not find or assign the parameter*/
            failure = -1;
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't convert msg >%s<\n",
                pbi->name, pgpibDpvt->msg);
        }
    }
    if(failure==-1) recGblSetSevr(pbi, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static int boStart(gpibDpvt *pgpibDpvt,int failure);
static void writeBoSpecial(boRecord * pbo);
static void boWorkSpecial(gpibDpvt *pgpibDpvt,int failure);
static char *ifcName[] = {"noop", "IFC", 0};
static char *renName[] = {"drop REN", "assert REN", 0};
static char *dclName[] = {"noop", "DCL", "0"};
static char *lloName[] = {"noop", "LLO", "0"};
static char *sdcName[] = {"noop", "SDC", "0"};
static char *gtlName[] = {"noop", "GTL", "0"};

long  devGpib_initBo(boRecord * pbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbo, &pbo->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBIFC|GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL)) {
        /* supply defaults for menus */
        char **papname = 0;
        switch (cmdType) {
        case GPIBIFC: papname = ifcName; break;
        case GPIBREN: papname = renName; break;
        case GPIBDCL: papname = dclName; break;
        case GPIBLLO: papname = lloName; break;
        case GPIBSDC: papname = sdcName; break;
        case GPIBGTL: papname = gtlName; break;
        default:
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s devGpib_initBo logic error\n",pbo->name);
        }
        if (papname) {
            if (pbo->znam[0] == 0)
                strncpy(pbo->znam, papname[0], sizeof(pbo->znam));
            if (pbo->onam[0] == 0)
                strncpy(pbo->onam, papname[1], sizeof(pbo->onam));
        }
    } else if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for BO record in param %d\n",
            pbo->name, pgpibDpvt->parm);
        pbo->pact = TRUE;
        return S_db_badField;
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if(pdevGpibNames) {
        if (pbo->znam[0] == 0)
            strncpy(pbo->znam, pdevGpibNames->item[0], sizeof(pbo->znam));
        if (pbo->onam[0] == 0)
            strncpy(pbo->onam, pdevGpibNames->item[1], sizeof(pbo->onam));
    }
    return 2;
}

long  devGpib_writeBo(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pbo->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else
    if(cmdType&(GPIBIFC|GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL)) {
        writeBoSpecial(pbo);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,boStart,genericFinish);
    }
    return 0;
}

static int boStart(gpibDpvt * pgpibDpvt,int failure)
{
    boRecord *pbo = (boRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pbo->val;
        }
    }
    return failure;
}

static void writeBoSpecial(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pbo->pact) return;
    if(cmdType&(GPIBIFC|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL)) {
        if(pbo->val==0) return;
    }
    pdevSupportGpib->queueRequest(pgpibDpvt,boWorkSpecial);
    return;
}

static void boWorkSpecial(gpibDpvt *pgpibDpvt,int failure)
{
    boRecord *precord = (boRecord *)pgpibDpvt->precord;
    int val = (int)precord->val;
    int cmdType = gpibCmdGetType(pgpibDpvt);
    asynGpib *pasynGpib = pgpibDpvt->pasynGpib;
    void *drvPvt = pgpibDpvt->asynGpibPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    asynStatus status = asynSuccess;

    if(failure) {; /*do nothing*/
    } else if(!pasynGpib) {
        failure = -1;
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s pasynGpib is 0\n",precord->name);
    } else switch(cmdType) {
        case GPIBIFC: status = pasynGpib->ifc(drvPvt,pasynUser); break;
        case GPIBREN: status = pasynGpib->ren(drvPvt,pasynUser,val); break;
        case GPIBDCL:
            status = pasynGpib->universalCmd(drvPvt,pasynUser,IBDCL);
            break;
        case GPIBLLO:
            status = pasynGpib->universalCmd(drvPvt,pasynUser,IBLLO);
            break;
        case GPIBSDC:
            status = pasynGpib->addressedCmd(drvPvt,pasynUser,IBSDC,1);
            break;
        case GPIBGTL:
            status = pasynGpib->addressedCmd(drvPvt,pasynUser,IBGTL,1);
            break;
        default: status = asynError; break;
    }
    if(status!=asynSuccess) failure = -1;
    if(failure==-1) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static void evFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initEv(eventRecord * pev)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pev, &pev->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pev);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for EV record in param %d\n",
            pev->name, pgpibDpvt->parm);
        pev->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_readEv(eventRecord * pev)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pev);
    int cmdType;
 
    if(pev->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,evFinish);
    }
    return 0;
}

static void evFinish(gpibDpvt * pgpibDpvt,int failure)
{
    unsigned short value;
    eventRecord *pev = ((eventRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pev->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",pev->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        if (sizeof pev->val == sizeof value) {
            char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "hu";
            if (sscanf(pgpibDpvt->msg, format, &value) == 1) {
                memcpy(&pev->val, &value, sizeof pev->val);
                pev->udf = FALSE;
            } else { /* sscanf did not find or assign the parameter */
                failure = -1;
            }
        }
        else {
            if (sscanf(pgpibDpvt->msg, " %39s", (char *)&pev->val) == 1) {
                pev->udf = FALSE;
            } else { /* sscanf did not find or assign the parameter */
                failure = -1;
            }
        }
    }
    if(failure==-1) recGblSetSevr(pev, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static void liFinish(gpibDpvt *pgpibDpvt,int failure);
static void liSrqHandler(void *userPrivate,asynUser *pasynUser,
                epicsInt32 statusByte)
{
    longinRecord *pli = (longinRecord *)userPrivate;
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    pli->val = statusByte;
    pli->udf = FALSE;
    scanOnce(pgpibDpvt->precord);
}

long  devGpib_initLi(longinRecord * pli)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pli, &pli->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pli);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO|GPIBSRQHANDLER))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for LI record in param %d\n",
            pli->name, pgpibDpvt->parm);
        pli->pact = TRUE;
        return S_db_badField;
    }
    if(cmdType&GPIBSRQHANDLER) {
        pdevSupportGpib->registerSrqHandler(pgpibDpvt,liSrqHandler,pli);
    }
    return 0;
}

long  devGpib_readLi(longinRecord * pli)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    int cmdType;
 
/*printf("READ -- %s %d      %p\n", pli->name, pli->val, &pli->val);*/
    if(pli->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSRQHANDLER)  return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,liFinish);
    }
    return 0;
}

static void liFinish(gpibDpvt * pgpibDpvt,int failure)
{
    long value;
    longinRecord *pli = ((longinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pli->name,pasynUser->errorMessage);
            failure = -1;
        }
/*printf("HI THERE -- %s %d\n", pli->name, pli->val); */
/*printf("HI THERE -- %s %p\n", pli->name, &pli->val);*/
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",pli->name);
        failure = -1;
    } else {/* interpret msg with predefined format and write into val/rval */
        char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%ld";
        if (sscanf(pgpibDpvt->msg, format, &value) == 1) {
            pli->val = value; pli->udf = FALSE;
        } else { /* sscanf did not find or assign the parameter */
            failure = -1;
        }
    }
    if(failure==-1) recGblSetSevr(pli, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static int loStart(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initLo(longoutRecord * plo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) plo, &plo->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(plo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for LO record in param %d\n",
            plo->name, pgpibDpvt->parm);
        plo->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_writeLo(longoutRecord * plo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(plo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(plo->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,loStart,genericFinish);
    }
    return 0;
}

static int loStart(gpibDpvt * pgpibDpvt,int failure)
{
    longoutRecord *plo = ((longoutRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgLong(pgpibDpvt, plo->val);
        }
    }
    return failure;
}

static void mbbiFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initMbbi(mbbiRecord * pmbbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    epicsUInt32 *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbi, &pmbbi->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbi);
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO|GPIBEFASTI|GPIBEFASTIW))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for MBBI record in param %d\n",
            pmbbi->name, pgpibDpvt->parm);
        pmbbi->pact = TRUE;
        return S_db_badField;
    }
    if(pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s: init_rec_mbbi: MBBI value list wrong for param"
                " #%d\n", pmbbi->name, pgpibDpvt->parm);
            pmbbi->pact = TRUE;
            return S_db_badField;
        }
        pmbbi->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbi->zrst; /* first name string element */
        val_ptr = &(pmbbi->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strncpy(name_ptr, pdevGpibNames->item[name_ct], sizeof(pmbbi->zrst));
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbi->zrst);
            val_ptr++;
        }
    }
    return 0;
}

long  devGpib_readMbbi(mbbiRecord * pmbbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbi);
    int cmdType;
 
    if(pmbbi->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,mbbiFinish);
    }
    return 0;
}

static void mbbiFinish(gpibDpvt * pgpibDpvt,int failure)
{
    mbbiRecord *pmbbi = ((mbbiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    unsigned long value;
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pmbbi->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
        if(pgpibDpvt->efastVal>=0) {
            pmbbi->rval = pgpibDpvt->efastVal;
        } else {
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",pmbbi->name);
        failure = -1;
    } else {
        char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%lu";
        if (sscanf(pgpibDpvt->msg, format, &value) == 1) {
            pmbbi->rval = value;
        } else {
            /*sscanf did not find or assign the parameter*/
            failure = -1;
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't convert msg >%s<\n",
                pmbbi->name, pgpibDpvt->msg);
        }
    }
    if(failure==-1) recGblSetSevr(pmbbi, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static void mbbiDirectFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    /* do common initialization */
    result = pdevSupportGpib->initRecord(
        (dbCommon *) pmbbiDirect, &pmbbiDirect->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for MBBI_DIRECT record in param %d\n",
            pmbbiDirect->name, pgpibDpvt->parm);
        pmbbiDirect->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_readMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    int cmdType;
 
    if(pmbbiDirect->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,mbbiDirectFinish);
    }
    return 0;
}

static void mbbiDirectFinish(gpibDpvt * pgpibDpvt,int failure)
{
    unsigned long value;
    mbbiDirectRecord *pmbbiDirect = ((mbbiDirectRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pmbbiDirect->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",pmbbiDirect->name);
        failure = -1;
    } else {
        char *format = (pgpibCmd->format) ? (pgpibCmd->format) : "%lu";
        if (sscanf(pgpibDpvt->msg, format, &value) == 1) {
            pmbbiDirect->rval = value;
        } else {
            failure = -1;
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't convert msg >%s<\n",
                pmbbiDirect->name, pgpibDpvt->msg);
        }
    }
    if(failure==-1) recGblSetSevr(pmbbiDirect, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static int mbboStart(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initMbbo(mbboRecord * pmbbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    epicsUInt32 *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbo, &pmbbo->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for MBBO record in param %d\n",
            pmbbo->name, pgpibDpvt->parm);
        pmbbo->pact = TRUE;
        return S_db_badField;
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if (pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s: init_rec_mbbo: MBBO value list wrong for param"
                " #%d\n", pmbbo->name, pgpibDpvt->parm);
            pmbbo->pact = TRUE;
            return S_db_badField;
        }
        pmbbo->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbo->zrst; /* first name string element */
        val_ptr = &(pmbbo->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strncpy(name_ptr, pdevGpibNames->item[name_ct], sizeof(pmbbo->zrst));
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbo->zrst);
            val_ptr++;
        }
    }
    return 2;
}

long  devGpib_writeMbbo(mbboRecord * pmbbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pmbbo->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,mbboStart,genericFinish);
    }
    return 0;
}

static int mbboStart(gpibDpvt * pgpibDpvt,int failure)
{
    mbboRecord *pmbbo = (mbboRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pmbbo->val;
        }
    }
    return failure;
}

static int mbboDirectStart(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initMbboDirect(mbboDirectRecord * pmbboDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *)pmbboDirect,&pmbboDirect->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbboDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for MBBO_DIRECT record in param %d\n",
            pmbboDirect->name, pgpibDpvt->parm);
        pmbboDirect->pact = TRUE;
        return S_db_badField;
    }
    return 2;
}

long  devGpib_writeMbboDirect(mbboDirectRecord * pmbboDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbboDirect);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pmbboDirect->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,
            mbboDirectStart,genericFinish);
    }
    return 0;
}

static int mbboDirectStart(gpibDpvt * pgpibDpvt,int failure)
{
    mbboDirectRecord *pmbboDirect = ((mbboDirectRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbboDirect->rval);
        }
    }
    return failure;
}

static void siFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initSi(stringinRecord * psi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) psi, &psi->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(psi);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for SI record in param %d\n",
            psi->name, pgpibDpvt->parm);
        psi->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_readSi(stringinRecord * psi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(psi);
    int cmdType;
 
    if(psi->pact) return 0;
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,siFinish);
    }
    return 0;
}

static void siFinish(gpibDpvt * pgpibDpvt,int failure)
{
    stringinRecord *psi = ((stringinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if (pgpibCmd->convert) {
        pasynUser->errorMessage[0] = 0;
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                psi->name,pasynUser->errorMessage);
            failure = -1;
        }
    } else if (!pgpibDpvt->msg) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s no msg buffer\n",psi->name);
        failure = -1;
    } else {
        char *format = (pgpibCmd->format) ? pgpibCmd->format : "%39c";
        if(sscanf(pgpibDpvt->msg, format, psi->val) != 1) {
            failure = -1;
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s can't convert msg >%s<\n",
                                                    psi->name, pgpibDpvt->msg);
        }
        psi->udf = FALSE;
    }
    if(failure==-1) recGblSetSevr(psi, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}

static int soStart(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initSo(stringoutRecord * pso)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pso, &pso->out);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pso);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT|GPIBCVTIO))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for SO record in param %d\n",
            pso->name, pgpibDpvt->parm);
        pso->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_writeSo(stringoutRecord * pso)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pso);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pso->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    } else {
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,soStart,genericFinish);
    }
    return 0;
}

static int soStart(gpibDpvt *pgpibDpvt,int failure)
{
    stringoutRecord *pso = ((stringoutRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert) {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgString(pgpibDpvt, pso->val);
        }
    }
    return failure;
}

static int wfStart(gpibDpvt *pgpibDpvt,int failure);
static void wfFinish(gpibDpvt *pgpibDpvt,int failure);
long  devGpib_initWf(waveformRecord * pwf)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gpibCmd *pgpibCmd;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pwf, &pwf->inp);
    if(result) return result;
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pwf);
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if((!pgpibCmd->convert) && (pwf->ftvl!=menuFtypeCHAR)) {
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s ftvl != CHAR but no convert\n", pwf->name);
            pwf->pact = 1;
            return S_db_badField;
        }
    } else if(!(cmdType&(GPIBSOFT|GPIBCVTIO|GPIBWRITE|GPIBCMD|GPIBACMD))) {
        asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
            "%s invalid command type for WF record in param %d\n",
            pwf->name, pgpibDpvt->parm);
        pwf->pact = TRUE;
        return S_db_badField;
    }
    return 0;
}

long  devGpib_readWf(waveformRecord * pwf)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pwf);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pwf->pact) return 0;
    if(cmdType&GPIBSOFT) {
        pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
        return 0;
    }
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,0,wfFinish);
    } else { /*Must be Output Operation*/
        pdevSupportGpib->queueWriteRequest(pgpibDpvt,wfStart,wfFinish);
    }
    return 0;
}

static int wfStart(gpibDpvt * pgpibDpvt,int failure)
{
    waveformRecord *pwf = (waveformRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);

    if(!failure && !pgpibCmd->convert && (pgpibCmd->type&GPIBWRITE)) {
        if(pwf->ftvl!=menuFtypeCHAR) {
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s ftvl != CHAR but no convert\n", pwf->name);
            pwf->pact = 1;
            failure = -1;
        } else {
            failure = pdevSupportGpib->writeMsgString(pgpibDpvt, pwf->bptr);
        }
    }
    return failure;
}

static void wfFinish(gpibDpvt * pgpibDpvt,int failure)
{
    waveformRecord *pwf = (waveformRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int cnvrtStat;
    asynUser *pasynUser = pgpibDpvt->pasynUser;

    if(failure) {; /*do nothing*/
    } else if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if(pgpibCmd->convert) {
        cnvrtStat = pgpibCmd->convert(pgpibDpvt,
            pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        if(cnvrtStat==-1) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,"%s convert failed %s\n",
                pwf->name,pasynUser->errorMessage);
            failure = -1;
        }
        } else if(pwf->ftvl!=menuFtypeCHAR) {
            asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                "%s ftvl != CHAR but no convert\n",pwf->name);
            failure = -1; 
        } else {
            char *format = (pgpibCmd->format) ? pgpibCmd->format : "%s";
            int lenDest = pwf->nelm;
            char *pdest = (char *)pwf->bptr;
            int nchars;

            nchars = epicsSnprintf(pdest,lenDest,format,pgpibDpvt->msg);
            if(nchars>=lenDest) {
                 pdest[lenDest-1] = 0;
                 asynPrint(pgpibDpvt->pasynUser,ASYN_TRACE_ERROR,
                     "%s %d characters were truncated\n",
                      pwf->name,(nchars-lenDest+1));
                 failure = -1;
                 nchars = lenDest;
            }
            pwf->udf = FALSE;
            pwf->nord = nchars;
        }
    }
    if(failure==-1) recGblSetSevr(pwf, READ_ALARM, INVALID_ALARM);
    pdevSupportGpib->completeProcess(pgpibDpvt);
}
