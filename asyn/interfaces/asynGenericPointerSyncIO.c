/*asynGenericPointerSyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynGenericPointer
 * Author:  Marty Kraimer
 * Created: 12OCT2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <cantProceed.h>

#define epicsExportSharedSymbols
#include <shareLib.h>
#include "asynDriver.h"
#include "asynGenericPointer.h"
#include "asynDrvUser.h"
#include "asynGenericPointerSyncIO.h"

typedef struct ioPvt{
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynGenericPointer  *pasynGenericPointer;
   void         *pointerPvt;
   asynDrvUser  *pasynDrvUser;
   void         *drvUserPvt;
}ioPvt;

/*asynGenericPointerSyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser,void *pvalue,double timeout);
static asynStatus readOp(asynUser *pasynUser,void *pvalue,double timeout);
static asynStatus writeReadOp( asynUser *pasynUser, void *pwrite_buffer,
                               void *pread_buffer, double timeout );
static asynStatus writeOpOnce(const char *port, int addr,
                     void *pvalue, double timeout, const char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr,
                     void *pvalue,double timeout, const char *drvInfo);
static asynStatus writeReadOpOnce( const char *port, int addr,
                     void *pwrite_buffer, void *pread_buffer, double timeout, const char *drvInfo );
static asynGenericPointerSyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    writeReadOp,
    writeOpOnce,
    readOpOnce,
    writeReadOpOnce
};
epicsShareDef asynGenericPointerSyncIO *pasynGenericPointerSyncIO = &interface;

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, const char *drvInfo)
{
    ioPvt         *pioPvt;
    asynUser      *pasynUser;
    asynStatus    status;
    asynInterface *pasynInterface;

    pioPvt = (ioPvt *)callocMustSucceed(1, sizeof(ioPvt),"asynGenericPointerSyncIO");
    pasynUser = pasynManager->createAsynUser(0,0);
    pasynUser->userPvt = pioPvt;
    *ppasynUser = pasynUser;
    status = pasynManager->connectDevice(pasynUser, port, addr);    
    if (status != asynSuccess) {
        return status;
    }
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "interface %s is not supported by port",asynCommonType);
       return asynError;
    }
    pioPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pioPvt->pcommonPvt = pasynInterface->drvPvt;
    pasynInterface = pasynManager->findInterface(pasynUser, asynGenericPointerType, 1);
    if (!pasynInterface) {
       epicsSnprintf(pasynUser->errorMessage,pasynUser->errorMessageSize,
           "interface %s is not supported by port",asynGenericPointerType);
       return asynError;
    }
    pioPvt->pasynGenericPointer = (asynGenericPointer *)pasynInterface->pinterface;
    pioPvt->pointerPvt = pasynInterface->drvPvt;
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
                return status;
            }
        }
    }
    return asynSuccess ;
}

static asynStatus disconnect(asynUser *pasynUser)
{
    ioPvt      *pioPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status;

    if(pioPvt->pasynDrvUser) {
        status = pioPvt->pasynDrvUser->destroy(pioPvt->drvUserPvt,pasynUser);
        if(status!=asynSuccess) {
            return status;
        }
    }
    status = pasynManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    free(pioPvt);
    return asynSuccess;
}

static asynStatus writeOp(asynUser *pasynUser,void *pvalue,double timeout)
{
    asynStatus status, unlockStatus;
    ioPvt      *pPvt = (ioPvt *)pasynUser->userPvt;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pPvt->pasynGenericPointer->write(pPvt->pointerPvt, pasynUser,pvalue);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynGenericPointerSyncIO wrote: %p\n",
                  pvalue);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return status;
}

static asynStatus readOp(asynUser *pasynUser,void *pvalue,double timeout)
{
    ioPvt      *pPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status, unlockStatus;

    pasynUser->timeout = timeout;
    status = pasynManager->queueLockPort(pasynUser);
    if(status!=asynSuccess) {
        return status;
    }
    status = pPvt->pasynGenericPointer->read(pPvt->pointerPvt, pasynUser, pvalue);
    if (status==asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                  "asynGenericPointerSyncIO read: %p\n", pvalue);
    }
    unlockStatus = pasynManager->queueUnlockPort(pasynUser);
    if (unlockStatus != asynSuccess) {
        return unlockStatus;
    }
    return status;
}

static asynStatus writeReadOp(asynUser *pasynUser,
                              void *pwrite_buffer,
                              void *pread_buffer,
                              double timeout )
{
  asynStatus status, unlockStatus;
  ioPvt      *pPvt = (ioPvt *)pasynUser->userPvt;

  pasynUser->timeout = timeout;
  status = pasynManager->queueLockPort( pasynUser );
  if( status != asynSuccess ) {
    return status;
  }
  status = pPvt->pasynGenericPointer->write( pPvt->pointerPvt, pasynUser, pwrite_buffer );
  if( status != asynSuccess ) {
    goto bad;
  } else {
    asynPrint( pasynUser, ASYN_TRACEIO_DEVICE,
               "asynGenericPointerSyncIO wrote: %p\n",
               pwrite_buffer );
  }
  status = pPvt->pasynGenericPointer->read( pPvt->pointerPvt, pasynUser, pread_buffer );
  if ( status != asynSuccess ) {
    goto bad;
  } else {
    asynPrint( pasynUser, ASYN_TRACEIO_DEVICE,
               "asynGenericPointerSyncIO read: %p\n",
               pread_buffer );
  }

 bad:
  unlockStatus = pasynManager->queueUnlockPort(pasynUser);
  if (unlockStatus != asynSuccess) {
    return unlockStatus;
  }
  return status;
}

static asynStatus writeOpOnce(const char *port, int addr,
    void *pvalue,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynGenericPointerSyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = writeOp(pasynUser,pvalue,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynGenericPointerSyncIO writeOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOpOnce(const char *port, int addr,
                   void *pvalue,double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynGenericPointerSyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = readOp(pasynUser,pvalue,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynGenericPointerSyncIO readOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus writeReadOpOnce(const char *port, int addr,
                   void *pwrite_buffer, void *pread_buffer, double timeout,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) {
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
           "asynGenericPointerSyncIO connect failed %s\n",
           pasynUser->errorMessage);
        disconnect(pasynUser);
        return status;
    }
    status = writeReadOp(pasynUser,pwrite_buffer,pread_buffer,timeout);
    if(status!=asynSuccess) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR,
            "asynGenericPointerSyncIO writeReadOp failed %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

