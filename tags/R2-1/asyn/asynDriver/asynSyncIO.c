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
#include <drvGenericSerial.h>
#include <asynSyncIO.h>

typedef enum {
   asynSyncIO_FLUSH,
   asynSyncIO_READ,
   asynSyncIO_WRITE,
   asynSyncIO_WRITE_READ
} asynSyncIOOp;

typedef struct asynSyncIOPvt {
   epicsEventId event;
   asynOctet *pasynOctet;
   void *pdrvPvt;
   char *input_buff;
   int  input_len;
   const char *output_buff;
   int  output_len;
   char *ieos;
   int  ieos_len;
   char *oeos;
   int  oeos_len;
   double timeout;
   int  flush;
   asynSyncIOOp op;
   int retval;
} asynSyncIOPvt;

static asynStatus 
   asynSyncIOConnect(const char *port, int addr, asynUser **ppasynUser);
static asynStatus 
   asynSyncIOConnectSocket(const char *server, int port, asynUser **ppasynUser);
static int
    asynSyncIOWrite(asynUser *pasynUser, char const *buffer, int buffer_len, 
                    double timeout);
static int 
    asynSyncIORead(asynUser *pasynUser, char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout);
static int 
    asynSyncIOWriteRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout);
static asynStatus 
    asynSyncIOFlush(asynUser *pasynUser);

static asynSyncIO asynSyncIOManager = {
    asynSyncIOConnect,
    asynSyncIOConnectSocket,
    asynSyncIOWrite,
    asynSyncIORead,
    asynSyncIOWriteRead,
    asynSyncIOFlush
};
epicsShareDef asynSyncIO *pasynSyncIO = &asynSyncIOManager;

/* Timeout for queue callback.  0. means wait forever, don't remove entry
   from queue */
#define QUEUE_TIMEOUT 0.
/* Timeout for event waiting for queue. This time lets other threads talk 
 * on port  */
#define EVENT_TIMEOUT 1.

static void asynSyncIOCallback(asynUser *pasynUser);

static asynStatus 
   asynSyncIOConnect(const char *port, int addr, asynUser **ppasynUser)
{
    asynSyncIOPvt *pasynSyncIOPvt;
    asynUser *pasynUser;
    asynStatus status;
    asynInterface *pasynInterface;

    /* Create private structure */
    pasynSyncIOPvt = (asynSyncIOPvt *)calloc(1, sizeof(asynSyncIOPvt));

    /* Create asynUser, copy address to caller */
    pasynUser = pasynManager->createAsynUser(asynSyncIOCallback,0);
    pasynUser->userPvt = pasynSyncIOPvt;
    *ppasynUser = pasynUser;

    /* Create epicsEvent */
    pasynSyncIOPvt->event = epicsEventCreate(epicsEventEmpty);

    /* Look up port, addr */
    status = pasynManager->connectDevice(pasynUser, port, addr);    
    if (status != asynSuccess) {
      printf("Can't connect to port %s address %d\n", port, addr);
      return(-1);
    }

    /* Get asynOctet interface */
    pasynInterface = pasynManager->findInterface(pasynUser, asynOctetType, 1);
    if (!pasynInterface) {
       printf("%s driver not supported\n", asynOctetType);
       return(-1);
    }

    pasynSyncIOPvt->pasynOctet = (asynOctet *)pasynInterface->pinterface;
    pasynSyncIOPvt->pdrvPvt = pasynInterface->drvPvt;
    return(asynSuccess);
}

static asynStatus 
   asynSyncIOConnectSocket(const char *server, int port, asynUser **ppasynUser)
{
    char portString[20];
    char *serverString;
    int status;

    sprintf(portString, "%d", port);
    serverString = calloc(1, strlen(server)+strlen(portString)+3);
    strcpy(serverString, server);
    strcat(serverString, ":");
    strcat(serverString, portString);
    status = drvGenericSerialConfigure(serverString, serverString, 0, 0);
    status = asynSyncIOConnect(serverString, 0, ppasynUser);
    return(status);
}

static asynStatus 
   asynSyncIOQueueAndWait(asynUser *pasynUser, 
                          const char *output_buff, int output_len,
                          char *input_buff, int input_len,
                          const char *ieos, int ieos_len, 
                          int flush, double timeout,
                          asynSyncIOOp op)
{
    asynSyncIOPvt *pPvt = (asynSyncIOPvt *)pasynUser->userPvt;
    asynStatus status;
    epicsEventWaitStatus waitStatus;

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
    status = pasynManager->queueRequest(pasynUser, asynQueuePriorityLow, 
                                        QUEUE_TIMEOUT);
    if (status) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynSyncIOQueueAndWait queue request failed %s\n",
                 pasynUser->errorMessage);
       return(status);
    }

    /* Wait for event, signals I/O is complete
     * The user specified timeout is for reading the device.  We want
     * to also allow some time for other threads to talk to the device,
     * which means there could be some time before our message gets to the
     * head of the queue.  So add EVENT_TIMEOUT to timeout. 
     * If timeout is -1 this means wait forever, so don't do timeout at all*/
    if (timeout == -1.0)
       waitStatus = epicsEventWait(pPvt->event);
    else
       waitStatus = epicsEventWaitWithTimeout(pPvt->event, 
                                              timeout+EVENT_TIMEOUT);
    if (waitStatus!=epicsEventWaitOK) {
       asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                 "asynSyncIOQueueAndWait queue timeout\n");
       return(asynTimeout);
    }
    /* Return that status that the callback put in the private structure */
    return(pPvt->retval);
}
 

static int
    asynSyncIOWrite(asynUser *pasynUser, char const *buffer, int buffer_len, 
                    double timeout)
{
    int nout;

    nout = asynSyncIOQueueAndWait(pasynUser, buffer, buffer_len, NULL, 0, 
                                  NULL, 0, 0, timeout, asynSyncIO_WRITE);
    return(nout);
}


static asynStatus 
    asynSyncIOFlush(asynUser *pasynUser)
{
    asynStatus status;

    status = asynSyncIOQueueAndWait(pasynUser, NULL, 0, NULL, 0, 
                                    NULL, 0, 1, 0., asynSyncIO_FLUSH);
    return(status);
}

static int 
    asynSyncIORead(asynUser *pasynUser, char *buffer, int buffer_len, 
                   const char *ieos, int ieos_len, int flush, double timeout)
{
    int ninp;

    ninp = asynSyncIOQueueAndWait(pasynUser, NULL, 0, buffer, buffer_len, 
                                  ieos, ieos_len, flush, timeout, 
                                  asynSyncIO_READ);
    return(ninp);
}


static int 
    asynSyncIOWriteRead(asynUser *pasynUser, 
                        const char *write_buffer, int write_buffer_len,
                        char *read_buffer, int read_buffer_len,
                        const char *ieos, int ieos_len, double timeout)
{
    int ninp;

    ninp = asynSyncIOQueueAndWait(pasynUser, write_buffer, write_buffer_len, 
                                  read_buffer, read_buffer_len, 
                                  ieos, ieos_len, 1, timeout, 
                                  asynSyncIO_WRITE_READ);
    return(ninp);
}

static void asynSyncIOCallback(asynUser *pasynUser)
{
    asynSyncIOPvt *pPvt = (asynSyncIOPvt *)pasynUser->userPvt;
    asynOctet *pasynOctet = pPvt->pasynOctet;
    void *pdrvPvt = pPvt->pdrvPvt;
    int retval=0;

    pasynUser->timeout = pPvt->timeout;
    if (pPvt->flush) {
       retval = pasynOctet->flush(pdrvPvt, pasynUser);
       if (retval) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynSyncIO flush failed %s\n",
                    pasynUser->errorMessage);
       }
    }
    if ((pPvt->op == asynSyncIO_WRITE) || (pPvt->op ==asynSyncIO_WRITE_READ)) {
       retval = pasynOctet->write(pdrvPvt, pasynUser, 
                                  pPvt->output_buff, pPvt->output_len);
       if (retval != pPvt->output_len) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynSyncIO write failed %s\n",
                    pasynUser->errorMessage);
       } else {
          asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, 
                      pPvt->output_buff, pPvt->output_len,
                      "asynSyncIOWrote wrote\n");
       }
    }
    if ((pPvt->op == asynSyncIO_READ) || (pPvt->op == asynSyncIO_WRITE_READ)) {
       pasynOctet->setEos(pdrvPvt, pasynUser, pPvt->ieos, pPvt->ieos_len);
       retval = pasynOctet->read(pdrvPvt, pasynUser, 
                                 pPvt->input_buff, pPvt->input_len);
       if (retval < 0) {
          asynPrint(pasynUser, ASYN_TRACE_ERROR, 
                    "asynSyncIO read failed %s\n",
                    pasynUser->errorMessage);
       } else {
          /* Append a NULL byte to the response if there is room */
          if (retval < pPvt->input_len) pPvt->input_buff[retval] = '\0';
          asynPrintIO(pasynUser, ASYN_TRACEIO_DEVICE, 
                      pPvt->input_buff, pPvt->input_len,
                      "asynSyncIOR read\n");
       }
    }
    pPvt->retval = retval;

    /* Signal the epicsEvent to let the waiting thread know we're done */
    epicsEventSignal(pPvt->event);
}
