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

typedef struct gpibBase {
    ELLLIST gpibPvtList;
    epicsTimerQueueId timerQueue;
}gpibBase;
static gpibBase *pgpibBase = 0;

typedef struct gpibPvt {
    ELLNODE node;
    epicsMutexId lock;
    const char *deviceName;
    gpibDriver *pgpibDriver;
    void *pdrvPvt;
}gpibPvt;

/* forward reference to internal methods */
static void gpibInit(void);
static gpibPvt *locateGpibPvt(const char *deviceName);
/*asynDriver methods */
static void report(void *pdrvPvt,asynUser *pasynUser,int details);
static asynStatus connect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser);
/*octetDriver methods */
static int gpibRead(void *pdrvPvt,asynUser *pasynUser,
    int addr,char *data,int maxchars);
static int gpibWrite(void *pdrvPvt,asynUser *pasynUser,
    int addr,const char *data,int numchars);
static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser,int addr);
static asynStatus setTimeout(void *pdrvPvt,asynUser *pasynUser,
    asynTimeoutType type,double timeout);
static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen);
static asynStatus installPeekHandler(void *pdrvPvt,asynUser *pasynUser,
    peekHandler handler);
static asynStatus removePeekHandler(void *pdrvPvt,asynUser *pasynUser);
/*gpibDriver methods*/
static asynStatus registerSrqHandler(void *pdrvPvt,asynUser *pasynUser,
    srqHandler handler, void *userPrivate);
static asynStatus addressedCmd(void *pdrvPvt,asynUser *pasynUser,
    int addr, const char *data, int length);
static asynStatus universalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *pdrvPvt,asynUser *pasynUser);
static asynStatus ren (void *pdrvPvt,asynUser *pasynUser, int onOff);
static void pollAddr(void *pdrvPvt,asynUser *pasynUser,int addr, int onOff);
static void srqProcessing(void *pdrvPvt,asynUser *pasynUser, int onOff);
static void srqSet(void *pdrvPvt,asynUser *pasynUser,
    double srqTimeout,double pollTimeout,double pollRate,
    int srqMaxEvents);
static void srqGet(void *pdrvPvt,asynUser *pasynUser,
    double *srqTimeout,double *pollTimeout,double *pollRate,
    int *srqMaxEvents);
/* The following are called by low level gpib drivers */
static void *registerDevice(
        const char *deviceName,
        gpibDriver *pgpibDriver, void *pdrvPvt,
        unsigned int priority, unsigned int stackSize);
static void srqHappened(void *pgpibvt);

#define NUM_INTERFACES 3

static asynDriver asyn = {report,connect,disconnect};
static octetDriver octet = {
    gpibRead,gpibWrite,gpibFlush,
    setTimeout, setEos,
    installPeekHandler, removePeekHandler
};

static gpibDriverUser gpib = {
    registerSrqHandler, addressedCmd, universalCmd, ifc, ren,
    pollAddr, srqProcessing, srqSet, srqGet,
    registerDevice, srqHappened
};

static driverInterface mydriverInterface[NUM_INTERFACES] = {
    {asynDriverType,&asyn},
    {octetDriverType,&octet},
    {gpibDriverUserType,&gpib}
};

epicsShareDef gpibDriverUser *pgpibDriverUser = &gpib;

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

/*asynDriver methods */
static void report(void *pdrvPvt,asynUser *pasynUser,int details)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    pgpibDriver->report(pgpibPvt->pdrvPvt,pasynUser,details);
}

static asynStatus connect(void *pdrvPvt,asynUser *pasynUser)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->connect(pgpibPvt->pdrvPvt,pasynUser));
}

static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->disconnect(pgpibPvt->pdrvPvt,pasynUser));
}

/*octetDriver methods */
static int gpibRead(void *pdrvPvt,asynUser *pasynUser,
    int addr,char *data,int maxchars)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->read(pgpibPvt->pdrvPvt,pasynUser,
           addr,data,maxchars));
}

static int gpibWrite(void *pdrvPvt,asynUser *pasynUser,
                    int addr,const char *data,int numchars)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->write(pgpibPvt->pdrvPvt,pasynUser,
           addr,data,numchars));
}

static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser,int addr)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->flush(pgpibPvt->pdrvPvt,pasynUser,addr));
}

static asynStatus setTimeout(void *pdrvPvt,asynUser *pasynUser,
    asynTimeoutType type,double timeout)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->setTimeout(pgpibPvt->pdrvPvt,pasynUser,type,timeout));
}

static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,
    const char *eos,int eoslen)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->setEos(pgpibPvt->pdrvPvt,pasynUser,eos,eoslen));
}

static asynStatus installPeekHandler(void *pdrvPvt,asynUser *pasynUser,
    peekHandler handler)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->installPeekHandler(pgpibPvt->pdrvPvt,pasynUser,handler));
}

static asynStatus removePeekHandler(void *pdrvPvt,asynUser *pasynUser)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->removePeekHandler(pgpibPvt->pdrvPvt,pasynUser));
}

/*gpibDriverUser methods */
static asynStatus registerSrqHandler(void *pdrvPvt,asynUser *pasynUser,
     srqHandler handler, void *userPrivate)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->registerSrqHandler(pgpibPvt->pdrvPvt,pasynUser,
           handler,userPrivate));
}

static asynStatus addressedCmd(void *pdrvPvt,asynUser *pasynUser,
    int addr, const char *data, int length)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->addressedCmd(pgpibPvt->pdrvPvt,pasynUser,
           addr,data,length));
}

static asynStatus universalCmd(void *pdrvPvt, asynUser *pasynUser, int cmd)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->universalCmd(pgpibPvt->pdrvPvt,pasynUser,cmd));
}

static asynStatus ifc (void *pdrvPvt,asynUser *pasynUser)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->ifc(pgpibPvt->pdrvPvt,pasynUser));
}

static asynStatus ren (void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;
    gpibDriver *pgpibDriver = pgpibPvt->pgpibDriver;
    return(pgpibDriver->ren(pgpibPvt->pdrvPvt,pasynUser,onOff));
}

static void pollAddr(void *pdrvPvt,asynUser *pasynUser,int addr, int onOff)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;

    printf("gpibDriver:pollAddr not implemented %p\n",pgpibPvt);
}

static void srqProcessing(void *pdrvPvt,asynUser *pasynUser, int onOff)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;

    printf("gpibDriver:srqProcessing not implemented %p\n",pgpibPvt);
}

static void srqSet(void *pdrvPvt,asynUser *pasynUser,
    double srqTimeout,double pollTimeout,double pollRate,
    int srqMaxEvents)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;

    printf("gpibDriver:srqSet not implemented %p\n",pgpibPvt);
}

static void srqGet(void *pdrvPvt,asynUser *pasynUser,
    double *srqTimeout,double *pollTimeout,double *pollRate,
    int *srqMaxEvents)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pdrvPvt;

    printf("gpibDriver:srqGet not implemented %p\n",pgpibPvt);
}

/* The following are called by low level gpib drivers */
static void *registerDevice(
        const char *deviceName,
        gpibDriver *pgpibDriver, void *pdrvPvt,
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
    pgpibPvt->pgpibDriver = pgpibDriver;
    pgpibPvt->pdrvPvt = pdrvPvt;
    padeviceDriver = callocMustSucceed(NUM_INTERFACES,sizeof(deviceDriver),
        "echoDriverInit");
    for(i=0; i<NUM_INTERFACES; i++) {
        padeviceDriver[i].pdriverInterface = &mydriverInterface[i];
        padeviceDriver[i].pdrvPvt = pgpibPvt;
    }
    ellAdd(&pgpibBase->gpibPvtList,&pgpibPvt->node);
    status = pasynQueueManager->registerDevice(deviceName,
         padeviceDriver,NUM_INTERFACES,priority,stackSize);
    if(status!=asynSuccess) return(0);
    return((void *)pgpibPvt);
}

static void srqHappened(void *pvt)
{
    gpibPvt *pgpibPvt = (gpibPvt *)pvt;

    printf("gpibDriver:srqHappened not implemented %p\n",pgpibPvt);
} 
