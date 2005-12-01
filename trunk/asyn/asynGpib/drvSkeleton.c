/* drvSkeleton.c */
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
#include <epicsMutex.h>
#include <epicsEvent.h>
#include <epicsThread.h>
#include <epicsTime.h>
#include <cantProceed.h>
#include <iocsh.h>

#include <epicsExport.h>
#include <asynGpibDriver.h>
#include <epicsExport.h>

typedef struct skeletonPvt {
    char *portName;
    void *pgpibPvt;
}skeletonPvt;

static void report(void *pdrvPvt,FILE *fd,int details);
static asynStatus connect(void *pdrvPvt,asynUser *pasynUser);
static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser);
/*asynOctet methods */
static int gpibRead(void *pdrvPvt,asynUser *pasynUser,int addr,char *data,int maxchars);
static int gpibWrite(void *pdrvPvt,asynUser *pasynUser,
                    int addr,const char *data,int numchars;
static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser,int addr);
static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen);
/*asynGpib methods*/
static asynStatus addressedCmd (void *pdrvPvt,asynUser *pasynUser,
    int addr, char *data, int length;
static asynStatus universalCmd (void *pdrvPvt, asynUser *pasynUser, int cmd);
static asynStatus ifc (void *pdrvPvt,asynUser *pasynUser);
static asynStatus ren (void *pdrvPvt,asynUser *pasynUser, int onOff);
static int srqStatus (void *pdrvPvt);
static asynStatus srqEnable (void *pdrvPvt, int onOff);
static asynStatus serialPollBegin (void *pdrvPvt);
static int serialPoll (void *pdrvPvt, int addr, double timeout);
static asynStatus serialPollEnd (void *pdrvPvt);

static void report(void *pdrvPvt,FILE *fd,int details)
{
}

static asynStatus connect(void *pdrvPvt,asynUser *pasynUser)
{
}

static asynStatus disconnect(void *pdrvPvt,asynUser *pasynUser)
{
}

/*asynOctet methods */
static int gpibRead(void *pdrvPvt,asynUser *pasynUser,int addr,char *data,int maxchars)
{
}

static int gpibWrite(void *pdrvPvt,asynUser *pasynUser,
                    int addr,const char *data,int numchars
{
}

static asynStatus gpibFlush(void *pdrvPvt,asynUser *pasynUser,int addr)
{
}

static asynStatus setEos(void *pdrvPvt,asynUser *pasynUser,const char *eos,int eoslen)
{
}

static asynStatus addressedCmd (void *pdrvPvt,asynUser *pasynUser,
    int addr, char *data, int length
{
}

static asynStatus universalCmd (void *pdrvPvt, asynUser *pasynUser, int cmd)
{
}

static asynStatus ifc (void *pdrvPvt,asynUser *pasynUser)
{
}

static asynStatus ren (void *pdrvPvt,asynUser *pasynUser, int onOff)
{
}

static int srqStatus (void *pdrvPvt)
{
}

static asynStatus srqEnable (void *pdrvPvt, int onOff)
{
}

static asynStatus serialPollBegin (void *pdrvPvt)
{
}

static int serialPoll (void *pdrvPvt, int addr, double timeout)
{
}

static asynStatus serialPollEnd (void *pdrvPvt)
{
}

static asynGpib skeletonDriver = {
    report,
    connect,
    disconnect,
    gpibRead,
    gpibWrite,
    gpibFlush,
    setEos,
    registerSrqHandler,
    addressedCmd,
    universalCmd,
    ifc,
    ren,
    srqStatus,
    srqEnable,
    serialPollBegin,
    serialPoll,
    serialPollEnd
};

int skeletonDriverConfig(const char *name, /*Start of device dependent args*/)
{
    skeletonPvt *pskeletonPvt;
    char *portName;

    portName = callocMustSucceed(strlen(name)+1,sizeof(char),
        "skeletonDriverConfig");
    pskeletonPvt = callocMustSucceed(1,sizeof(skeletonPvt),
        "skeletonDriverConfig");
    pskeletonPvt->portName = portName;
    pskeletonPvt->pgpibPvt = pasynGpibUser->registerPort(
        portName,
        ASYN_MULTIDEVICE|ASYN_CANBLOCK, autoConnect,
        &skeletonDriver,pskeletonPvt,
        epicsThreadPriorityLow,epicsThreadGetStackSize(epicsThreadStackSmall));
}

/*register skeletonDriverConfig*/
static const iocshArg skeletonDriverConfigArg0 =
    { "portName", iocshArgString };
static const iocshArg *skeletonDriverConfigArgs[] = {
    &skeletonDriverConfigArg0};
static const iocshFuncDef skeletonDriverConfigFuncDef = {
    "skeletonDriverConfig", 1, skeletonDriverConfigArgs};
static void skeletonDriverConfigCallFunc(const iocshArgBuf *args)
{
    skeletonDriverConfig(args[0].sval);
}

static void skeletonDriverRegister(void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&skeletonDriverConfigFuncDef, skeletonDriverConfigCallFunc);
    }
}
epicsExportRegistrar(skeletonDriverRegister);
