/* devAoAsynInt32.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
    Author:  Mark Rivers
    28-June-2004 Converted from MPF to plain driver calls 
*/

#include <stdlib.h>
#include <stddef.h>
#include <stdio.h>

#include <alarm.h>
#include <recGbl.h>
#include <dbAccess.h>
#include <dbDefs.h>
#include <link.h>
#include <epicsPrint.h>
#include <epicsExport.h>
#include <cantProceed.h>
#include <dbCommon.h>
#include <aoRecord.h>
#include <recSup.h>
#include <devSup.h>

#include "asynDriver.h"
#include "asynInt32.h"
#include "asynUtils.h"

typedef struct {
   aoRecord *pao;
   asynUser *pasynUser;
   asynInt32 *pasynInt32;
   void *asynInt32Pvt;
   int canBlock;
} devAoAsynInt32Pvt;

static long init_record(aoRecord *pao);
static long convert(aoRecord *pao, int pass);
static long write_ao(aoRecord *pao);
static void callback_ao(asynUser *pasynUser);

struct aodset { /* analog input dset */
    long          number;
    DEVSUPFUN     dev_report;
    DEVSUPFUN     init;
    DEVSUPFUN     init_record; /*returns: (0,2)=>(success,success no convert)*/
    DEVSUPFUN     get_ioint_info;
    DEVSUPFUN     write_ao;/*(0)=>(success ) */
    DEVSUPFUN     special_linconv;
} devAoAsynInt32 = {
    6,
    NULL, 
    NULL,
    init_record,
    NULL,
    write_ao,
    convert
};

epicsExportAddress(dset, devAoAsynInt32);


static long init_record(aoRecord *pao)
{
    devAoAsynInt32Pvt *pPvt;
    char *port, *userParam;
    int card, signal;
    asynStatus status;
    asynUser *pasynUser;
    asynInterface *pasynInterface;

    pPvt = callocMustSucceed(1, sizeof(*pPvt), "devAoAsynInt32::init_record");
    pao->dpvt = pPvt;
    pPvt->pao = pao;

    /* Create asynUser */
    pasynUser = pasynManager->createAsynUser(callback_ao, 0);
    pasynUser->userPvt = pPvt;
    pPvt->pasynUser = pasynUser;

    /* Parse the VME link to get signal and port */
    status = pasynUtils->parseVmeIo(pasynUser, &pao->out, &card, &signal, 
                                    &port, &userParam);
    if (status != asynSuccess) {
        errlogPrintf("devAoAsynInt32::initCommon, error in VME link %s\n",
                     pasynUser->errorMessage);
        goto bad;
    }
    if (signal<0 || signal>7) {
        errlogPrintf("devAoAsynInt32::init_record %s Illegal OUT signal field "
                     "(0-7) = %d\n",
                     pao->name, signal);
        goto bad;
    }

    /* Connect to device */
    status = pasynManager->connectDevice(pasynUser, port, signal);
    if (status != asynSuccess) {
        errlogPrintf("devAoAsynInt32::init_record, connectDevice failed\n");
        goto bad;
    }

    /* Get the asynInt32 interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
        errlogPrintf("devAoAsynInt32::init_record, find asynInt32 interface failed\n");
        goto bad;
    }
    pPvt->pasynInt32 = (asynInt32 *)pasynInterface->pinterface;
    pPvt->asynInt32Pvt = pasynInterface->drvPvt;

    /* Determine if device can block */
    pasynManager->canBlock(pasynUser, &pPvt->canBlock);

    /* set linear conversion slope */
    pao->eslo = (pao->eguf - pao->egul)/4095.0;
    return(2); /* Don't convert from rval */
bad:
   pao->pact=1;
   return(2);
}

static long write_ao(aoRecord *pao)
{
    devAoAsynInt32Pvt *pPvt = (devAoAsynInt32Pvt *)pao->dpvt;
    int status;

    status = pasynManager->queueRequest(pPvt->pasynUser, 0, 0);
    if (status != asynSuccess) {
        asynPrint(pPvt->pasynUser, ASYN_TRACE_ERROR,
                  "devAoAsynInt32::write_ao, error queuing request %s\n", 
                  pPvt->pasynUser->errorMessage);
    }
    return status;
}

static void callback_ao(asynUser *pasynUser)
{
    devAoAsynInt32Pvt *pPvt = (devAoAsynInt32Pvt *)pasynUser->userPvt;
    aoRecord *pao = (aoRecord *)pPvt->pao;
    int status;

    status = pPvt->pasynInt32->write(pPvt->asynInt32Pvt, pPvt->pasynUser, 
                                     pao->rval);
    if (status == 0)
        pao->udf=0;
    else
        recGblSetSevr(pao,READ_ALARM,INVALID_ALARM);
}

static long convert(aoRecord *pao, int pass)
{
    if (pass==0) return(0);
    /* set linear conversion slope */
    pao->eslo = (pao->eguf - pao->egul)/4095.0;
    return 0;
}

