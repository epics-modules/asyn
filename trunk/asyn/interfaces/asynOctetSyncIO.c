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
#include <asynDrvUser.h>
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
   asynOctetSyncIO_READ_RAW,
   asynOctetSyncIO_WRITE,
   asynOctetSyncIO_WRITE_RAW,
   asynOctetSyncIO_WRITE_READ,
   asynOctetSyncIO_SET_INPUT_EOS,
   asynOctetSyncIO_GET_INPUT_EOS,
   asynOctetSyncIO_SET_OUTPUT_EOS,
   asynOctetSyncIO_GET_OUTPUT_EOS
} asynOctetSyncIOOp;

typedef struct syncPvt {
   epicsEventId event;
   asynCommon   *pasynCommon;
   void         *pcommonPvt;
   asynOctet    *pasynOctet;
   void         *pdrvPvt;
   char         *input_buff;
   int          input_len;
   const char   *output_buff;
   int          output_len;
   char         *eos;
   int          eos_len;
   int          *rtn_len;
   double       timeout;
   int          flush;
   asynStatus   status;
   size_t       nbytesOut;
   size_t       nbytesIn;
   int          eomReason;
   asynOctetSyncIOOp op;
} syncPvt;

/*local routines */
static asynStatus queueAndWait(asynUser *pasynUser, 
                          const char *output_buff, int output_len,
                          char *input_buff, int input_len,
                          const char *eos, int eos_len, int *rtn_len,
                          int flush, double timeout,
                          asynOctetSyncIOOp op);
static void processCallback(asynUser *pasynUser);

/*asynOctetSyncIO methods*/
static asynStatus connect(const char *port, int addr,
                               asynUser **ppasynUser, const char *drvInfo);
static asynStatus disconnect(asynUser *pasynUser);
static asynStatus openSocket(const char *server, int port,
                                         char **portName);
static asynStatus writeIt(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered);
static asynStatus writeRaw(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered);
static asynStatus readIt(asynUser *pasynUser, char *buffer, int buffer_len, 
                   double timeout, int *nbytesTransfered,int *eomReason);
static asynStatus readRaw(asynUser *pasynUser, char *buffer, int buffer_len, 
                   double timeout, int *nbytesTransfered,int *eomReason);
static asynStatus writeRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason);
static asynStatus flushIt(asynUser *pasynUser);
static asynStatus setInputEos(asynUser *pasynUser,
                     const char *eos,int eoslen);
static asynStatus getInputEos(asynUser *pasynUser,
                     char *eos, int eossize, int *eoslen);
static asynStatus setOutputEos(asynUser *pasynUser,
                     const char *eos,int eoslen);
static asynStatus getOutputEos(asynUser *pasynUser,
                     char *eos, int eossize, int *eoslen);
static asynStatus writeOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered, const char *drvInfo);
static asynStatus writeRawOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered, const char *drvInfo);
static asynStatus readOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason, const char *drvInfo);
static asynStatus readRawOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason, const char *drvInfo);
static asynStatus writeReadOnce(const char *port, int addr,
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason,
                        const char *drvInfo);
static asynStatus flushOnce(const char *port, int addr,const char *drvInfo);
static asynStatus setInputEosOnce(const char *port, int addr,
                        const char *eos,int eoslen,const char *drvInfo);
static asynStatus getInputEosOnce(const char *port, int addr,
                        char *eos, int eossize, int *eoslen,const char *drvInfo);
static asynStatus setOutputEosOnce(const char *port, int addr,
                        const char *eos,int eoslen,const char *drvInfo);
static asynStatus getOutputEosOnce(const char *port, int addr,
                        char *eos, int eossize, int *eoslen,const char *drvInfo);

static asynOctetSyncIO asynOctetSyncIOManager = {
    connect,
    disconnect,
    openSocket,
    writeIt,
    writeRaw,
    readIt,
    readRaw,
    writeRead,
    flushIt,
    setInputEos,
    getInputEos,
    setOutputEos,
    getOutputEos,
    writeOnce,
    writeRawOnce,
    readOnce,
    readRawOnce,
    writeReadOnce,
    flushOnce,
    setInputEosOnce,
    getInputEosOnce,
    setOutputEosOnce,
    getOutputEosOnce
};
epicsShareDef asynOctetSyncIO *pasynOctetSyncIO = &asynOctetSyncIOManager;

static asynStatus queueAndWait(asynUser *pasynUser, 
                          const char *output_buff, int output_len,
                          char *input_buff, int input_len,
                          const char *eos, int eos_len, int *rtn_len,
                          int flush, double timeout,
                          asynOctetSyncIOOp op)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;
    epicsEventWaitStatus waitStatus;
    int wasQueued;

    /* Copy parameters to private structure for use in callback */
    pPvt->output_buff = output_buff;
    pPvt->output_len = output_len;
    pPvt->input_buff = input_buff;
    pPvt->input_len = input_len;
    pPvt->eos = (char *)eos;
    pPvt->eos_len = eos_len;
    pPvt->rtn_len = rtn_len;
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
                        "queueAndWait Cancel request failed: %s",
                        pasynUser->errorMessage);
           }
           return(asynTimeout);
        }
    }
    if(status==asynSuccess) status = pPvt->status;
    /* Return that status that the callback put in the private structure */
    return(status);
}

static void processCallback(asynUser *pasynUser)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynOctet          *pasynOctet = pPvt->pasynOctet;
    void               *pdrvPvt = pPvt->pdrvPvt;
    asynStatus         status = asynSuccess;

    pasynUser->timeout = pPvt->timeout;
    if (pPvt->op == asynOctetSyncIO_SET_INPUT_EOS) {
        status = pasynOctet->setInputEos(pdrvPvt,pasynUser,
            pPvt->eos,pPvt->eos_len);
        if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO setInputEos failed %s\n",
                    pasynUser->errorMessage);
        }
        goto done;
    }
    if (pPvt->op == asynOctetSyncIO_GET_INPUT_EOS) {
        status = pasynOctet->getInputEos(pdrvPvt,pasynUser,
            pPvt->eos,pPvt->eos_len,pPvt->rtn_len);
        if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO getInputEos failed %s\n",
                    pasynUser->errorMessage);
        }
        goto done;
    }
    if (pPvt->op == asynOctetSyncIO_SET_OUTPUT_EOS) {
        status = pasynOctet->setOutputEos(pdrvPvt,pasynUser,
            pPvt->eos,pPvt->eos_len);
        if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO setOutputEos failed %s\n",
                    pasynUser->errorMessage);
        }
        goto done;
    }
    if (pPvt->op == asynOctetSyncIO_GET_OUTPUT_EOS) {
        status = pasynOctet->getOutputEos(pdrvPvt,pasynUser,
            pPvt->eos,pPvt->eos_len,pPvt->rtn_len);
        if(status!=asynSuccess) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynOctetSyncIO getOutputEos failed %s\n",
                    pasynUser->errorMessage);
        }
        goto done;
    }
    pPvt->nbytesOut = pPvt->nbytesIn = 0;
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
    if ((pPvt->op == asynOctetSyncIO_WRITE)
    || (pPvt->op ==asynOctetSyncIO_WRITE_RAW)
    || (pPvt->op ==asynOctetSyncIO_WRITE_READ)) {
       if (pPvt->op == asynOctetSyncIO_WRITE_RAW) {
           status = pasynOctet->writeRaw(pdrvPvt, pasynUser, 
                   pPvt->output_buff, pPvt->output_len,&pPvt->nbytesOut);
       } else {
           status = pasynOctet->write(pdrvPvt, pasynUser, 
                   pPvt->output_buff, pPvt->output_len,&pPvt->nbytesOut);
       }
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
    if ((pPvt->op == asynOctetSyncIO_READ)
    || (pPvt->op ==asynOctetSyncIO_READ_RAW)
    || (pPvt->op == asynOctetSyncIO_WRITE_READ)) {
       if(pPvt->op ==asynOctetSyncIO_READ_RAW) {
           status = pasynOctet->readRaw(pdrvPvt, pasynUser, 
              pPvt->input_buff,pPvt->input_len,&pPvt->nbytesIn,&pPvt->eomReason);
       } else {
           status = pasynOctet->read(pdrvPvt, pasynUser, 
              pPvt->input_buff,pPvt->input_len,&pPvt->nbytesIn,&pPvt->eomReason);
       }
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
done:
    pPvt->status = status;
    /* Signal the epicsEvent to let the waiting thread know we're done */
    if(pPvt->event) epicsEventSignal(pPvt->event);
}

static asynStatus connect(const char *port, int addr,
           asynUser **ppasynUser,const char *drvInfo)
{
    syncPvt *psyncPvt;
    asynUser *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;
    int isConnected;
    int canBlock;

    /* Create private structure */
    psyncPvt = (syncPvt *)calloc(1, sizeof(syncPvt));
    /* Create asynUser, copy address to caller */
    pasynUser = pasynManager->createAsynUser(processCallback,0);
    pasynUser->userPvt = psyncPvt;
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
        psyncPvt->event = epicsEventCreate(epicsEventEmpty);
    }
    /* Get asynCommon interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynCommonType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynCommonType);
       return(asynError);
    }
    psyncPvt->pasynCommon = (asynCommon *)pasynInterface->pinterface;
    psyncPvt->pcommonPvt = pasynInterface->drvPvt;
    /* Get asynOctet interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynOctetType);
       return(asynError);
    }
    psyncPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    psyncPvt->pdrvPvt = pasynInterface->drvPvt;

    /* Get asynDrvUser interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynDrvUserType, 1);
    if(pasynInterface && drvInfo) {
        asynDrvUser *pasynDrvUser;
        void       *drvPvt;
        pasynDrvUser = (asynDrvUser *)pasynInterface->pinterface;
        drvPvt = pasynInterface->drvPvt;
        status = pasynDrvUser->create(drvPvt,pasynUser,drvInfo,0,0);
        if(status!=asynSuccess) {
            printf("asynOctetSyncIO::connect drvUserCreate drvInfo=%s %s\n",
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
       status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       NULL, 0, 0,
                       0,0., asynOctetSyncIO_DRIVER_CONNECT);
       if (status != asynSuccess) {
          printf("Error connecting to device %s\n", pasynUser->errorMessage);
          return(status);
       }
    }
    return(asynSuccess);
}

static asynStatus disconnect(asynUser *pasynUser)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
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

static asynStatus openSocket(const char *server, int port,
       char **portName)
{
    char portString[20];
    asynStatus status;

    sprintf(portString, "%d", port);
    *portName = calloc(1, strlen(server)+strlen(portString)+3);
    strcpy(*portName, server);
    strcat(*portName, ":");
    strcat(*portName, portString);
    status = drvAsynIPPortConfigure(*portName, *portName, 0, 0, 0,0);
    return(status);
}

static asynStatus writeIt(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;

    status = queueAndWait(pasynUser, buffer, buffer_len, NULL, 0, 
                       NULL, 0, 0,
                       0,timeout, asynOctetSyncIO_WRITE);
    *nbytesTransfered = pPvt->nbytesOut;
    return(status);
}

static asynStatus writeRaw(asynUser *pasynUser,
    char const *buffer, int buffer_len, double timeout,int *nbytesTransfered)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;

    status = queueAndWait(pasynUser, buffer, buffer_len, NULL, 0, 
                       NULL, 0, 0,
                       0,timeout, asynOctetSyncIO_WRITE_RAW);
    *nbytesTransfered = pPvt->nbytesOut;
    return(status);
}

static asynStatus readIt(asynUser *pasynUser,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;

    status = queueAndWait(pasynUser,
                       NULL, 0, buffer, buffer_len, 
                       0, 0, 0,
                       0,timeout, asynOctetSyncIO_READ);
    *nbytesTransfered = pPvt->nbytesIn;;
    if(eomReason) *eomReason = pPvt->eomReason;
    return(status);
}

static asynStatus readRaw(asynUser *pasynUser,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;

    status = queueAndWait(pasynUser,
                       NULL, 0, buffer, buffer_len, 
                       0, 0, 0,
                       0,timeout, asynOctetSyncIO_READ_RAW);
    *nbytesTransfered = pPvt->nbytesIn;;
    if(eomReason) *eomReason = pPvt->eomReason;
    return(status);
}

static asynStatus writeRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        double timeout,
                        int *nbytesOut, int *nbytesIn,int *eomReason)
{
    syncPvt *pPvt = (syncPvt *)pasynUser->userPvt;
    asynStatus status;

    status = queueAndWait(pasynUser,
                       write_buffer, write_buffer_len, 
                       read_buffer, read_buffer_len, 
                       0, 0, 0,
                       1,timeout, asynOctetSyncIO_WRITE_READ);
    *nbytesOut = pPvt->nbytesOut;
    *nbytesIn = pPvt->nbytesIn;
    if(eomReason) *eomReason = pPvt->eomReason;
    return status;
}

static asynStatus
    flushIt(asynUser *pasynUser)
{
    asynStatus status;

    status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       NULL, 0, 0,
                       1,0., asynOctetSyncIO_FLUSH);
    return(status);
}

static asynStatus setInputEos(asynUser *pasynUser,
                     const char *eos,int eoslen)
{
    asynStatus status;

    status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       eos, eoslen, 0,
                       0,0., asynOctetSyncIO_SET_INPUT_EOS);
    return(status);
}

static asynStatus getInputEos(asynUser *pasynUser,
                     char *eos, int eossize, int *eoslen)
{
    asynStatus status;

    status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       eos, eossize, eoslen,
                       0,0., asynOctetSyncIO_GET_INPUT_EOS);
    return(status);
}

static asynStatus setOutputEos(asynUser *pasynUser,
                     const char *eos,int eoslen)
{
    asynStatus status;

    status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       eos, eoslen, 0,
                       0,0., asynOctetSyncIO_SET_OUTPUT_EOS);
    return(status);
}

static asynStatus getOutputEos(asynUser *pasynUser,
                     char *eos, int eossize, int *eoslen)
{
    asynStatus status;

    status = queueAndWait(pasynUser, NULL, 0, NULL, 0, 
                       eos, eossize, eoslen,
                       0,0., asynOctetSyncIO_GET_INPUT_EOS);
    return(status);
}

static asynStatus writeOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = writeIt(pasynUser,buffer,buffer_len,timeout,nbytesTransfered);
    if(status!=asynSuccess) {
        printf("writeOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus writeRawOnce(const char *port, int addr,
                    char const *buffer, int buffer_len, double timeout,
                    int *nbytesTransfered,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = writeRaw(pasynUser,buffer,buffer_len,timeout,nbytesTransfered);
    if(status!=asynSuccess) {
        printf("writeOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = readIt(pasynUser,buffer,buffer_len,
         timeout,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        printf("readOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus readRawOnce(const char *port, int addr,
                   char *buffer, int buffer_len, 
                   double timeout,
                   int *nbytesTransfered,int *eomReason,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = readRaw(pasynUser,buffer,buffer_len,
         timeout,nbytesTransfered,eomReason);
    if(status!=asynSuccess) {
        printf("readOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus writeReadOnce(const char *port, int addr,
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        double timeout,
                        int *nbytesOut, int *nbytesIn, int *eomReason,
                        const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = writeRead(pasynUser,write_buffer,write_buffer_len,
         read_buffer,read_buffer_len,
         timeout,nbytesOut,nbytesIn,eomReason);
    if(status!=asynSuccess) {
        printf("writeReadOnce error %s\n",
            pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}
static asynStatus flushOnce(const char *port, int addr,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = flushIt(pasynUser);
    if(status!=asynSuccess) {
        printf("flushOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus setInputEosOnce(const char *port, int addr,
                        const char *eos,int eoslen,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = setInputEos(pasynUser,eos,eoslen);
    if(status!=asynSuccess) {
        printf("setInputEosOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus getInputEosOnce(const char *port, int addr,
                        char *eos, int eossize, int *eoslen,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = getInputEos(pasynUser,eos,eossize,eoslen);
    if(status!=asynSuccess) {
        printf("getInputEosOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus setOutputEosOnce(const char *port, int addr,
                        const char *eos,int eoslen,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = setOutputEos(pasynUser,eos,eoslen);
    if(status!=asynSuccess) {
        printf("setOutputEosOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}

static asynStatus getOutputEosOnce(const char *port, int addr,
                        char *eos, int eossize, int *eoslen,const char *drvInfo)
{
    asynStatus status;
    asynUser   *pasynUser;

    status = connect(port,addr,&pasynUser,drvInfo);
    if(status!=asynSuccess) return status;
    status = getOutputEos(pasynUser,eos,eossize,eoslen);
    if(status!=asynSuccess) {
        printf("getOutputEosOnce error %s\n",pasynUser->errorMessage);
    }
    disconnect(pasynUser);
    return status;
}
