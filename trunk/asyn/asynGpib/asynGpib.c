/* asynGpib.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* epics includes */
#include <epicsAssert.h>
#include <ellLib.h>
#include <errlog.h>
#include <taskwd.h>
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <epicsTimer.h>
#include <cantProceed.h>
#include <iocsh.h>

#include <epicsExport.h>
#include "asynDriver.h"
#include "asynOctet.h"
#include "asynGpibDriver.h"

#define BOOL int
#ifndef TRUE
#define TRUE 1
#endif
#ifndef FALSE
#define FALSE 0
#endif
#define SRQTIMEOUT .01
#define MAX_POLL 5

typedef struct gpibBase {
    ELLLIST gpibPvtList;
    epicsTimerQueueId timerQueue;
}gpibBase;
static gpibBase *pgpibBase = 0;

typedef struct pollNode {
    int                    pollIt;
    int                    statusByte;
    asynUser               *pasynUser;
    asynCommon             *pasynCommon;
    void                   *drvPvt;
}pollNode;

typedef struct pollListPrimary {
    pollNode primary;
    BOOL     pollSecondary;
    pollNode secondary[NUM_GPIB_ADDRESSES];
}pollListPrimary;

typedef struct gpibPvt {
    ELLNODE node;
    const char *portName;
    epicsMutexId lock;
    int         attributes;
    pollListPrimary pollList[NUM_GPIB_ADDRESSES];
    int pollRequestIsQueued;
    asynGpibPort *pasynGpibPort;
    void *asynGpibPortPvt;
    asynUser *pasynUser;
    asynInterface common;
    asynInterface octet;
    asynInterface gpib;
    asynInterface int32;
    void          *asynInt32Pvt;
    int           eoslen;
    char          eos;
    void          *pasynPvt;   /*For registerInterruptSource*/
}gpibPvt;

#define GETgpibPvtasynGpibPort \
    gpibPvt *pgpibPvt = (gpibPvt *)drvPvt; \
    asynGpibPort *pasynGpibPort; \
    assert(pgpibPvt); \
    pasynGpibPort = pgpibPvt->pasynGpibPort; \
    assert(pasynGpibPort);

/* forward reference to internal methods */
static void gpibInit(void);
static gpibPvt *locateGpibPvt(const char *portName);
static asynStatus getAddr(gpibPvt *pgpibPvt,asynUser *pasynUser,
           int *addr, int *primary,int *secondary, BOOL *isPrimary);
static void exceptionHandler(asynUser *pasynUser,asynException exception);
static void pollOne(asynUser *pasynUser,gpibPvt *pgpibPvt,
    asynGpibPort *pasynGpibPort,pollNode *ppollNode,int addr);
static void srqPoll(asynUser *pasynUser);
/*asynCommon methods */
static void report(void *drvPvt,FILE *fd,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
/*asynOctet methods */
static asynStatus writeIt(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t maxchars,size_t *nbytesTransfered);
static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason);
static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser);
static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen);
/*asynGpib methods*/
static asynStatus addressedCmd(void *drvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus universalCmd(void *drvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *drvPvt,asynUser *pasynUser);
static asynStatus ren (void *drvPvt,asynUser *pasynUser, int onOff);
static asynStatus pollAddr(void *drvPvt,asynUser *pasynUser, int onOff);
/* The following are called by low level gpib drivers */
static void *registerPort(
        const char *portName,
        int attributes,int autoConnect,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize);
static void srqHappened(void *pgpibvt);

static asynCommon common = {
   report,connect,disconnect
};

static asynOctet octet = {
    writeIt,readIt,gpibFlush, 0,0,setInputEos, getInputEos,0,0
};

static asynGpib gpib = {
    addressedCmd, universalCmd, ifc, ren, pollAddr, registerPort, srqHappened
};

/*asynInt32Base implements all asynInt32 methods*/
static asynInt32 int32 = {0,0,0,0,0};

epicsShareDef asynGpib *pasynGpib = &gpib;

/*internal methods */
static void gpibInit(void)
{
    if(pgpibBase) return;
    pgpibBase = callocMustSucceed(1,sizeof(gpibPvt),"gpibInit");
    ellInit(&pgpibBase->gpibPvtList);
    pgpibBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static gpibPvt *locateGpibPvt(const char *portName)
{
    gpibPvt *pgpibPvt = (gpibPvt *)ellFirst(&pgpibBase->gpibPvtList);
    while(pgpibPvt) {
        if(strcmp(portName,pgpibPvt->portName)==0) return(pgpibPvt);
        pgpibPvt = (gpibPvt *)ellNext(&pgpibPvt->node);
    }
    return(0);
}

static asynStatus getAddr(gpibPvt *pgpibPvt,asynUser *pasynUser,
           int *addr, int *primary,int *secondary, BOOL *isPrimary)
{
    asynStatus status;

    status = pasynManager->getAddr(pasynUser,addr);
    if(status!=asynSuccess) return status;
    if(*addr==-1) {
        if(pgpibPvt->attributes&ASYN_MULTIDEVICE) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "%s asynGpib addr %d is illegal",
                 pgpibPvt->portName,*addr);
            return asynError;
        }
        *primary = *addr = 0; *isPrimary = TRUE;
        return asynSuccess;
    } else if(*addr<100) {
        if(*addr>=NUM_GPIB_ADDRESSES) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "%s asynGpib addr %d is illegal",
                 pgpibPvt->portName,*addr);
            return asynError;
        }
        *primary = *addr; *isPrimary = TRUE;
        return asynSuccess;
    }
    *primary = *addr/100; *secondary = *primary%100;
    if(*primary>=NUM_GPIB_ADDRESSES || *secondary>=NUM_GPIB_ADDRESSES) {
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
            "%s asynGpib addr %d is illegal",
             pgpibPvt->portName,*addr);
        return asynError;
    }
    *isPrimary = FALSE;
    return asynSuccess;
}

static void exceptionHandler(asynUser *pasynUser,asynException exception)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pasynUser->userPvt;
    asynGpibPort *pasynGpibPort = pgpibPvt->pasynGpibPort;
    asynStatus status;

    if(exception!=asynExceptionConnect) return;
    status = pasynGpibPort->srqEnable(pgpibPvt->asynGpibPortPvt,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s asynGpib:pollAddr srqEnable %s\n",
            pgpibPvt->portName,pasynUser->errorMessage);
    }
}

/* NOTE FOR SINGLE ADDRESS CONTROLLER
* The asynUser must specify addr = 0 or SRQs will not work.
*/
static void pollOne(asynUser *pasynUser,gpibPvt *pgpibPvt,
    asynGpibPort *pasynGpibPort,pollNode *ppollNode,int addr)
{
    asynStatus status;
    int statusByte = 0;
    int isConnected=0, isEnabled=0, isAutoConnect = 0;
    
    status = pasynManager->isEnabled(ppollNode->pasynUser,&isEnabled);
    if(status==asynSuccess)
        status = pasynManager->isConnected(ppollNode->pasynUser,&isConnected);
    if(status==asynSuccess)
        status = pasynManager->isAutoConnect(ppollNode->pasynUser,&isAutoConnect);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d asynGpib:srqPoll %s\n",
            pgpibPvt->portName,addr,pasynUser->errorMessage);
        return;
    }
    if(isEnabled && (!isConnected && isAutoConnect)) {
        status = ppollNode->pasynCommon->connect(
            ppollNode->drvPvt,ppollNode->pasynUser);
        if(status==asynSuccess)
            status = pasynManager->isConnected(ppollNode->pasynUser,&isConnected);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s addr %d asynGpib:srqPoll %s\n",
                pgpibPvt->portName,addr,pasynUser->errorMessage);
            return;
        }
    }
    if(!isEnabled || !isConnected) {
        asynPrint(pasynUser,ASYN_TRACE_FLOW,
            "%s addr %d asynGpib:srqPoll but can not connect\n",
            pgpibPvt->portName,addr);
        return;
    }
    status = pasynGpibPort->serialPoll(
        pgpibPvt->asynGpibPortPvt,addr,SRQTIMEOUT,&statusByte);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s addr %d asynGpib:srqPoll serialPoll %s\n",
            pgpibPvt->portName,addr,
            (status==asynTimeout ? "timeout" : "error"));
        return;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s asynGpib:srqPoll serialPoll addr %d statusByte %2.2x\n",
        pgpibPvt->portName,addr,statusByte);
    if(statusByte&0x40) {
        ELLLIST            *pclientList;
        interruptNode      *pnode;
        asynInt32Interrupt *pinterrupt;

        status = pasynManager->interruptStart(pgpibPvt->asynInt32Pvt,&pclientList);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s addr %d asynGpib:srqPoll interruptStart\n",
                pgpibPvt->portName,addr);
            return;
        }
        pnode = (interruptNode *)ellFirst(pclientList);
        while (pnode) {
            asynUser *pasynUser;
            int      userAddr,primary,secondary;
            BOOL isPrimary;

            pinterrupt = pnode->drvPvt;
            pasynUser = pinterrupt->pasynUser;
            status = getAddr(pgpibPvt,pasynUser,&userAddr,
                &primary,&secondary,&isPrimary);
            if(status!=asynSuccess) {
                asynPrint(pasynUser,ASYN_TRACE_ERROR,
                    "%s addr %d asynGpib:srqPoll getAddr %s\n",
                    pgpibPvt->portName,addr,pasynUser->errorMessage);
            } else if(userAddr==addr && pasynUser->reason==ASYN_REASON_SIGNAL) {
                    pinterrupt->callback(pinterrupt->userPvt,
                        pinterrupt->pasynUser, statusByte);
            }
            pnode = (interruptNode *)ellNext(&pnode->node);
        }
        pasynManager->interruptEnd(pgpibPvt->asynInt32Pvt);
    }
}

static void srqPoll(asynUser *pasynUser)
{
    void       *drvPvt = pasynUser->userPvt;
    asynStatus status;
    int        srqStatus= 0;
    int        primary,secondary,ntrys;
    GETgpibPvtasynGpibPort

    epicsMutexMustLock(pgpibPvt->lock);
    if(!pgpibPvt->pollRequestIsQueued) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s asynGpib:srqPoll but !pollRequestIsQueued. Why?\n",
            pgpibPvt->portName);
    pgpibPvt->pollRequestIsQueued = 0;
    epicsMutexUnlock(pgpibPvt->lock);
    for(ntrys=0; ntrys<MAX_POLL; ntrys++) {
        status = pasynGpibPort->srqStatus(pgpibPvt->asynGpibPortPvt,&srqStatus);
        if(status!=asynSuccess) {
            asynPrint(pasynUser,ASYN_TRACE_ERROR,
                "%s asynGpib:srqPoll srqStatus error %s\n",
                pgpibPvt->portName,
                (status==asynTimeout ? "timeout" : "error"));
            break;
        }
        if(!srqStatus) break;
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s asynGpib:srqPoll serialPollBegin\n",pgpibPvt->portName);
        pasynGpibPort->serialPollBegin(pgpibPvt->asynGpibPortPvt);
        for(primary=0; primary<NUM_GPIB_ADDRESSES; primary++) {
            pollListPrimary *ppollListPrimary = &pgpibPvt->pollList[primary];
            pollNode *ppollNode = &ppollListPrimary->primary;
            if(ppollNode->pollIt) {
                pollOne(pasynUser,pgpibPvt,pasynGpibPort,ppollNode,primary);
            }
            if(ppollListPrimary->pollSecondary) {
                for(secondary=0; secondary<NUM_GPIB_ADDRESSES; secondary++) {
                    int addr = primary*100+secondary;
                    ppollNode = &ppollListPrimary->secondary[secondary];
                    if(ppollNode->pollIt) {
                        pollOne(pasynUser,pgpibPvt,pasynGpibPort,ppollNode,addr);
                    }
                }
            }
        }
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s asynGpib:srqPoll serialPollEnd\n",pgpibPvt->portName);
        pasynGpibPort->serialPollEnd(pgpibPvt->asynGpibPortPvt);
    }
    if(srqStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynGpib:srqPoll srqStatus is %x after ntrys %d Why?\n",
            pgpibPvt->portName,srqStatus,ntrys);
    }
}

/*asynCommon methods */
static void report(void *drvPvt,FILE *fd,int details)
{
    GETgpibPvtasynGpibPort
    pasynGpibPort->report(pgpibPvt->asynGpibPortPvt,fd,details);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    asynStatus status;
    GETgpibPvtasynGpibPort

    status = pasynGpibPort->connect(pgpibPvt->asynGpibPortPvt,pasynUser);
    if(status==asynSuccess) {
        if(pgpibPvt->eoslen==1) {
            char eos[2];

            eos[0] = pgpibPvt->eos; eos[1] = 0;
            status = pasynGpibPort->setEos(pgpibPvt->asynGpibPortPvt,
                pasynUser,eos,pgpibPvt->eoslen);
        }
        srqHappened(pgpibPvt);
    }
    return(status);
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->disconnect(pgpibPvt->asynGpibPortPvt,pasynUser));
}

/*asynOctet methods */
static asynStatus writeIt(void *drvPvt,asynUser *pasynUser,
    const char *data,size_t numchars,size_t *nbytesTransfered)
{
    int nt;
    asynStatus status;
    GETgpibPvtasynGpibPort

    status =  pasynGpibPort->write(pgpibPvt->asynGpibPortPvt,pasynUser,
              data,(int)numchars,&nt);
    *nbytesTransfered = (size_t)nt;
    return status;
}

static asynStatus readIt(void *drvPvt,asynUser *pasynUser,
    char *data,size_t maxchars,size_t *nbytesTransfered,int *eomReason)
{
    int nt;
    asynStatus status;
    GETgpibPvtasynGpibPort

    status = pasynGpibPort->read(pgpibPvt->asynGpibPortPvt,pasynUser,
               data,(int)maxchars,&nt,eomReason);
    *nbytesTransfered = (size_t)nt;
    if(status!=asynSuccess) return status;
    if(pgpibPvt->eoslen==1 && nt>0) {
        if(data[nt-1]==pgpibPvt->eos) {
            if (eomReason) *eomReason |= ASYN_EOM_EOS;
            nt--;
        }
    }
    if(nt<(int)maxchars) data[nt] = 0;
    if((nt==maxchars) && eomReason) *eomReason |= ASYN_EOM_CNT;
    *nbytesTransfered = (size_t)nt;
    pasynOctetBase->callInterruptUsers(pasynUser,pgpibPvt->pasynPvt,
        data,nbytesTransfered,eomReason);
    return status;
}

static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->flush(pgpibPvt->asynGpibPortPvt,pasynUser));
}

static asynStatus setInputEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    asynStatus status;
    GETgpibPvtasynGpibPort
    
    if(eoslen>1) {
        epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
            "%s asynGpib:setInputEos eoslen %d too long. only 1 is allowed",
             pgpibPvt->portName,eoslen);
        return asynError;
    }
    status = pasynGpibPort->setEos(pgpibPvt->asynGpibPortPvt,
        pasynUser,eos,eoslen);
    if(status!=asynSuccess) return status;
    pgpibPvt->eoslen = eoslen;
    if(eoslen==1) pgpibPvt->eos = eos[0];
    return status;
}

static asynStatus getInputEos(void *drvPvt,asynUser *pasynUser,
    char *eos, int eossize, int *eoslen)
{
    int        len;
    GETgpibPvtasynGpibPort

    len =  pgpibPvt->eoslen;
    *eoslen = len;
    if(len==1) {
        eos[0] = pgpibPvt->eos;
    }
    if(eossize>len) eos[len] = 0;
    return asynSuccess;
}


/*asynGpib methods */
static asynStatus addressedCmd(void *drvPvt,asynUser *pasynUser,
    const char *data, int length)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->addressedCmd(pgpibPvt->asynGpibPortPvt,pasynUser,
           data,length));
}

static asynStatus universalCmd(void *drvPvt, asynUser *pasynUser, int cmd)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->universalCmd(pgpibPvt->asynGpibPortPvt,pasynUser,cmd));
}

static asynStatus ifc (void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->ifc(pgpibPvt->asynGpibPortPvt,pasynUser));
}

static asynStatus ren (void *drvPvt,asynUser *pasynUser, int onOff)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->ren(pgpibPvt->asynGpibPortPvt,pasynUser,onOff));
}

static asynStatus pollAddr(void *drvPvt,asynUser *pasynUser, int onOff)
{
    int addr,primary,secondary;
    BOOL isPrimary;
    asynStatus status;
    pollNode *pnode;
    GETgpibPvtasynGpibPort

    status = getAddr(pgpibPvt,pasynUser,&addr,&primary,&secondary,&isPrimary);
    if(status!=asynSuccess) return status;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s asynGpib:pollAddr addr %d onOff %d\n",
        pgpibPvt->portName,addr,onOff);
    if(isPrimary) {
        pnode = &pgpibPvt->pollList[primary].primary;
    } else {
        pgpibPvt->pollList[primary].pollSecondary = TRUE;
        pnode = &pgpibPvt->pollList[primary].secondary[secondary];
    }
    if(pnode->pollIt==onOff) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s asynGpib:pollAddr addr %d poll state not changed\n",
             pgpibPvt->portName,addr);
        return asynError;
    }
    if(onOff) {
        asynInterface *pasynInterface;

        pnode->pollIt = 0; /*initialize to 0 in case of failure*/
        pnode->pasynUser = pasynManager->createAsynUser(0,0);
        pnode->pasynUser->userPvt = pgpibPvt;
        status = pasynManager->connectDevice(pnode->pasynUser,
            pgpibPvt->portName,addr);
        if(status!=asynSuccess) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s asynGpib:pollAddr connectDevice %s\n",
                pgpibPvt->portName,pasynUser->errorMessage);
                return asynError;
        }
        pasynInterface = pasynManager->findInterface(pnode->pasynUser,
                 asynCommonType,0);
        if(!pasynInterface) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s asynGpib:pollIt cant find interface asynCommon\n",
                pgpibPvt->portName);
            return asynError;
        }
        pnode->pasynCommon = (asynCommon *)pasynInterface->pinterface;
        pnode->drvPvt = pasynInterface->drvPvt;
        pnode->pollIt = 1;
    } else {
        pnode->pollIt = 0;
        status = pasynManager->freeAsynUser(pnode->pasynUser);
        if(status!=asynSuccess) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "%s asynGpib:pollAddr %s\n",
                pgpibPvt->portName,pasynUser->errorMessage);
        }
        pnode->pasynUser = 0;
    }
    return asynSuccess;
}

/* The following are called by low level gpib drivers */
static void *registerPort(
        const char *portName,
        int attributes,int autoConnect,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize)
{
    gpibPvt    *pgpibPvt;
    asynStatus status;
    asynUser   *pasynUser;

    if(!pgpibBase) gpibInit();
    pgpibPvt = locateGpibPvt(portName);
    if(pgpibPvt) {
        printf("asynGpib:registerDriver %s already registered\n",portName);
        return(0);
    }
    pgpibPvt = callocMustSucceed(1,sizeof(gpibPvt),
        "asynGpib:registerPort");
    pgpibPvt->lock = epicsMutexMustCreate();
    pgpibPvt->portName = portName;
    pgpibPvt->attributes = attributes;
    pgpibPvt->pasynGpibPort = pasynGpibPort;
    pgpibPvt->asynGpibPortPvt = asynGpibPortPvt;
    pgpibPvt->common.interfaceType = asynCommonType;
    pgpibPvt->common.pinterface = &common;
    pgpibPvt->common.drvPvt = pgpibPvt;
    pgpibPvt->octet.interfaceType = asynOctetType;
    pgpibPvt->octet.pinterface = &octet;
    pgpibPvt->octet.drvPvt = pgpibPvt;
    pgpibPvt->gpib.interfaceType = asynGpibType;
    pgpibPvt->gpib.pinterface = &gpib;
    pgpibPvt->gpib.drvPvt = pgpibPvt;
    pgpibPvt->int32.interfaceType = asynInt32Type;
    pgpibPvt->int32.pinterface = &int32;
    pgpibPvt->int32.drvPvt = pgpibPvt;
    ellAdd(&pgpibBase->gpibPvtList,&pgpibPvt->node);
    status = pasynManager->registerPort(portName,attributes,autoConnect,
         priority,stackSize);
    if(status==asynSuccess)
        status = pasynOctetBase->initialize(portName,&pgpibPvt->octet,0,0,0);
    if(status==asynSuccess)
        status = pasynManager->registerInterruptSource(
            portName,&pgpibPvt->octet,&pgpibPvt->pasynPvt);
    if(status==asynSuccess)
        status = pasynManager->registerInterface(portName,&pgpibPvt->gpib);
    if(status==asynSuccess)
        status = pasynInt32Base->initialize(portName,&pgpibPvt->int32);
    if(status!=asynSuccess) return 0;
    pasynUser = pasynManager->createAsynUser(srqPoll,0);
    pgpibPvt->pasynUser = pasynUser;
    pasynUser->userPvt = pgpibPvt;
    pasynUser->errorMessage[0] = 0;
    status = pasynManager->connectDevice(pasynUser,portName,-1);
    if(status==asynSuccess) {
        status = pasynManager->exceptionCallbackAdd(pasynUser,exceptionHandler);
    }
    if(status==asynSuccess) {
        status = pasynManager->registerInterruptSource(portName,
            &pgpibPvt->int32,&pgpibPvt->asynInt32Pvt);
    }
    /* Note: the asynCommon interface must be registered after all other initialization is complete,
     * because a connection request can occur immediately after registering this interface */
    if(status==asynSuccess)
        status = pasynManager->registerInterface(portName,&pgpibPvt->common);
    if(status!=asynSuccess) {
        printf("%s registerPort failed %s\n",portName,pasynUser->errorMessage);
        return 0;
    }
    return (void *)pgpibPvt;
}

static void srqHappened(void *drvPvt)
{
    asynStatus status;
    asynUser *pasynUser;
    GETgpibPvtasynGpibPort

    pasynUser = pgpibPvt->pasynUser;
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s asynGpib:srqHappened\n", pgpibPvt->portName);
    epicsMutexMustLock(pgpibPvt->lock);
    if(pgpibPvt->pollRequestIsQueued) {
        epicsMutexUnlock(pgpibPvt->lock);
        return;
    }
    pgpibPvt->pollRequestIsQueued = 1;
    epicsMutexUnlock(pgpibPvt->lock);
    status = pasynManager->queueRequest(pgpibPvt->pasynUser,
        asynQueuePriorityMedium,0.0);
    if(status!=asynSuccess) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynGpib:srqHappened queueRequest failed %s\n",
            pgpibPvt->portName,pasynUser->errorMessage);
    }
} 
