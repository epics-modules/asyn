/* devCommonGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
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
#include <recGbl.h>
#include <dbAccess.h>
#include <alarm.h>
#include <devSup.h>
#include <recSup.h>
#include <callback.h>
#include <drvSup.h>
#include <link.h>
#include <errlog.h>
#include <menuFtype.h>
#include <shareLib.h>

#include <asynDriver.h>
#include <gpibDriver.h>

#include "devSupportGpib.h"
#define epicsExportSharedSymbols
#include "devCommonGpib.h"


static void aiGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initAi(aiRecord * pai)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gDset *pgDset = (gDset *)pai->dset;
    DEVSUPFUN  got_special_linconv = pgDset->funPtr[5];

    result = pdevSupportGpib->initRecord((dbCommon *) pai, &pai->inp);
    if(result) return (result);
    pgpibDpvt = gpibDpvtGet(pai);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for AI record in param %d\n",
	    pai->name, pgpibDpvt->parm);
	pai->pact = TRUE;
	return (S_db_badField);
    }
    if(got_special_linconv) (*got_special_linconv)(pai,TRUE);
    return (0);
}

long epicsShareAPI devGpibLib_readAi(aiRecord * pai)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pai);
    int cmdType;
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];
 
    if(pai->pact) return(got_special_linconv ? 2 : 0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,aiGpibFinish);
    return(got_special_linconv ? 2 : 0);
}

static void aiGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    double value;
    long rawvalue;
    aiRecord *pai = ((aiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    DEVSUPFUN got_special_linconv = ((gDset *) pai->dset)->funPtr[5];
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pai->name);
        failure = 1;
    } else {/* interpret msg with predefined format and write into val/rval */
        int result = 0;
	if(got_special_linconv) {
	    result = sscanf(pgpibDpvt->msg, pgpibCmd->format, &rawvalue);
            if(result==1) {pai->rval = rawvalue; pai->udf = FALSE;}
	} else {
            result = sscanf(pgpibDpvt->msg, pgpibCmd->format, &value);
            if(result==1) {pai->val = value; pai->udf = FALSE;}
	}
        if(result!=1) failure = 1;
    }
    if(failure) recGblSetSevr(pai, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void aoGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initAo(aoRecord * pao)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pao, &pao->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pao);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for AO record in param %d\n",
	    pao->name, pgpibDpvt->parm);
	pao->pact = TRUE;
	return (S_db_badField);
    }
    if(got_special_linconv) (*got_special_linconv)(pao,TRUE);
    return (0);
}

long epicsShareAPI devGpibLib_writeAo(aoRecord * pao)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pao);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
    DEVSUPFUN  got_special_linconv = ((gDset *) pao->dset)->funPtr[5];
 
    if(pao->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else { 
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            if (got_special_linconv) {
                failure = pdevSupportGpib->writeMsgLong(pgpibDpvt,pao->rval);
            } else {
                failure = pdevSupportGpib->writeMsgDouble(pgpibDpvt,pao->oval);
            }
        }
    }
    if(failure) recGblSetSevr(pao, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,aoGpibFinish);
    return(0);
}

static void aoGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void biGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initBi(biRecord * pbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbi, &pbi->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbi);
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBEFASTI|GPIBEFASTIW))) {
        printf("%s invalid command type for BI record in param %d\n",
	    pbi->name, pgpibDpvt->parm);
	pbi->pact = TRUE;
	return (S_db_badField);
    }
    if(pdevGpibNames) {
        if (pbi->znam[0] == 0) strcpy(pbi->znam, pdevGpibNames->item[0]);
        if (pbi->onam[0] == 0) strcpy(pbi->onam, pdevGpibNames->item[1]);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readBi(biRecord * pbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbi);
    int cmdType;
 
    if(pbi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,biGpibFinish);
    return(0);
}

static void biGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    biRecord *pbi = ((biRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    unsigned long value;
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pbi->name);
        failure = 1;
    } else {/* interpret msg with predefined format and write into rval */
        if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
            if(pgpibDpvt->efastVal>0) {
                pbi->rval = pgpibDpvt->efastVal;
            } else {
                failure = 1;
            }
        } else {
            if(sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
                pbi->rval = value;
            } else {
                /* sscanf did not find or assign the parameter*/
                failure = 1;
                if (*pdevGpibParmBlock->debugFlag) {
                        printf("%s can't convert msg >%s<\n",
                            pbi->name, pgpibDpvt->msg);
                }
            }
        }
    }
    if(failure) recGblSetSevr(pbi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void boGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
static long writeBoSpecial(boRecord * pbo);
static void boGpibsWorkSpecial(gpibDpvt *pgpibDpvt,int timeoutOccured);
static char *ifcName[] = {"noop", "IFC", 0};
static char *renName[] = {"drop REN", "assert REN", 0};
static char *dclName[] = {"noop", "DCL", "0"};
static char *lloName[] = {"noop", "LLO", "0"};
static char *sdcName[] = {"noop", "SDC", "0"};
static char *gtlName[] = {"noop", "GTL", "0"};
static char *resetName[] = {"noop", "resetLink", "0"};

long epicsShareAPI devGpibLib_initBo(boRecord * pbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gDset *pgDset = (gDset *)pbo->dset;
    devGpibNames *pdevGpibNames;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pbo, &pbo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBIFC|GPIBREN|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBRESETLNK)) {
        pgDset->funPtr[4] = writeBoSpecial;
    } else if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT))) {
        printf("%s invalid command type for BO record in param %d\n",
	    pbo->name, pgpibDpvt->parm);
	pbo->pact = TRUE;
	return (S_db_badField);
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if (pdevGpibNames) {
        if (pbo->znam[0] == 0) strcpy(pbo->znam, pdevGpibNames->item[0]);
        if (pbo->onam[0] == 0) strcpy(pbo->onam, pdevGpibNames->item[1]);
    } else {    /* supply defaults for menus */
        char **papname = 0;
        switch (cmdType) {
        case GPIBIFC: papname = ifcName; break;
        case GPIBREN: papname = renName; break;
        case GPIBDCL: papname = dclName; break;
        case GPIBLLO: papname = lloName; break;
        case GPIBSDC: papname = sdcName; break;
        case GPIBGTL: papname = gtlName; break;
        case GPIBRESETLNK: papname = resetName; break;
        default:
            break;
        }
        if (papname) {
            if (pbo->znam[0] == 0) strcpy(pbo->znam, papname[0]);
            if (pbo->onam[0] == 0) strcpy(pbo->onam, papname[1]);
        }
    }
    return (2);
}

long epicsShareAPI devGpibLib_writeBo(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pbo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pbo->val;
        }
    }
    if(failure) recGblSetSevr(pbo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,boGpibFinish);
    return(0);
}

static void boGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static long writeBoSpecial(boRecord * pbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pbo);
    int cmdType = gpibCmdGetType(pgpibDpvt);
 
    if(pbo->pact) return(0);
    if(cmdType&(GPIBIFC|GPIBDCL|GPIBLLO|GPIBSDC|GPIBGTL|GPIBRESETLNK)) {
        if(pbo->val==0) return(0);
    }
    pdevSupportGpib->queueRequest(pgpibDpvt,boGpibsWorkSpecial);
    return(0);
}

static void boGpibsWorkSpecial(gpibDpvt *pgpibDpvt,int timeoutOccured)
{
    boRecord *precord = (boRecord *)pgpibDpvt->precord;
    int val = (int)precord->val;
    int cmdType = gpibCmdGetType(pgpibDpvt);
    gpibDriverUser *pgpibDriverUser = pgpibDpvt->pgpibDriverUser;
    void *pdrvPvt = pgpibDpvt->pgpibDriverUserPvt;
    asynUser *pasynUser = pgpibDpvt->pasynUser;
    asynStatus status = asynSuccess;
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if(!pgpibDpvt->pgpibDriverUser) {
        failure = 1;
        printf("%s pgpibDriverUser is 0\n",precord->name);
    } else switch(gpibCmdTypeNoEOS(cmdType)) {
        case GPIBIFC: status = pgpibDriverUser->ifc(pdrvPvt,pasynUser); break;
        case GPIBREN: status = pgpibDriverUser->ren(pdrvPvt,pasynUser,val); break;
        case GPIBDCL:
            status = pgpibDriverUser->universalCmd(pdrvPvt,pasynUser,IBDCL);
            break;
        case GPIBLLO:
            status = pgpibDriverUser->universalCmd(pdrvPvt,pasynUser,IBLLO);
            break;
        case GPIBSDC:
            status = pgpibDriverUser->addressedCmd(pdrvPvt,pasynUser,
                pgpibDpvt->gpibAddr,IBSDC,1);
            break;
        case GPIBGTL:
            status = pgpibDriverUser->addressedCmd(pdrvPvt,pasynUser,
                pgpibDpvt->gpibAddr,IBGTL,1);
            break;
        case GPIBRESETLNK:
        {
            asynDriver *pasynDriver = pgpibDpvt->pasynDriver;
            void *pasynDriverPvt = pgpibDpvt->pasynDriverPvt;

            assert(pasynDriver);
            status = pgpibDpvt->pasynDriver->disconnect(
                pasynDriverPvt,pasynUser);
            if(status==asynSuccess) status = pgpibDpvt->pasynDriver->connect(
                pasynDriverPvt,pasynUser);
        }
        break;
        default: status = -1;
    }
    if(status!=asynSuccess) failure = 1;
    if(failure) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void evGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initEv(eventRecord * pev)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pev, &pev->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pev);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for EV record in param %d\n",
	    pev->name, pgpibDpvt->parm);
	pev->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readEv(eventRecord * pev)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pev);
    int cmdType;
 
    if(pev->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,evGpibFinish);
    return(0);
}

static void evGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    unsigned short value;
    eventRecord *pev = ((eventRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pev->name);
        failure = 1;
    } else {/* interpret msg with predefined format and write into val/rval */
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pev->val = value;
            pev->udf = FALSE;
	} else { /* sscanf did not find or assign the parameter */
	    failure = 1;
	}
    }
    if(failure) recGblSetSevr(pev, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void liGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
static long readLiSrq(longinRecord * pli);
static void liSrqHandler(void *userPrivate,int gpibAddr,int statusByte);
long epicsShareAPI devGpibLib_initLi(longinRecord * pli)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gDset *pgDset = (gDset *)pli->dset;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pli, &pli->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pli);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBSRQHANDLER))) {
        printf("%s invalid command type for LI record in param %d\n",
	    pli->name, pgpibDpvt->parm);
	pli->pact = TRUE;
	return (S_db_badField);
    }
    if(cmdType&GPIBSRQHANDLER) {
        pgDset->funPtr[4] = readLiSrq;
        pdevSupportGpib->registerSrqHandler(pgpibDpvt,liSrqHandler,pli);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readLi(longinRecord * pli)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    int cmdType;
 
    if(pli->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,liGpibFinish);
    return(0);
}

static void liGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    long value;
    longinRecord *pli = ((longinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pli->name);
        failure = 1;
    } else {/* interpret msg with predefined format and write into val/rval */
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pli->val = value; pli->udf = FALSE;
	} else { /* sscanf did not find or assign the parameter */
	    failure = 1;
	}
    }
    if(failure) recGblSetSevr(pli, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static long readLiSrq(longinRecord * pli)
{
    pli->udf = FALSE;
    return(0);
}

static void liSrqHandler(void *userPrivate,int gpibAddr,int statusByte)
{
    longinRecord *pli = (longinRecord *)userPrivate;
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pli);
    pli->val = statusByte;
    requestProcessCallback(pgpibDpvt);
}

static void loGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initLo(longoutRecord * plo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) plo, &plo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(plo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for LO record in param %d\n",
	    plo->name, pgpibDpvt->parm);
	plo->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_writeLo(longoutRecord * plo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(plo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(plo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else {
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgLong(pgpibDpvt, plo->val);
        }
    }
    if(failure) recGblSetSevr(plo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,loGpibFinish);
    return(0);
}

static void loGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbbiGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initMbbi(mbbiRecord * pmbbi)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    unsigned long *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbi, &pmbbi->inp);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbi);
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT|GPIBEFASTI|GPIBEFASTIW))) {
        printf("%s invalid command type for MBBI record in param %d\n",
	    pmbbi->name, pgpibDpvt->parm);
	pmbbi->pact = TRUE;
	return (S_db_badField);
    }
    if(pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            printf("%s: init_rec_mbbi: MBBI value list wrong for param"
                " #%d\n", pmbbi->name, pgpibDpvt->parm);
            pmbbi->pact = TRUE;
            return (S_db_badField);
        }
        pmbbi->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbi->zrst; /* first name string element */
        val_ptr = &(pmbbi->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strcpy(name_ptr, pdevGpibNames->item[name_ct]);
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbi->zrst);
            val_ptr++;
        }
    }
    return (0);
}

long epicsShareAPI devGpibLib_readMbbi(mbbiRecord * pmbbi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbi);
    int cmdType;
 
    if(pmbbi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,mbbiGpibFinish);
    return(0);
}

static void mbbiGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    mbbiRecord *pmbbi = ((mbbiRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    unsigned long value;
    int failure = 0;

    if(timeoutOccured) {
	failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pmbbi->name);
	failure = 1;
    } else {/* interpret msg with predefined format and write into rval */
	if(pgpibCmd->type&(GPIBEFASTI|GPIBEFASTIW)) {
            if(pgpibDpvt->efastVal>0) {
                pmbbi->rval = pgpibDpvt->efastVal;
            } else {
                failure = 1;
            }
        } else {
            if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
                pmbbi->rval = value;
            } else {
                /*sscanf did not find or assign the parameter*/
	        failure = 1;
                if (*pdevGpibParmBlock->debugFlag) {
                        printf("%s can't convert msg >%s<\n",
                            pmbbi->name, pgpibDpvt->msg);
                }
            }
        }
    }
    if(failure) recGblSetSevr(pmbbi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbbiDirectGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    /* do common initialization */
    result = pdevSupportGpib->initRecord(
        (dbCommon *) pmbbiDirect, &pmbbiDirect->inp);
    if(result) return (result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for MBBI_DIRECT record in param %d\n",
	    pmbbiDirect->name, pgpibDpvt->parm);
	pmbbiDirect->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readMbbiDirect(mbbiDirectRecord * pmbbiDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbiDirect);
    int cmdType;
 
    if(pmbbiDirect->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,mbbiDirectGpibFinish);
    return(0);
}

static void mbbiDirectGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    unsigned long value;
    mbbiDirectRecord *pmbbiDirect = ((mbbiDirectRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    devGpibParmBlock *pdevGpibParmBlock = pgpibDpvt->pdevGpibParmBlock;
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s no msg buffer\n",pmbbiDirect->name);
        failure = 1;
    } else {
        if (sscanf(pgpibDpvt->msg, pgpibCmd->format, &value) == 1) {
            pmbbiDirect->rval = value;
        } else {
            failure = 1;
            if (*pdevGpibParmBlock->debugFlag) {
                printf("%s can't convert msg >%s<\n",
                    pmbbiDirect->name, pgpibDpvt->msg);
            }
        }
    }
    if(failure) recGblSetSevr(pmbbiDirect, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbboGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initMbbo(mbboRecord * pmbbo)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    devGpibNames *pdevGpibNames;
    int name_ct;        /* for filling in the name strings */
    char *name_ptr;     /* index into name list array */
    unsigned long *val_ptr;     /* indev into the value list array */

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pmbbo, &pmbbo->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbbo);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBEFASTO|GPIBSOFT))) {
        printf("%s invalid command type for MBBO record in param %d\n",
	    pmbbo->name, pgpibDpvt->parm);
	pmbbo->pact = TRUE;
	return (S_db_badField);
    }
    pdevGpibNames = devGpibNamesGet(pgpibDpvt);
    if (pdevGpibNames) {
        if (pdevGpibNames->value == NULL) {
            printf("%s: init_rec_mbbo: MBBO value list wrong for param"
               " #%d\n", pmbbo->name, pgpibDpvt->parm);
            pmbbo->pact = TRUE;
            return (S_db_badField);
        }
        pmbbo->nobt = pdevGpibNames->nobt;
        name_ct = 0;    /* current name string element */
        name_ptr = pmbbo->zrst; /* first name string element */
        val_ptr = &(pmbbo->zrvl);       /* first value element */
        while (name_ct < pdevGpibNames->count) {
            if (name_ptr[0] == 0) {
                strcpy(name_ptr, pdevGpibNames->item[name_ct]);
                *val_ptr = pdevGpibNames->value[name_ct];
            }
            name_ct++;
            name_ptr += sizeof(pmbbo->zrst);
            val_ptr++;
        }
    }
    return (2);
}

long epicsShareAPI devGpibLib_writeMbbo(mbboRecord * pmbbo)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbbo);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pmbbo->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else {
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbbo->rval);
        } else if (pgpibCmd->type&GPIBEFASTO) {
            pgpibDpvt->efastVal = pmbbo->val;
        }
    }
    if(failure) recGblSetSevr(pmbbo, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,mbboGpibFinish);
    return(0);
}

static void mbboGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void mbboDirectGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initMbboDirect(mbboDirectRecord * pmbboDirect)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *)pmbboDirect,&pmbboDirect->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pmbboDirect);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for MBBO_DIRECT record in param %d\n",
	    pmbboDirect->name, pgpibDpvt->parm);
	pmbboDirect->pact = TRUE;
	return (S_db_badField);
    }
    return (2);
}

long epicsShareAPI devGpibLib_writeMbboDirect(mbboDirectRecord * pmbboDirect)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pmbboDirect);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pmbboDirect->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else {    /* generate msg using predefined format and current val */
        if (pgpibCmd->type&GPIBWRITE) {
            failure = pdevSupportGpib->writeMsgULong(pgpibDpvt,pmbboDirect->rval);
        }
    }
    if(failure) recGblSetSevr(pmbboDirect, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,mbboDirectGpibFinish);
    return(0);
}

static void mbboDirectGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void siGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initSi(stringinRecord * psi)
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
    if(!(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD|GPIBSOFT))) {
        printf("%s invalid command type for SI record in param %d\n",
	    psi->name, pgpibDpvt->parm);
	psi->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readSi(stringinRecord * psi)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(psi);
    int cmdType;
 
    if(psi->pact) return(0);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    pdevSupportGpib->queueReadRequest(pgpibDpvt,siGpibFinish);
    return(0);
}

static void siGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    stringinRecord *psi = ((stringinRecord *) (pgpibDpvt->precord));
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if (pgpibCmd->convert) {
	pgpibCmd->convert(pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
    } else if (!pgpibDpvt->msg) {
        printf("%s  no msg buffer\n",psi->name);
        failure = 1;
    } else {/* interpret msg with predefined format and write into val/rval */
        strncpy(psi->val, pgpibDpvt->msg, (sizeof(psi->val)-1));
        psi->val[sizeof(psi->val)-1] = 0;
        psi->udf = FALSE;
    }
    if(failure) recGblSetSevr(psi, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void soGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initSo(stringoutRecord * pso)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pso, &pso->out);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pso);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(!(cmdType&(GPIBWRITE|GPIBCMD|GPIBACMD|GPIBSOFT))) {
        printf("%s invalid command type for SO record in param %d\n",
	    pso->name, pgpibDpvt->parm);
	pso->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_writeSo(stringoutRecord * pso)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pso);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pso->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if (pgpibCmd->convert) {
        int cnvrtStat;
        cnvrtStat = pgpibCmd->convert(
            pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
        if(cnvrtStat==-1) failure = 1;
    } else {    /* generate msg using predefined format and current val */
        /* tstraumann:  better test if there was space allocated */
        if (pgpibCmd->type&GPIBWRITE) {/* only if needs formatting */
            failure = pdevSupportGpib->writeMsgString(pgpibDpvt, pso->val);
        }
    }
    if(failure) recGblSetSevr(pso, WRITE_ALARM, INVALID_ALARM);
    if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,soGpibFinish);
    return(0);
}

static void soGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    dbCommon *precord = pgpibDpvt->precord;

    if(timeoutOccured) recGblSetSevr(precord, WRITE_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}

static void wfGpibFinish(gpibDpvt *pgpibDpvt,int timeoutOccured);
long epicsShareAPI devGpibLib_initWf(waveformRecord * pwf)
{
    long result;
    int cmdType;
    gpibDpvt *pgpibDpvt;
    gpibCmd *pgpibCmd;

    /* do common initialization */
    result = pdevSupportGpib->initRecord((dbCommon *) pwf, &pwf->inp);
    if(result) return(result);
    /* make sure the command type makes sense for the record type */
    pgpibDpvt = gpibDpvtGet(pwf);
    pgpibCmd = gpibCmdGet(pgpibDpvt);
    cmdType = gpibCmdGetType(pgpibDpvt);
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if((!pgpibCmd->convert) && (pwf->ftvl!=menuFtypeCHAR)) {
            printf("%s ftvl != CHAR but "
                "!pgpibCmd->convert) && (GPIBREAD|GPIBREADW|GPIBRAWREAD)\n",
                pwf->name);
            pwf->pact = 1;
            return (S_db_badField);
        }
    } else if(!(cmdType&(GPIBSOFT|GPIBWRITE|GPIBCMD|GPIBACMD))) {
        printf("%s invalid command type for WF record in param %d\n",
	    pwf->name, pgpibDpvt->parm);
	pwf->pact = TRUE;
	return (S_db_badField);
    }
    return (0);
}

long epicsShareAPI devGpibLib_readWf(waveformRecord * pwf)
{
    gpibDpvt *pgpibDpvt = gpibDpvtGet(pwf);
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;
 
    if(pwf->pact) return(0);
    if(cmdType&GPIBSOFT) return pdevSupportGpib->processGPIBSOFT(pgpibDpvt);
    if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        pdevSupportGpib->queueReadRequest(pgpibDpvt,wfGpibFinish);
    } else { /*Must be Output Operation*/
        if (pgpibCmd->convert) {
            int cnvrtStat;
            cnvrtStat = pgpibCmd->convert(
                pgpibDpvt, pgpibCmd->P1, pgpibCmd->P2, pgpibCmd->P3);
            if(cnvrtStat==-1) failure = 1;
        } else if (pgpibCmd->type&GPIBWRITE) {
            if(pwf->ftvl!=menuFtypeCHAR) {
                printf("%s ftvl != CHAR but pgpibCmd->type is GPIBWRITE\n",
                    pwf->name);
                pwf->pact = 1;
                failure = 1;
            } else {
                if(pgpibCmd->msgLen < pwf->nord) {
                    printf("%s msgLen %d but pwf->nord %lu\n",
                        pwf->name,pgpibCmd->msgLen,pwf->nord);
                    failure = 1;
                } else {
                    strncpy(pgpibDpvt->msg,(char *)pwf->bptr,pwf->nord);
                    pgpibDpvt->msg[pwf->nord -1] = 0;
                }
            }
        }
        if(failure) recGblSetSevr(pwf, WRITE_ALARM, INVALID_ALARM);
        if(!failure) pdevSupportGpib->queueWriteRequest(pgpibDpvt,wfGpibFinish);
    }
    return(0);
}

static void wfGpibFinish(gpibDpvt * pgpibDpvt,int timeoutOccured)
{
    waveformRecord *pwf = (waveformRecord *)pgpibDpvt->precord;
    gpibCmd *pgpibCmd = gpibCmdGet(pgpibDpvt);
    int cmdType = gpibCmdGetType(pgpibDpvt);
    int failure = 0;

    if(timeoutOccured) {
        failure = 1;
    } else if(cmdType&(GPIBREAD|GPIBREADW|GPIBRAWREAD)) {
        if(pgpibCmd->convert) {
            pgpibCmd->convert(pgpibDpvt,pgpibCmd->P1,pgpibCmd->P2,pgpibCmd->P3);
        } else {
            int lenmsg = strlen(pgpibDpvt->msg);
            if(lenmsg >= pwf->nelm) {
                printf("%s message length %d but pwf->nelm %lu\n",
                    pwf->name,lenmsg,pwf->nelm);
                pgpibDpvt->msg[pwf->nelm-1] = 0;
                failure = 1;
                lenmsg = pwf->nelm - 1;
            }
            strcpy((char *)pwf->bptr,pgpibDpvt->msg);
            pwf->nord = lenmsg+1;
        }
    }
    if(failure) recGblSetSevr(pwf, READ_ALARM, INVALID_ALARM);
    requestProcessCallback(pgpibDpvt);
}
