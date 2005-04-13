/*asynUInt32DigitalSyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynUInt32Digital
 * Author:  Marty Kraimer
 * Created: 12OCT2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>

#include <asynDriver.h>
#include <asynUInt32Digital.h>
#include <asynDrvUser.h>
#define epicsExportSharedSymbols
#include <asynUInt32DigitalSyncIO.h>

typedef struct ioPvt{
   asynCommon        *pasynCommon;
   void              *pcommonPvt;
   asynUInt32Digital *pasynUInt32Digital;
   void              *uint32DigitalPvt;
   asynDrvUser       *pasynDrvUser;
   void              *drvUserPvt;
}ioPvt;

/*asynUInt32DigitalSyncIO methods*/
static asynStatus connect(const char *port, int addr,asynUser **ppasynUser,
                      const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser,
                      epicsUInt32 value,epicsUInt32 mask, double timeout);
static asynStatus readOp(asynUser *pasynUser,
                      epicsUInt32 *pvalue,epicsUInt32 mask,double timeout);
static asynStatus setInterrupt(asynUser *pasynUser,
                      epicsUInt32 mask, interruptReason reason,double timeout);
static asynStatus clearInterrupt(asynUser *pasynUser,
                      epicsUInt32 mask,double timeout);
static asynStatus getInterrupt(asynUser *pasynUser,
                      epicsUInt32 *mask, interruptReason reason,double timeout);
static asynStatus writeOpOnce(const char *port, int addr,
                      epicsUInt32 value,epicsUInt32 mask, double timeout,
                      const char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr,
                      epicsUInt32 *pvalue,epicsUInt32 mask,double timeout,
                      const char *drvInfo);
static asynStatus setInterruptOnce(const char *port, int addr,
                      epicsUInt32 mask, interruptReason reason,double timeout,
                      const char *drvInfo);
static asynStatus clearInterruptOnce(const char *port, int addr,
                      epicsUInt32 mask,double timeout,const char *drvInfo);
static asynStatus getInterruptOnce(const char *port, int addr,
                      epicsUInt32 *mask, interruptReason reason,double timeout,
                      const char *drvInfo);
static asynUInt32DigitalSyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    setInterrupt,
    clearInterrupt,
    getInterrupt,
    writeOpOnce,
    readOpOnce,
    setInterruptOnce,
    clearInterruptOnce,
    getInterruptOnce
};
epicsShareDef asynUInt32DigitalSyncIO *pasynUInt32DigitalSyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    /* Create private structure */
    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynUInt32DigitalSyncIO");
    pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = pioPvt;
    *ppasynUser = pasynUser;
    status = pasynManager->connectDevice(pasynUser, port, addr);    
    if (status != asynSuccess) {
      printf("Can't connect to port %s address %d %s\n",
          port, addr,pasynUser->errorMessage);
      pasynManager->freeAsynUser(pasynUser);
      free(pioPvt);
      return status ;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       printf("%s interface not supported\n", asynCommonType);
       goto cleanup;
    }
    pioPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pioPvt->pcommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser,asynUInt32DigitalType,1);
    if (!pasynInterface) {
       printf("%s interface not supported\n", asynUInt32DigitalType);
       goto cleanup;
    }
    pioPvt->pasynUInt32Digital = (asynUInt32Digital *)pasynInterface->pinterface;
    pioPvt->uint32DigitalPvt = pasynInterface->drvPvt;
    if(drvInfo) {
        /* Check for asynDrvUser interface */
        pasynInterface = pasynManager->findInterface(pasynUser,asynDrvUserType,1);
        if(pasynInterface) {
            asynDrvUser *pasynDrvUser;
            void       *drvPvt;
            pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
            drvPvt = pasynInterface->drvPvt;
            status = pasynDrvUser->create(drvPvt,pasynUser,drvInfo,0,0);
            if(status==asynSuccess) {
                pioPvt->pasynDrvUser = pasynDrvUser;
                pioPvt->drvUserPvt = drvPvt;
            } else {
                printf("asynInt32SyncIO::connect drvUserCreate drvInfo=%s %s\n",
                         drvInfo, pasynUser->errorMessage);
            }
        }
    }
    return asynSuccess ;
cleanup:
    disconnect(pasynUser);
    return asynError;
}

static asynStatus disconnect(asynUser *pasynUser)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status;

    if(pioPvt->pasynDrvUser) {
        status = pioPvt->pasynDrvUser->destroy(pioPvt->drvUserPvt,pasynUser);
        if(status!=asynSuccess) {
            asynPrint(pasynUser, ASYN_TRACE_ERROR,
                "asynUInt32DigitalSyncIO pasynDrvUser->destroy failed %s\n",
                pasynUser->errorMessage);
            return status;
        }
    }
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO disconnect failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO freeAsynUser failed %s\n",
            pasynUser->errorMessage);
        return status;
    }
    free(pioPvt);
    return asynSuccess;
}
 

static asynStatus writeOp(asynUser *pasynUser,
       epicsUInt32 value,epicsUInt32 mask,double timeout)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->lockPort(pasynUser,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO lockPort failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pioPvt->pasynUInt32Digital->write(
       pioPvt->uint32DigitalPvt, pasynUser, value,mask);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR, 
            "asynUInt32DigitalSyncIO write failed %s\n",pasynUser->errorMessage);
    } else {
         asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
             "asynUInt32DigitalSyncIO wrote: %e\n",value);
    }
    if((pasynManager->unlockPort(pasynUser)) ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "unlockPort error %s\n", pasynUser->errorMessage);
    }
    return status;
}

static asynStatus readOp(asynUser *pasynUser,
        epicsUInt32 *pvalue, epicsUInt32 mask,double timeout)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->lockPort(pasynUser,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO lockPort failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pioPvt->pasynUInt32Digital->read(
        pioPvt->uint32DigitalPvt, pasynUser,pvalue,mask);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynUInt32DigitalSyncIO read failed %s\n",
                 pasynUser->errorMessage);
    } else {
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                   "asynUInt32DigitalSyncIO read: %e\n",*pvalue);
    }
    if((pasynManager->unlockPort(pasynUser)) ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "unlockPort error %s\n", pasynUser->errorMessage);
    }
    return status;
}

static asynStatus setInterrupt(asynUser *pasynUser,
                   epicsUInt32 mask, interruptReason reason,double timeout)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->lockPort(pasynUser,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO lockPort failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pioPvt->pasynUInt32Digital->setInterrupt(
        pioPvt->uint32DigitalPvt, pasynUser,mask,reason);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynUInt32DigitalSyncIO setInterrupt failed %s\n",
                 pasynUser->errorMessage);
    } else {
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                   "asynUInt32DigitalSyncIO setInterrupt: 0x%x\n",mask);
    }
    if((pasynManager->unlockPort(pasynUser)) ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "unlockPort error %s\n", pasynUser->errorMessage);
    }
    return status;
}

static asynStatus clearInterrupt(asynUser *pasynUser,
                        epicsUInt32 mask,double timeout)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->lockPort(pasynUser,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO lockPort failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pioPvt->pasynUInt32Digital->clearInterrupt(
       pioPvt->uint32DigitalPvt, pasynUser,mask);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynUInt32DigitalSyncIO clearInterrupt failed %s\n",
                 pasynUser->errorMessage);
    } else {
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                   "asynUInt32DigitalSyncIO clearInterrupt: 0x%x\n",mask);
    }
    if((pasynManager->unlockPort(pasynUser)) ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "unlockPort error %s\n", pasynUser->errorMessage);
    }
    return status;
}

static asynStatus getInterrupt(asynUser *pasynUser,
                    epicsUInt32 *mask, interruptReason reason,double timeout)
{
    asynStatus status;
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->lockPort(pasynUser,1);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO lockPort failed %s\n",pasynUser->errorMessage);
        return status;
    }
    status = pioPvt->pasynUInt32Digital->getInterrupt(
        pioPvt->uint32DigitalPvt, pasynUser,mask,reason);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynUInt32DigitalSyncIO getInterrupt failed %s\n",
                 pasynUser->errorMessage);
    } else {
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                   "asynUInt32DigitalSyncIO getInterrupt: 0x%x\n",mask);
    }
    if((pasynManager->unlockPort(pasynUser)) ) {
        asynPrint(pasynUser,ASYN_TRACE_ERROR,
            "unlockPort error %s\n", pasynUser->errorMessage);
    }
    return status;
}

static asynStatus writeOpOnce(const char *port, int addr,
    epicsUInt32 value,epicsUInt32 mask,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       printf("asynUInt32DigitalSyncIO connect failed %s\n",
            pasynUser->errorMessage);
       return status;
    }
    status = writeOp(pasynUser,value,mask,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO writeOp failed %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOpOnce(const char *port, int addr,
                   epicsUInt32 *pvalue,epicsUInt32 mask,double timeout,
                   const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       printf("asynUInt32DigitalSyncIO connect failed %s\n",
            pasynUser->errorMessage);
       return status;
    }
    status = readOp(pasynUser,pvalue,mask,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO readOp failed %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus setInterruptOnce(const char *port, int addr,
                    epicsUInt32 mask, interruptReason reason,double timeout,
                    const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       printf("asynUInt32DigitalSyncIO connect failed %s\n",
            pasynUser->errorMessage);
       return status;
    }
    status = setInterrupt(pasynUser,mask,reason,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO setInterrupt failed %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus clearInterruptOnce(const char *port, int addr,
                        epicsUInt32 mask,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       printf("asynUInt32DigitalSyncIO connect failed %s\n",
            pasynUser->errorMessage);
       return status;
    }
    status = clearInterrupt(pasynUser,mask,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO clearInterrupt failed %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus getInterruptOnce(const char *port, int addr,
                  epicsUInt32 *mask, interruptReason reason,double timeout,
                  const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       printf("asynUInt32DigitalSyncIO connect failed %s\n",
            pasynUser->errorMessage);
       return status;
    }
    status = getInterrupt(pasynUser,mask,reason,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynUInt32DigitalSyncIO getInterrupt failed %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}
