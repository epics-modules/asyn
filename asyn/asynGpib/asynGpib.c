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
#include <epicsExport.h>
#include <iocsh.h>

#define epicsExportSharedSymbols
#include <asynGpibDriver.h>

#define BOOL int
#define TRUE 1
#define FALSE 0
#define SRQTIMEOUT 2.0
#define MAX_POLL 5

typedef struct gpibBase {
    ELLLIST gpibPvtList;
    epicsTimerQueueId timerQueue;
}gpibBase;
static gpibBase *pgpibBase = 0;

typedef struct pollListNode {
    int pollIt;
    int statusByte;
}pollListNode;

typedef struct pollListPrimary {
    pollListNode primary;
    pollListNode secondary[NUM_GPIB_ADDRESSES];
}pollListPrimary;

typedef struct gpibPvt {
    ELLNODE node;
    const char *portName;
    epicsMutexId lock;
    pollListPrimary pollList[NUM_GPIB_ADDRESSES];
    int pollRequestIsQueued;
    asynGpibPort *pasynGpibPort;
    void *asynGpibPortPvt;
    asynUser *pasynUser;
    srqHandler srq_handler;
    void *srqHandlerPvt;
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
static void srqPoll(asynUser *pasynUser);
/*asynCommon methods */
static void report(void *drvPvt,FILE *fd,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
static asynStatus setPortOption(void *drvPvt,asynUser *pasynUser,
    const char *key,const char *val);
static asynStatus getPortOption(void *drvPvt,asynUser *pasynUser,
    const char *key,char *val,int sizeval);
/*asynOctet methods */
static int gpibRead(void *drvPvt,asynUser *pasynUser,char *data,int maxchars);
static int gpibWrite(void *drvPvt,asynUser *pasynUser,
    const char *data,int numchars);
static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser);
static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
/*asynGpib methods*/
static asynStatus addressedCmd(void *drvPvt,asynUser *pasynUser,
    const char *data, int length);
static asynStatus universalCmd(void *drvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *drvPvt,asynUser *pasynUser);
static asynStatus ren (void *drvPvt,asynUser *pasynUser, int onOff);
static asynStatus registerSrqHandler(void *drvPvt,asynUser *pasynUser,
    srqHandler handler, void *srqHandlerPvt);
static void pollAddr(void *drvPvt,asynUser *pasynUser, int onOff);
/* The following are called by low level gpib drivers */
static void *registerPort(
        const char *portName,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize);
static void srqHappened(void *pgpibvt);

#define NUM_INTERFACES 3
static asynCommon asyn = {
   report,connect,disconnect,setPortOption,getPortOption
};
static asynOctet octet = {
    gpibRead,gpibWrite,gpibFlush, setEos
};
static asynGpib gpib = {
    addressedCmd, universalCmd, ifc, ren,
    registerSrqHandler, pollAddr,
    registerPort, srqHappened
};

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

static void srqPoll(asynUser *pasynUser)
{
    void *drvPvt = pasynUser->userPvt;
    int srqStatus,primary,secondary,ntrys;
    GETgpibPvtasynGpibPort

    epicsMutexMustLock(pgpibPvt->lock);
    if(!pgpibPvt->pollRequestIsQueued) 
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s asynGpib:srqPoll but !pollRequestIsQueued. Why?\n",
            pgpibPvt->portName);
    pgpibPvt->pollRequestIsQueued = 0;
    epicsMutexUnlock(pgpibPvt->lock);
    for(ntrys=0; ntrys<MAX_POLL; ntrys++) {
        srqStatus = pasynGpibPort->srqStatus(pgpibPvt->asynGpibPortPvt);
        if(!srqStatus) break;
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s asynGpib:srqPoll serialPollBegin\n",pgpibPvt->portName);
        pasynGpibPort->serialPollBegin(pgpibPvt->asynGpibPortPvt);
        for(primary=0; primary<NUM_GPIB_ADDRESSES; primary++) {
            pollListPrimary *ppollListPrimary = &pgpibPvt->pollList[primary];
            pollListNode *ppollListNode = &ppollListPrimary->primary;
            int statusByte;
    
            if(ppollListNode->pollIt) {
                asynPrint(pasynUser, ASYN_TRACE_FLOW,
                    "%s asynGpib:srqPoll serialPoll addr %d\n",
                    pgpibPvt->portName,primary);
                statusByte = pasynGpibPort->serialPoll(
                    pgpibPvt->asynGpibPortPvt,primary,SRQTIMEOUT);
                if(statusByte) {
                    pgpibPvt->srq_handler(pgpibPvt->srqHandlerPvt,
                        primary,statusByte);
                }
            }
            for(secondary=0; secondary<NUM_GPIB_ADDRESSES; secondary++) {
                ppollListNode = &ppollListPrimary->secondary[secondary];
                if(ppollListNode->pollIt) {
                    int addr = primary*100+secondary;
                    asynPrint(pasynUser, ASYN_TRACE_FLOW,
                        "%s asynGpib:srqPoll serialPoll addr %d\n",
                        pgpibPvt->portName,addr);
                    statusByte = pasynGpibPort->serialPoll(
                        pgpibPvt->asynGpibPortPvt,addr,SRQTIMEOUT);
                    if(statusByte) {
                        pgpibPvt->srq_handler(pgpibPvt->srqHandlerPvt,
                            addr,statusByte);
                    }
                }
            }
        }
        asynPrint(pasynUser, ASYN_TRACE_FLOW,
            "%s asynGpib:srqPoll serialPollEnd\n",pgpibPvt->portName);
        pasynGpibPort->serialPollEnd(pgpibPvt->asynGpibPortPvt);
        srqStatus = pasynGpibPort->srqStatus(pgpibPvt->asynGpibPortPvt);
        if(!srqStatus) break;
    }
    if(srqStatus) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "%s asynGpib:srqPoll srqStatus is %x after ntrys %dWhy?\n",
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
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->connect(pgpibPvt->asynGpibPortPvt,pasynUser));
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->disconnect(pgpibPvt->asynGpibPortPvt,pasynUser));
}

static asynStatus setPortOption(void *drvPvt, asynUser *pasynUser,
    const char *key, const char *val)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->setPortOption(drvPvt,pasynUser,key,val));
}

static asynStatus getPortOption(void *drvPvt, asynUser *pasynUser,
    const char *key, char *val, int sizeval)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->getPortOption(drvPvt,pasynUser,key,val,sizeval));
}

/*asynOctet methods */
static int gpibRead(void *drvPvt,asynUser *pasynUser,char *data,int maxchars)
{
    int nchars;
    GETgpibPvtasynGpibPort

    nchars = pasynGpibPort->read(pgpibPvt->asynGpibPortPvt,pasynUser,
           data,maxchars);
    return(nchars);
}

static int gpibWrite(void *drvPvt,asynUser *pasynUser,
                    const char *data,int numchars)
{
    int nchars;
    GETgpibPvtasynGpibPort

    nchars = pasynGpibPort->write(pgpibPvt->asynGpibPortPvt,pasynUser,
           data,numchars);
    return(nchars);
}

static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->flush(pgpibPvt->asynGpibPortPvt,pasynUser));
}

static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    GETgpibPvtasynGpibPort
    return(pasynGpibPort->setEos(pgpibPvt->asynGpibPortPvt,pasynUser,eos,eoslen));
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

static asynStatus registerSrqHandler(void *drvPvt,asynUser *pasynUser,
     srqHandler handler, void *srqHandlerPvt)
{
    asynStatus status;
    GETgpibPvtasynGpibPort

    if(pgpibPvt->srq_handler) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "%s asynGpib:registerSrqHandler. handler already registered\n",
            pgpibPvt->portName);
        return(asynError);
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s asynGpib:registerSrqHandler.\n",pgpibPvt->portName);
    pgpibPvt->srq_handler = handler;
    pgpibPvt->srqHandlerPvt = srqHandlerPvt;
    status = pasynGpibPort->srqEnable(pgpibPvt->asynGpibPortPvt,1);
    return(status);
}

static void pollAddr(void *drvPvt,asynUser *pasynUser, int onOff)
{
    int primary,secondary,addr;
    GETgpibPvtasynGpibPort

    addr = pasynManager->getAddr(pasynUser);
    if(addr<100) {
	assert(addr>=0 && addr<NUM_GPIB_ADDRESSES);
	pgpibPvt->pollList[addr].primary.pollIt = onOff;
	return;
    }
    asynPrint(pasynUser, ASYN_TRACE_FLOW,
        "%s asynGpib:pollAddr addr %d onOff %d\n",
        pgpibPvt->portName,addr,onOff);
    primary = addr/100; secondary = primary%100;
    assert(primary>=0 && primary<NUM_GPIB_ADDRESSES);
    assert(secondary>=0 && secondary<NUM_GPIB_ADDRESSES);
    pgpibPvt->pollList[addr].secondary[secondary].pollIt = onOff;
}

/* The following are called by low level gpib drivers */
static void *registerPort(
        const char *portName,
        asynGpibPort *pasynGpibPort, void *asynGpibPortPvt,
        unsigned int priority, unsigned int stackSize)
{
    gpibPvt *pgpibPvt;
    asynStatus status;
    asynInterface *paasynInterface;

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
    pgpibPvt->pasynGpibPort = pasynGpibPort;
    pgpibPvt->asynGpibPortPvt = asynGpibPortPvt;
    paasynInterface = callocMustSucceed(NUM_INTERFACES,sizeof(asynInterface),
        "echoDriverInit");
    paasynInterface[0].interfaceType = asynCommonType;
    paasynInterface[0].pinterface = &asyn;
    paasynInterface[0].drvPvt = pgpibPvt;
    paasynInterface[1].interfaceType = asynOctetType;
    paasynInterface[1].pinterface = &octet;
    paasynInterface[1].drvPvt = pgpibPvt;
    paasynInterface[2].interfaceType = asynGpibType;
    paasynInterface[2].pinterface = &gpib;
    paasynInterface[2].drvPvt = pgpibPvt;
    ellAdd(&pgpibBase->gpibPvtList,&pgpibPvt->node);
    status = pasynManager->registerPort(portName,
         paasynInterface,NUM_INTERFACES,priority,stackSize);
    if(status==asynSuccess) {
        pgpibPvt->pasynUser = pasynManager->createAsynUser(srqPoll,0);
        pgpibPvt->pasynUser->userPvt = pgpibPvt;
        status = pasynManager->connectDevice(pgpibPvt->pasynUser,portName,0);
    }
    if(status!=asynSuccess) return(0);
    return((void *)pgpibPvt);
}

static void srqHappened(void *drvPvt)
{
    asynStatus status;
    asynUser *pasynUser;
    GETgpibPvtasynGpibPort

    pasynUser = pgpibPvt->pasynUser;
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
