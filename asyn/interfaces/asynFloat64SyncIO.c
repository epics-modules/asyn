/*asynFloat64SyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to asynFloat64
 * Author:  Marty Kraimer
 * Created: 12OCT2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <epicsEvent.h>
#include <asynDriver.h>
#include <asynFloat64.h>
#include <asynDrvUser.h>
#include <drvAsynIPPort.h>
#include <asynFloat64SyncIO.h>

/* Timeout for queue callback.  0. means wait forever, don't remove entry
   from queue */
#define QUEUE_TIMEOUT 0.
/* Timeout for event waiting for queue. This time lets other threads talk 
 * on port  */
#define EVENT_TIMEOUT 1.

typedef enum {
   opConnect,
   opRead,
   opWrite
} opType;
typedef struct ioPvt{
   epicsEventId event;
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynFloat64    *pasynFloat64;
   void         *pdrvPvt;
   asynStatus   status;
   epicsFloat64  value;
   double       timeout;
   opType op;
}ioPvt;

/*local functions*/
static asynStatus queueAndWait(asynUser *pasynUser, double timeout, opType op);
static void processCallback(asynUser *pasynUser);

/*asynFloat64SyncIO methods*/
static asynStatus connect(const char *port, int addr,
                          asynUser **ppasynUser, char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus writeOp(asynUser *pasynUser,epicsFloat64 value,double timeout);
static asynStatus readOp(asynUser *pasynUser,epicsFloat64 *pvalue,double timeout);
static asynStatus writeOpOnce(const char *port, int addr,
                          epicsFloat64 value, double timeout, char *drvInfo);
static asynStatus readOpOnce(const char *port, int addr,
                        epicsFloat64 *pvalue,double timeout, char *drvInfo);
static asynFloat64SyncIO interface = {
    connect,
    disconnect,
    writeOp,
    readOp,
    writeOpOnce,
    readOpOnce
};
epicsShareDef asynFloat64SyncIO *pasynFloat64SyncIO = &interface;

static asynStatus queueAndWait(asynUser *pasynUser, double timeout, opType op)
{
    ioPvt *pPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus status;
    epicsEventWaitStatus waitStatus;
    int wasQueued;

    /* Copy parameters to private structure for use in callback */
    pPvt->timeout = timeout;
    pPvt->op = op;
    /* Queue request */
    status = pasynManager->queueRequest(pasynUser, 
                ((op == opConnect) ?
                asynQueuePriorityConnect : asynQueuePriorityLow), 
                QUEUE_TIMEOUT);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "queueAndWait queue request failed %s\n",
                 pasynUser->errorMessage);
       return(status);
    }

    /* Wait for event, signals I/O is complete
     * The user specified timeout is for reading the device.  We want
     * to also allow some time for other threads to talk to the device,
     * which means there could be some time before our message gets to the
     * head of the queue.  So add EVENT_TIMEOUT to timeout. 
     * If timeout is -1 this means wait forever, so don't do timeout at all*/
    if(pPvt->event) {
        if (timeout == -1.0)
           waitStatus = epicsEventWait(pPvt->event);
        else
           waitStatus = epicsEventWaitWithTimeout(pPvt->event, 
                                                  timeout+EVENT_TIMEOUT);
        if (waitStatus!=epicsEventWaitOK) {
           asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                     "queueAndWait event timeout\n");
           /* We need to delete the entry from the queue or it will block this
            * port forever */
           status = pasynManager->cancelRequest(pasynUser,&wasQueued);
           if(status!=asynSuccess || !wasQueued) {
              asynPrint(pasynUser, ASYN_TRACE_ERROR,
                        "queueAndWait Cancel request failed: %s\n",
                        pasynUser->errorMessage);
           }
           return(asynTimeout);
        }
    }
    /* Return that status that the callback put in the private structure */
    return(pPvt->status);
}

static void processCallback(asynUser *pasynUser)
{
    ioPvt *pPvt = (ioPvt *)pasynUser->userPvt;
    asynFloat64 *pasynFloat64 = pPvt->pasynFloat64;
    void *pdrvPvt = pPvt->pdrvPvt;
    asynStatus status = asynSuccess;

    pasynUser->timeout = pPvt->timeout;
    switch(pPvt->op) {
    case opConnect:
       status = pPvt->pasynCommon->connect(pPvt->pcommonPvt,pasynUser);
       if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynFloat64SyncIO connect failed %s\n",
                    pasynUser->errorMessage);
       } else {
          asynPrint(pasynUser, ASYN_TRACE_FLOW, "asynFloat64SyncIO connect\n");
       }
       break;
    case opWrite:
       status = pasynFloat64->write(pdrvPvt, pasynUser, pPvt->value);
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                 "asynFloat64SyncIO status=%d, wrote: %e",
                 status,pPvt->value);
       break;
    case opRead:
       status = pasynFloat64->read(pdrvPvt, pasynUser, &pPvt->value);
       asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                 "asynFloat64SyncIO status=%d read: %e",status,pPvt->value);
       break;
    }
    pPvt->status = status;
    /* Signal the epicsEvent to let the waiting thread know we're done */
    if(pPvt->event) epicsEventSignal(pPvt->event);
}

static asynStatus connect(const char *port, int addr,
   asynUser **ppasynUser, char *drvInfo)
{
    ioPvt *pioPvt;
    asynUser *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;
    int isConnected;
    int canBlock;

    /* Create private structure */
    pioPvt = (ioPvt *)calloc(1, sizeof(ioPvt));
    /* Create asynUser, copy address to caller */
    pasynUser = pasynManager->createAsynUser(processCallback,0);
    pasynUser->userPvt = pioPvt;
    *ppasynUser = pasynUser;
    /* Look up port, addr */
    status = pasynManager->connectDevice(pasynUser, port, addr);    
    if (status != asynSuccess) {
      printf("Can't connect to port %s address %d %s\n",
          port, addr,pasynUser->errorMessage);
      return(status);
    }
    status = pasynManager->canBlock(pasynUser,&canBlock);
    if (status != asynSuccess) {
      printf("port %s address %d canBlock failed %s\n",
         port,addr,pasynUser->errorMessage);
      return(status);
    }
    if(canBlock) {
        /* Create epicsEvent */
        pioPvt->event = epicsEventCreate(epicsEventEmpty);
    }
    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynCommonType);
       return(asynError);
    }
    pioPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pioPvt->pcommonPvt = pasynInterface->drvPvt;

    /* Get asynFloat64 interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynFloat64Type, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynFloat64Type);
       return(asynError);
    }
    pioPvt->pasynFloat64 = (asynFloat64 *)pasynInterface->pinterface;
    pioPvt->pdrvPvt = pasynInterface->drvPvt;

    /* Get asynDrvUser interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynDrvUserType, 1);
    if(pasynInterface && drvInfo) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;
        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,drvInfo,0,0);
        if(status!=asynSuccess) {
            printf("asynIntFloat64SyncIO::connect drvUserCreate drvInfo=%s %s\n",
                     drvInfo, pasynUser->errorMessage);
        }
    }

    /* Connect to device if not already connected.  
     * For TCP/IP sockets this ensures that the port is connected */
    status = pasynManager->isConnected(pasynUser, &isConnected);
    if (status != asynSuccess) {
       printf("Error getting isConnected status %s\n", pasynUser->errorMessage);
       return(status);
    }
    if (!isConnected) {
       status = queueAndWait(pasynUser,
                          0.0,opConnect);
       if (status != asynSuccess) {
          printf("Error connecting to device %s\n", pasynUser->errorMessage);
          return(status);
       }
    }
    return(asynSuccess);
}

static asynStatus disconnect(asynUser *pasynUser)
{
    ioPvt *pPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus    status;
    int           canBlock = 0;

    status = pasynManager->canBlock(pasynUser,&canBlock);
    if(status!=asynSuccess) return status;
    status = pasynManager->disconnect(pasynUser);
    if(status!=asynSuccess) return status;
    if(canBlock) epicsEventDestroy(pPvt->event);
    status = pasynManager->freeAsynUser(pasynUser);
    if(status!=asynSuccess) return status;
    free(pPvt);
    return asynSuccess;
}

static asynStatus writeOp(asynUser *pasynUser,epicsFloat64 value,double timeout)
{
    asynStatus status;
    ioPvt *pPvt = (ioPvt *)pasynUser->userPvt;

    pPvt->value = value;
    status = queueAndWait(pasynUser,timeout,opWrite);
    return(status);
}

static asynStatus readOp(asynUser *pasynUser,epicsFloat64 *pvalue,double timeout)
{
    ioPvt *pPvt = (ioPvt *)pasynUser->userPvt;
    asynStatus         status;

    status = queueAndWait(pasynUser,
                                  timeout,opRead);
    if(status==asynSuccess) *pvalue = pPvt->value;
    return(status);
}

static asynStatus writeOpOnce(const char *port, int addr,
    epicsFloat64 value,double timeout,char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;
    int        nbytes;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return -1;
    nbytes = writeOp(pasynUser,value,timeout);
    disconnect(pasynUser);
    return nbytes;
}

static asynStatus readOpOnce(const char *port, int addr,
                   epicsFloat64 *pvalue,double timeout,char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return -1;
    status = readOp(pasynUser,pvalue,timeout);
    disconnect(pasynUser);
    return status;
}
