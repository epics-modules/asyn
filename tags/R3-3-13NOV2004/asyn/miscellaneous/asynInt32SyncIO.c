/*asynInt32SyncIO.c*/
/***********************************************************************
* Copyright (c) 2002 The University of Chicago, as Operator of Argonne
* National Laboratory, and the Regents of the University of
* California, as Operator of Los Alamos National Laboratory, and
* Berliner Elektronenspeicherring-Gesellschaft m.b.H. (BESSY).
* asynDriver is distributed subject to a Software License Agreement
* found in file LICENSE that is included with this distribution.
***********************************************************************/
/*
 * This package provide a simple, synchronous interface to the "asyn" package. 
 * It will work with any device that asyn supports, e.g. serial, GPIB, and 
 * TCP/IP sockets.  It hides the details of asyn.  It is intended for use 
 * by applications which do simple synchronous reads, writes and write/reads.  
 * It does not support port-specific control, e.g. baud rates, 
 * GPIB Universal Commands, etc.
 *
 * Author:  Mark Rivers
 * Created: March 20, 2004
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <epicsEvent.h>
#include <asynDriver.h>
#include <asynInt32.h>
#include <drvAsynIPPort.h>
#include <asynInt32SyncIO.h>

/* Timeout for queue callback.  0. means wait forever, don't remove entry
   from queue */
#define QUEUE_TIMEOUT 0.
/* Timeout for event waiting for queue. This time lets other threads talk 
 * on port  */
#define EVENT_TIMEOUT 1.

typedef enum {
   asynInt32SyncIO_DRIVER_CONNECT,
   asynInt32SyncIO_READ,
   asynInt32SyncIO_WRITE,
   asynInt32SyncIO_GET_BOUNDS
} asynInt32SyncIOOp;

typedef struct asynInt32SyncIOPvt {
   epicsEventId      event;
   asynCommon        *pasynCommon;
   void              *pcommonPvt;
   asynInt32         *pasynInt32;
   void              *pdrvPvt;
   asynStatus        status;
   epicsInt32        value;
   epicsInt32        low;
   epicsInt32        high;
   double            timeout;
   asynInt32SyncIOOp op;
} asynInt32SyncIOPvt;

/*local functions*/
static asynStatus asynInt32SyncIOQueueAndWait(asynUser *pasynUser, 
                          double timeout, asynInt32SyncIOOp op);
static void asynInt32SyncIOCallback(asynUser *pasynUser);

/*asynInt32SyncIO methods*/
static asynStatus asynInt32SyncIOConnect(const char *port, int addr,
                                         asynUser **ppasynUser);
static asynStatus asynInt32SyncIODisconnect(asynUser *pasynUser);
static asynStatus asynInt32SyncIOWrite(asynUser *pasynUser,
                                       epicsInt32 value,double timeout);
static asynStatus asynInt32SyncIORead(asynUser *pasynUser,
                                       epicsInt32 *pvalue,double timeout);
static asynStatus asynInt32SyncIOGetBounds(asynUser *pasynUser, 
                                       epicsInt32 *plow, epicsInt32 *phigh);
static asynStatus asynInt32SyncIOWriteOnce(const char *port, int addr,
                                       epicsInt32 value,double timeout);
static asynStatus asynInt32SyncIOReadOnce(const char *port, int addr,
                                       epicsInt32 *pvalue,double timeout);
static asynStatus asynInt32SyncIOGetBoundsOnce(const char *port, int addr,
                                       epicsInt32 *plow, epicsInt32 *phigh);
static asynInt32SyncIO asynInt32SyncIOManager = {
    asynInt32SyncIOConnect,
    asynInt32SyncIODisconnect,
    asynInt32SyncIOWrite,
    asynInt32SyncIORead,
    asynInt32SyncIOGetBounds,
    asynInt32SyncIOWriteOnce,
    asynInt32SyncIOReadOnce,
    asynInt32SyncIOGetBoundsOnce
};
epicsShareDef asynInt32SyncIO *pasynInt32SyncIO = &asynInt32SyncIOManager;

static asynStatus asynInt32SyncIOQueueAndWait(asynUser *pasynUser, 
                          double timeout, asynInt32SyncIOOp op)
{
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;
    asynStatus status;
    epicsEventWaitStatus waitStatus;
    int wasQueued;

    /* Copy parameters to private structure for use in callback */
    pPvt->timeout = timeout;
    pPvt->op = op;
    /* Queue request */
    status = pasynManager->queueRequest(pasynUser, 
                ((op == asynInt32SyncIO_DRIVER_CONNECT) ?
                asynQueuePriorityConnect : asynQueuePriorityLow), 
                QUEUE_TIMEOUT);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynInt32SyncIOQueueAndWait queue request failed %s\n",
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
                     "asynInt32SyncIOQueueAndWait event timeout\n");
           /* We need to delete the entry from the queue or it will block this
            * port forever */
           status = pasynManager->cancelRequest(pasynUser,&wasQueued);
           if(status!=asynSuccess || !wasQueued) {
              asynPrint(pasynUser, ASYN_TRACE_ERROR,
                        "asynInt32SyncIOQueueAndWait Cancel request failed: %s\n",
                        pasynUser->errorMessage);
           }
           return(asynTimeout);
        }
    }
    /* Return that status that the callback put in the private structure */
    return(pPvt->status);
}

static void asynInt32SyncIOCallback(asynUser *pasynUser)
{
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;
    asynInt32 *pasynInt32 = pPvt->pasynInt32;
    void *pdrvPvt = pPvt->pdrvPvt;
    asynStatus status = asynSuccess;

    pasynUser->timeout = pPvt->timeout;
    switch(pPvt->op) {
    case asynInt32SyncIO_DRIVER_CONNECT:
       status = pPvt->pasynCommon->connect(pPvt->pcommonPvt,pasynUser);
       if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynInt32SyncIO connect failed %s\n",
                    pasynUser->errorMessage);
       } else {
          asynPrint(pasynUser, ASYN_TRACE_FLOW, "asynInt32SyncIO connect\n");
       }
       break;
    case asynInt32SyncIO_WRITE:
       status = pasynInt32->write(pdrvPvt, pasynUser, pPvt->value);
       if(status==asynError) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynInt32SyncIO write failed %s\n",
                    pasynUser->errorMessage);
       } else {
           asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                      "asynInt32SyncIO wrote: %e",pPvt->value);
       }
       break;
    case asynInt32SyncIO_READ:
       status = pasynInt32->read(pdrvPvt, pasynUser, &pPvt->value);
       if(status==asynError) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynInt32SyncIO read failed %s\n",
                    pasynUser->errorMessage);
       } else {
          asynPrint(pasynUser, ASYN_TRACEIO_DEVICE, 
                      "asynInt32SyncIO read: %e",pPvt->value);
       }
       break;
    case asynInt32SyncIO_GET_BOUNDS:
        status = pasynInt32->getBounds(pdrvPvt,pasynUser,&pPvt->low,&pPvt->high);
       if(status==asynError) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynInt32SyncIO getBounds failed %s\n",
                    pasynUser->errorMessage);
       } else {
          asynPrint(pasynUser, ASYN_TRACE_FLOW, 
             "asynInt32SyncIO getBounds: low %d high %d\n",pPvt->low,pPvt->high);
       }
       break;
    }
    pPvt->status = status;
    /* Signal the epicsEvent to let the waiting thread know we're done */
    if(pPvt->event) epicsEventSignal(pPvt->event);
}

static asynStatus asynInt32SyncIOConnect(const char *port, int addr,
   asynUser **ppasynUser)
{
    asynInt32SyncIOPvt *pasynInt32SyncIOPvt;
    asynUser *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;
    int isConnected;
    int canBlock;

    /* Create private structure */
    pasynInt32SyncIOPvt = (asynInt32SyncIOPvt *)calloc(1, sizeof(asynInt32SyncIOPvt));
    /* Create asynUser, copy address to caller */
    pasynUser = pasynManager->createAsynUser(asynInt32SyncIOCallback,0);
    pasynUser->userPvt = pasynInt32SyncIOPvt;
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
        pasynInt32SyncIOPvt->event = epicsEventCreate(epicsEventEmpty);
    }
    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynCommonType);
       return(asynError);
    }
    pasynInt32SyncIOPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pasynInt32SyncIOPvt->pcommonPvt = pasynInterface->drvPvt;

    /* Get asynInt32 interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynInt32Type, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynInt32Type);
       return(asynError);
    }
    pasynInt32SyncIOPvt->pasynInt32 = (asynInt32 *)pasynInterface->pinterface;
    pasynInt32SyncIOPvt->pdrvPvt = pasynInterface->drvPvt;

    /* Connect to device if not already connected.  
     * For TCP/IP sockets this ensures that the port is connected */
    status = pasynManager->isConnected(pasynUser, &isConnected);
    if (status != asynSuccess) {
       printf("Error getting isConnected status %s\n", pasynUser->errorMessage);
       return(status);
    }
    if (!isConnected) {
       status = asynInt32SyncIOQueueAndWait(pasynUser,
                          0.0,asynInt32SyncIO_DRIVER_CONNECT);
       if (status != asynSuccess) {
          printf("Error connecting to device %s\n", pasynUser->errorMessage);
          return(status);
       }
    }
    return(asynSuccess);
}

static asynStatus asynInt32SyncIODisconnect(asynUser *pasynUser)
{
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;
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
 

static asynStatus asynInt32SyncIOWrite(asynUser *pasynUser,
                         epicsInt32 value,double timeout)
{
    asynStatus status;
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;

    pPvt->value = value;
    status = asynInt32SyncIOQueueAndWait(pasynUser,timeout,asynInt32SyncIO_WRITE);
    return(status);
}

static asynStatus asynInt32SyncIORead(asynUser *pasynUser,
                        epicsInt32 *pvalue, double timeout)
{
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;
    asynStatus         status;

    status = asynInt32SyncIOQueueAndWait(pasynUser,
                                  timeout,asynInt32SyncIO_READ);
    if(status==asynSuccess) *pvalue = pPvt->value;
    return(status);
}

static asynStatus asynInt32SyncIOGetBounds(asynUser *pasynUser, 
                                       epicsInt32 *plow, epicsInt32 *phigh)
{
    asynInt32SyncIOPvt *pPvt = (asynInt32SyncIOPvt *)pasynUser->userPvt;
    asynStatus         status;

    status = asynInt32SyncIOQueueAndWait(pasynUser,
                                  0.0,asynInt32SyncIO_GET_BOUNDS);
    if(status==asynSuccess) {
        *plow = pPvt->low;
        *phigh = pPvt->high;
    }
    return(status);
}

static asynStatus asynInt32SyncIOWriteOnce(const char *port, int addr,
    epicsInt32 value,double timeout)
{
    asynStatus status;
    asynUser   *pasynUser;
    int        nbytes;

    status = asynInt32SyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return -1;
    nbytes = asynInt32SyncIOWrite(pasynUser,value,timeout);
    asynInt32SyncIODisconnect(pasynUser);
    return nbytes;
}

static asynStatus asynInt32SyncIOReadOnce(const char *port, int addr,
                   epicsInt32 *pvalue,double timeout)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = asynInt32SyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return -1;
    status = asynInt32SyncIORead(pasynUser,pvalue,timeout);
    asynInt32SyncIODisconnect(pasynUser);
    return status;
}

static asynStatus 
    asynInt32SyncIOGetBoundsOnce(const char *port, int addr,
                        epicsInt32 *plow, epicsInt32 *phigh)
{
    asynStatus status;
    asynUser   *pasynUser;
    int        nbytes;

    status = asynInt32SyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return -1;
    nbytes = asynInt32SyncIOGetBounds(pasynUser,plow,phigh);
    asynInt32SyncIODisconnect(pasynUser);
    return nbytes;
}
