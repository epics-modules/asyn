/* gpibDriver.c */
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* gpibCore is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
#include <stddef.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
/* epics includes */
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
#include <gpibDriver.h>

#define BOOL int
#define TRUE 1
#define FALSE 0
#define SRQTIMEOUT 2.0

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
    pollListPrimary pollList[NUM_GPIB_ADDRESSES];
    epicsMutexId lock;
    const char *deviceName;
    gpibDevice *pgpibDevice;
    void *gpibDevicePvt;
    asynUser *pasynUser;
    peekHandler peek_handler;
    void * peekHandlerPvt;
    srqHandler srq_handler;
    void *srqHandlerPvt;
}gpibPvt;

#define GETgpibPvtgpibDevice \
    gpibPvt *pgpibPvt = (gpibPvt *)drvPvt; \
    gpibDevice *pgpibDevice; \
    assert(pgpibPvt); \
    pgpibDevice = pgpibPvt->pgpibDevice; \
    assert(pgpibDevice);

/* forward reference to internal methods */
static void gpibInit(void);
static gpibPvt *locateGpibPvt(const char *deviceName);
static void srqPoll(void *drvPvt);
/*asynDriver methods */
static void report(void *drvPvt,int details);
static asynStatus connect(void *drvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *drvPvt,asynUser *pasynUser);
/*octetDriver methods */
static int gpibRead(void *drvPvt,asynUser *pasynUser,
    int addr,char *data,int maxchars);
static int gpibWrite(void *drvPvt,asynUser *pasynUser,
    int addr,const char *data,int numchars);
static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser,int addr);
static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
    int addr, const char *eos,int eoslen);
static asynStatus installPeekHandler(void *drvPvt,asynUser *pasynUser,
    peekHandler handler, void *peekHandlerPvt);
static asynStatus removePeekHandler(void *drvPvt,asynUser *pasynUser);
/*gpibDriver methods*/
static asynStatus addressedCmd(void *drvPvt,asynUser *pasynUser,
    int addr, const char *data, int length);
static asynStatus universalCmd(void *drvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *drvPvt,asynUser *pasynUser);
static asynStatus ren (void *drvPvt,asynUser *pasynUser, int onOff);
static asynStatus registerSrqHandler(void *drvPvt,asynUser *pasynUser,
    srqHandler handler, void *srqHandlerPvt);
static void pollAddr(void *drvPvt,asynUser *pasynUser,int addr, int onOff);
/* The following are called by low level gpib drivers */
static void *registerDevice(
        const char *deviceName,
        gpibDevice *pgpibDevice, void *gpibDevicePvt,
        unsigned int priority, unsigned int stackSize);
static void srqHappened(void *pgpibvt);

#define NUM_INTERFACES 3
static asynDriver asyn = {
   report,connect,disconnect
};
static octetDriver octet = {
    gpibRead,gpibWrite,gpibFlush, setEos,
    installPeekHandler, removePeekHandler
};
static gpibDriver gpib = {
    addressedCmd, universalCmd, ifc, ren,
    registerSrqHandler, pollAddr,
    registerDevice, srqHappened
};
static driverInterface gpibDriverInterface[NUM_INTERFACES] = {
    {asynDriverType,&asyn},
    {octetDriverType,&octet},
    {gpibDriverType,&gpib}
};

epicsShareDef gpibDriver *pgpibDriver = &gpib;

/*internal methods */
static void gpibInit(void)
{
    if(pgpibBase) return;
    pgpibBase = callocMustSucceed(1,sizeof(gpibPvt),"gpibInit");
    ellInit(&pgpibBase->gpibPvtList);
    pgpibBase->timerQueue = epicsTimerQueueAllocate(
        1,epicsThreadPriorityScanLow);
}

static gpibPvt *locateGpibPvt(const char *deviceName)
{
    gpibPvt *pgpibPvt = (gpibPvt *)ellFirst(&pgpibBase->gpibPvtList);
    while(pgpibPvt) {
        if(strcmp(deviceName,pgpibPvt->deviceName)==0) return(pgpibPvt);
        pgpibPvt = (gpibPvt *)ellNext(&pgpibPvt->node);
    }
    return(0);
}
static void srqPoll(void *drvPvt)
{
    GETgpibPvtgpibDevice
    int srqStatus,primary,secondary;

    srqStatus = pgpibDevice->srqStatus(pgpibPvt->gpibDevicePvt);
    while(srqStatus) {
        pgpibDevice->serialPollBegin(pgpibPvt->gpibDevicePvt);
        for(primary=0; primary<NUM_GPIB_ADDRESSES; primary++) {
            pollListPrimary *ppollListPrimary = &pgpibPvt->pollList[primary];
            pollListNode *ppollListNode = &ppollListPrimary->primary;
            int statusByte;
    
            if(ppollListNode->pollIt) {
                statusByte = pgpibDevice->serialPoll(
                    pgpibPvt->gpibDevicePvt,primary,SRQTIMEOUT);
                if(statusByte) {
                    pgpibPvt->srq_handler(pgpibPvt->srqHandlerPvt,
                        primary,statusByte);
                }
            }
            for(secondary=0; secondary<NUM_GPIB_ADDRESSES; secondary++) {
                ppollListNode = &ppollListPrimary->secondary[secondary];
                if(ppollListNode->pollIt) {
                    int addr = primary*100+secondary;
                    statusByte = pgpibDevice->serialPoll(
                        pgpibPvt->gpibDevicePvt,addr,SRQTIMEOUT);
                    if(statusByte) {
                        pgpibPvt->srq_handler(pgpibPvt->peekHandlerPvt,
                            addr,statusByte);
                    }
                }
            }
        }
        pgpibDevice->serialPollBegin(pgpibPvt->gpibDevicePvt);
        srqStatus = pgpibDevice->srqStatus(pgpibPvt->gpibDevicePvt);
        if(!srqStatus) break;
        printf("%s after srqPoll srqStatus is %x Why?\n",
            pgpibPvt->deviceName,srqStatus);
    }
}

/*asynDriver methods */
static void report(void *drvPvt,int details)
{
    GETgpibPvtgpibDevice
    pgpibDevice->report(pgpibPvt->gpibDevicePvt,details);
}

static asynStatus connect(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->connect(pgpibPvt->gpibDevicePvt,pasynUser));
}

static asynStatus disconnect(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->disconnect(pgpibPvt->gpibDevicePvt,pasynUser));
}

/*octetDriver methods */
static int gpibRead(void *drvPvt,asynUser *pasynUser,
    int addr,char *data,int maxchars)
{
    GETgpibPvtgpibDevice
    int nchars;
    nchars = pgpibDevice->read(pgpibPvt->gpibDevicePvt,pasynUser,
           addr,data,maxchars);
    if(nchars>0 && pgpibPvt->peek_handler) {
        int i;
        for(i=0; i<nchars; i++) {
            pgpibPvt->peek_handler(pgpibPvt->peekHandlerPvt,
                data[i],1,(i==(nchars-1) ? 1 : 0));
        }
    }
    return(nchars);
}

static int gpibWrite(void *drvPvt,asynUser *pasynUser,
                    int addr,const char *data,int numchars)
{
    GETgpibPvtgpibDevice
    int nchars;
    nchars = pgpibDevice->write(pgpibPvt->gpibDevicePvt,pasynUser,
           addr,data,numchars);
    if(nchars>0 && pgpibPvt->peek_handler) {
        int i;
        for(i=0; i<nchars; i++) {
            pgpibPvt->peek_handler(pgpibPvt->peekHandlerPvt,
                data[i],0,(i==(nchars-1) ? 1 : 0));
        }
    }
    return(nchars);
}

static asynStatus gpibFlush(void *drvPvt,asynUser *pasynUser,int addr)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->flush(pgpibPvt->gpibDevicePvt,pasynUser,addr));
}

static asynStatus setEos(void *drvPvt,asynUser *pasynUser,
    int addr, const char *eos,int eoslen)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->setEos(pgpibPvt->gpibDevicePvt,pasynUser,addr,eos,eoslen));
}

static asynStatus installPeekHandler(void *drvPvt,asynUser *pasynUser,
    peekHandler handler, void *peekHandlerPvt)
{
    GETgpibPvtgpibDevice
    pgpibPvt->peekHandlerPvt = peekHandlerPvt;
    pgpibPvt->peek_handler = handler;
    return(asynSuccess);
}

static asynStatus removePeekHandler(void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtgpibDevice
    pgpibPvt->peek_handler = 0;
    pgpibPvt->peekHandlerPvt = 0;
    return(asynSuccess);
}

/*gpibDriver methods */
static asynStatus addressedCmd(void *drvPvt,asynUser *pasynUser,
    int addr, const char *data, int length)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->addressedCmd(pgpibPvt->gpibDevicePvt,pasynUser,
           addr,data,length));
}

static asynStatus universalCmd(void *drvPvt, asynUser *pasynUser, int cmd)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->universalCmd(pgpibPvt->gpibDevicePvt,pasynUser,cmd));
}

static asynStatus ifc (void *drvPvt,asynUser *pasynUser)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->ifc(pgpibPvt->gpibDevicePvt,pasynUser));
}

static asynStatus ren (void *drvPvt,asynUser *pasynUser, int onOff)
{
    GETgpibPvtgpibDevice
    return(pgpibDevice->ren(pgpibPvt->gpibDevicePvt,pasynUser,onOff));
}

static asynStatus registerSrqHandler(void *drvPvt,asynUser *pasynUser,
     srqHandler handler, void *srqHandlerPvt)
{
    GETgpibPvtgpibDevice
    asynStatus status;

    if(pgpibPvt->srq_handler) {
        printf("%s gpibDriver:registerSrqHandler. handler already registered\n",
            pgpibPvt->deviceName);
        return(asynError);
    }
    pgpibPvt->srq_handler = handler;
    pgpibPvt->srqHandlerPvt = srqHandlerPvt;
    status = pgpibDevice->srqEnable(pgpibPvt->gpibDevicePvt,1);
    return(status);
}

static void pollAddr(void *drvPvt,asynUser *pasynUser,int addr, int onOff)
{
    GETgpibPvtgpibDevice

    int primary,secondary;

    if(addr<100) {
	assert(addr>=0 && addr<NUM_GPIB_ADDRESSES);
	pgpibPvt->pollList[addr].primary.pollIt = onOff;
	return;
    }
    primary = addr/100; secondary = primary%100;
    assert(primary>=0 && primary<NUM_GPIB_ADDRESSES);
    assert(secondary>=0 && secondary<NUM_GPIB_ADDRESSES);
    pgpibPvt->pollList[addr].secondary[secondary].pollIt = onOff;
}

/* The following are called by low level gpib drivers */
static void *registerDevice(
        const char *deviceName,
        gpibDevice *pgpibDevice, void *gpibDevicePvt,
        unsigned int priority, unsigned int stackSize)
{
    gpibPvt *pgpibPvt;
    asynStatus status;
    deviceDriver *padeviceDriver;
    int i;

    if(!pgpibBase) gpibInit();
    pgpibPvt = locateGpibPvt(deviceName);
    if(pgpibPvt) {
        printf("gpibDriver:registerDriver %s already registered\n",deviceName);
        return(0);
    }
    pgpibPvt = callocMustSucceed(1,sizeof(gpibPvt),
        "gpibDriver:registerDevice");
    pgpibPvt->lock = epicsMutexMustCreate();
    pgpibPvt->deviceName = deviceName;
    pgpibPvt->pgpibDevice = pgpibDevice;
    pgpibPvt->gpibDevicePvt = gpibDevicePvt;
    padeviceDriver = callocMustSucceed(NUM_INTERFACES,sizeof(deviceDriver),
        "echoDriverInit");
    for(i=0; i<NUM_INTERFACES; i++) {
        padeviceDriver[i].pdriverInterface = &gpibDriverInterface[i];
        padeviceDriver[i].drvPvt = pgpibPvt;
    }
    ellAdd(&pgpibBase->gpibPvtList,&pgpibPvt->node);
    status = pasynQueueManager->registerDevice(deviceName,
         padeviceDriver,NUM_INTERFACES,priority,stackSize);
    if(status==asynSuccess) {
        pgpibPvt->pasynUser = pasynQueueManager->createAsynUser(
            srqPoll,0,pgpibPvt);
        status = pasynQueueManager->connectDevice(pgpibPvt->pasynUser,deviceName);
    }
    if(status!=asynSuccess) return(0);
    return((void *)pgpibPvt);
}

static void srqHappened(void *drvPvt)
{
    GETgpibPvtgpibDevice
    asynStatus status;

    status = pasynQueueManager->queueRequest(pgpibPvt->pasynUser,
        asynQueuePriorityLow,0.0);
    if(status!=asynSuccess) {
        printf("%s gpibDriver:srqHappened queueRequest failed %s\n",
            pgpibPvt->deviceName,pgpibPvt->pasynUser->errorMessage);
    }
} 
