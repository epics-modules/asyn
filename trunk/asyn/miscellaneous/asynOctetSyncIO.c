/*asynOctetSyncIO.c*/
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
#include <asynOctet.h>
#include <drvAsynIPPort.h>
#include <asynOctetSyncIO.h>

/* Timeout for queue callback.  0. means wait forever, don't remove entry
   from queue */
#define QUEUE_TIMEOUT 0.
/* Timeout for event waiting for queue. This time lets other threads talk 
 * on port  */
#define EVENT_TIMEOUT 1.

typedef enum {
   asynOctetSyncIO_DRIVER_CONNECT,
   asynOctetSyncIO_FLUSH,
   asynOctetSyncIO_READ,
   asynOctetSyncIO_WRITE,
   asynOctetSyncIO_WRITE_READ
} asynOctetSyncIOOp;

typedef struct asynOctetSyncIOPvt {
   epicsEventId event;
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynOctet    *pasynOctet;
   void         *pdrvPvt;
   char         *input_buff;
   int          input_len;
   const char   *output_buff;
   int          output_len;
   char         *ieos;
   int          ieos_len;
   char         *oeos;
   int          oeos_len;
   double       timeout;
   int          flush;
   asynStatus   status;
   int          nbytesOut;
   int          nbytesIn;
   int          eomReason;
   asynOctetSyncIOOp op;
} asynOctetSyncIOPvt;

/*local routines */
static asynStatus asynOctetSyncIOQueueAndWait(asynUser *pasynUser, 
                          const char *output_buff, int output_len,
                          char *input_buff, int input_len,
                          const char *ieos, int ieos_len, 
                          int flush, double timeout,
                          asynOctetSyncIOOp op);
static void asynOctetSyncIOCallback(asynUser *pasynUser);

/*asynOctetSyncIO methods*/
static asynStatus asynOctetSyncIOConnect(const char *port, int addr,
                                         asynUser **ppasynUser);
static asynStatus asynOctetSyncIODisconnect(asynUser *pasynUser);
static asynStatus asynOctetSyncIOOpenSocket(const char *server, int port,
                                         char **portName);
static asynStatus asynOctetSyncIOWrite(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered);
static asynStatus asynOctetSyncIORead(asynUser *pasynUser,
                   char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout,
                   int *nbytesTransfered,int *eomReason);
static asynStatus asynOctetSyncIOWriteRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason);
static asynStatus asynOctetSyncIOFlush(asynUser *pasynUser);
static asynStatus asynOctetSyncIOWriteOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered);
static asynStatus asynOctetSyncIOReadOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout,
                   int *nbytesTransfered,int *eomReason);
static asynStatus asynOctetSyncIOWriteReadOnce(const char *port, int addr,
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason);

static asynOctetSyncIO asynOctetSyncIOManager = {
    asynOctetSyncIOConnect,
    asynOctetSyncIODisconnect,
    asynOctetSyncIOOpenSocket,
    asynOctetSyncIOWrite,
    asynOctetSyncIORead,
    asynOctetSyncIOWriteRead,
    asynOctetSyncIOFlush,
    asynOctetSyncIOWriteOnce,
    asynOctetSyncIOReadOnce,
    asynOctetSyncIOWriteReadOnce
};
epicsShareDef asynOctetSyncIO *pasynOctetSyncIO = &asynOctetSyncIOManager;

static asynStatus asynOctetSyncIOQueueAndWait(asynUser *pasynUser, 
                          const char *output_buff, int output_len,
                          char *input_buff, int input_len,
                          const char *ieos, int ieos_len, 
                          int flush, double timeout,
                          asynOctetSyncIOOp op)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
    asynStatus status;
    epicsEventWaitStatus waitStatus;
    int wasQueued;

    /* Copy parameters to private structure for use in callback */
    pPvt->output_buff = output_buff;
    pPvt->output_len = output_len;
    pPvt->input_buff = input_buff;
    pPvt->input_len = input_len;
    pPvt->ieos = (char *)ieos;
    pPvt->ieos_len = ieos_len;
    pPvt->flush = flush;
    pPvt->timeout = timeout;
    pPvt->op = op;

    /* Queue request */
    status = pasynManager->queueRequest(pasynUser, 
                ((op == asynOctetSyncIO_DRIVER_CONNECT) ?
                asynQueuePriorityConnect : asynQueuePriorityLow), 
                QUEUE_TIMEOUT);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynOctetSyncIOQueueAndWait queue request failed %s\n",
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
                     "asynOctetSyncIOQueueAndWait event timeout\n");
           /* We need to delete the entry from the queue or it will block this
            * port forever */
           status = pasynManager->cancelRequest(pasynUser,&wasQueued);
           if(status!=asynSuccess || !wasQueued) {
              asynPrint(pasynUser, ASYN_TRACE_ERROR,
                        "asynOctetSyncIOQueueAndWait Cancel request failed: %s",
                        pasynUser->errorMessage);
           }
           return(asynTimeout);
        }
    }
    if(status==asynSuccess) status = pPvt->status;
    /* Return that status that the callback put in the private structure */
    return(status);
}

static void asynOctetSyncIOCallback(asynUser *pasynUser)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
    asynOctet          *pasynOctet = pPvt->pasynOctet;
    void               *pdrvPvt = pPvt->pdrvPvt;
    asynStatus         status = asynSuccess;

    pPvt->nbytesOut = pPvt->nbytesIn = 0;
    pasynUser->timeout = pPvt->timeout;
    if (pPvt->flush) {
       status = pasynOctet->flush(pdrvPvt, pasynUser);
       if (status) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO flush failed %s\n",
                    pasynUser->errorMessage);
       }
    }
    if (pPvt->op == asynOctetSyncIO_DRIVER_CONNECT) {
       status = pPvt->pasynCommon->connect(pPvt->pcommonPvt,pasynUser);
    }
    if ((pPvt->op == asynOctetSyncIO_WRITE) || (pPvt->op ==asynOctetSyncIO_WRITE_READ)) {
       status = pasynOctet->write(pdrvPvt, pasynUser, 
                   pPvt->output_buff, pPvt->output_len,&pPvt->nbytesOut);
       if(status==asynError) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO write failed %s\n",
                    pasynUser->errorMessage);
       }
       if (pPvt->nbytesOut != pPvt->output_len) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO write output_len %d but nbytesTransfered %d\n",
                    pPvt->output_len,pPvt->nbytesOut);
       } else {
          asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, 
                      pPvt->output_buff, pPvt->nbytesOut,
                      "asynOctetSyncIO wrote: ");
       }
    }
    if ((pPvt->op == asynOctetSyncIO_READ) || (pPvt->op == asynOctetSyncIO_WRITE_READ)) {
       pasynOctet->setEos(pdrvPvt, pasynUser, pPvt->ieos, pPvt->ieos_len);
       status = pasynOctet->read(pdrvPvt, pasynUser, 
          pPvt->input_buff, pPvt->input_len,&pPvt->nbytesIn,&pPvt->eomReason);
       if(status==asynError) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO read failed %s\n",
                    pasynUser->errorMessage);
       } else {
          /* Append a NULL byte to the response if there is room */
          if(pPvt->nbytesIn < pPvt->input_len)
              pPvt->input_buff[pPvt->nbytesIn] = '\0';
          asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, 
                      pPvt->input_buff, pPvt->nbytesIn,
                      "asynOctetSyncIO read: ");
       }
    }
    pPvt->status = status;
    /* Signal the epicsEvent to let the waiting thread know we're done */
    if(pPvt->event) epicsEventSignal(pPvt->event);
}

static asynStatus 
   asynOctetSyncIOConnect(const char *port, int addr, asynUser **ppasynUser)
{
    asynOctetSyncIOPvt *pasynOctetSyncIOPvt;
    asynUser *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;
    int isConnected;
    int canBlock;

    /* Create private structure */
    pasynOctetSyncIOPvt = (asynOctetSyncIOPvt *)calloc(1, sizeof(asynOctetSyncIOPvt));
    /* Create asynUser, copy address to caller */
    pasynUser = pasynManager->createAsynUser(asynOctetSyncIOCallback,0);
    pasynUser->userPvt = pasynOctetSyncIOPvt;
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
        pasynOctetSyncIOPvt->event = epicsEventCreate(epicsEventEmpty);
    }
    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynCommonType);
       return(asynError);
    }
    pasynOctetSyncIOPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    pasynOctetSyncIOPvt->pcommonPvt = pasynInterface->drvPvt;
    /* Get asynOctet interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynOctetType);
       return(asynError);
    }
    pasynOctetSyncIOPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pasynOctetSyncIOPvt->pdrvPvt = pasynInterface->drvPvt;

    /* Connect to device if not already connected.  
     * For TCP/IP sockets this ensures that the port is connected */
    status = pasynManager->isConnected(pasynUser, &isConnected);
    if (status != asynSuccess) {
       printf("Error getting isConnected status %s\n", pasynUser->errorMessage);
       return(status);
    }
    if (!isConnected) {
       status = asynOctetSyncIOQueueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       NULL, 0, 0, 0., asynOctetSyncIO_DRIVER_CONNECT);
       if (status != asynSuccess) {
          printf("Error connecting to device %s\n", pasynUser->errorMessage);
          return(status);
       }
    }
    return(asynSuccess);
}

static asynStatus asynOctetSyncIODisconnect(asynUser *pasynUser)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
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

static asynStatus asynOctetSyncIOOpenSocket(const char *server, int port,
       char **portName)
{
    char portString[20];
    asynStatus status;

    sprintf(portString, "%d", port);
    *portName = calloc(1, strlen(server)+strlen(portString)+3);
    strcpy(*portName, server);
    strcat(*portName, ":");
    strcat(*portName, portString);
    status = drvAsynIPPortConfigure(*portName, *portName, 0, 0, 0);
    return(status);
}

static asynStatus asynOctetSyncIOWrite(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
    asynStatus status;

    status = asynOctetSyncIOQueueAndWait(pasynUser, buffer, buffer_len, NULL, 0, 
                                  NULL, 0, 0, timeout, asynOctetSyncIO_WRITE);
    *nbytesTransfered = pPvt->nbytesOut;
    return(status);
}

static asynStatus asynOctetSyncIORead(asynUser *pasynUser,
                   char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout,
                   int *nbytesTransfered,int *eomReason)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
    asynStatus status;
    int        ninp = 0;

    status = asynOctetSyncIOQueueAndWait(pasynUser,
                                  NULL, 0, buffer, buffer_len, 
                                  ieos, ieos_len, flush, timeout, 
                                  asynOctetSyncIO_READ);
    *nbytesTransfered = pPvt->nbytesIn;;
    if(eomReason) *eomReason = pPvt->eomReason;
    return(status);
}

static asynStatus asynOctetSyncIOWriteRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason)
{
    asynOctetSyncIOPvt *pPvt = (asynOctetSyncIOPvt *)pasynUser->userPvt;
    asynStatus status;

    status = asynOctetSyncIOQueueAndWait(pasynUser,
                                  write_buffer, write_buffer_len, 
                                  read_buffer, read_buffer_len, 
                                  ieos, ieos_len, 1, timeout, 
                                  asynOctetSyncIO_WRITE_READ);
    *nbytesOut = pPvt->nbytesOut;
    *nbytesIn = pPvt->nbytesIn;
    if(eomReason) *eomReason = pPvt->eomReason;
    return status;
}

static asynStatus
    asynOctetSyncIOFlush(asynUser *pasynUser)
{
    asynStatus status;

    status = asynOctetSyncIOQueueAndWait(pasynUser, NULL, 0, NULL, 0, 
                                    NULL, 0, 1, 0., asynOctetSyncIO_FLUSH);
    return(status);
}

static asynStatus asynOctetSyncIOWriteOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = asynOctetSyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return status;
    status = asynOctetSyncIOWrite(pasynUser,buffer,buffer_len,timeout,nbytesTransfered);
    if(status!=asynSuccess) {
        printf("asynOctetSyncIOWriteOnce error %s\n",pasynUser->errorMessage);
    }
    asynOctetSyncIODisconnect(pasynUser);
    return status;
}

static asynStatus asynOctetSyncIOReadOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout,
                   int *nbytesTransfered,int *eomReason)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = asynOctetSyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return status;
    status = asynOctetSyncIORead(pasynUser,buffer,buffer_len,
         ieos,ieos_len,flush,timeout,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        printf("asynOctetSyncIOReadOnce error %s\n",pasynUser->errorMessage);
    }
    asynOctetSyncIODisconnect(pasynUser);
    return status;
}

static asynStatus asynOctetSyncIOWriteReadOnce(const char *port, int addr,
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout,
                        int *nbytesOut, int *nbytesIn, int *eomReason)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = asynOctetSyncIOConnect(port,addr,&pasynUser);
    if(status!=asynSuccess) return status;
    status = asynOctetSyncIOWriteRead(pasynUser,write_buffer,write_buffer_len,
         read_buffer,read_buffer_len,
         ieos,ieos_len,timeout,nbytesOut,nbytesIn,eomReason);
    if(status!=asynSuccess) {
        printf("asynOctetSyncIOWriteReadOnce error %s\n",
            pasynUser->errorMessage);
    }
    asynOctetSyncIODisconnect(pasynUser);
    return status;
}
