/*
 * ASYN support for HiSLIP
 *

 ***************************************************************************
 * Copyright (c) 2020 N. Yamamoto <noboru.yamamoto@kek.jp>
 * based on AsynUSBTMC supoort by
 * Copyright (c) 2013 W. Eric Norum <wenorum@lbl.gov>                      *
 * This file is distributed subject to a Software License Agreement found  *
 * in the file LICENSE that is included with this distribution.            *
 ***************************************************************************
 */
#include "drvAsynHiSLIP.h"

using nsHiSLIP::CC_request_t;
using nsHiSLIP::CC_Lock_t;
using nsHiSLIP::HiSLIP_t;
using nsHiSLIP::HiSLIP;
using nsHiSLIP::Message_t;

#define BULK_IO_HEADER_SIZE      16
#define BULK_IO_PAYLOAD_CAPACITY 4096
#define IDSTRING_CAPACITY        100

#define ASYN_REASON_SRQ 4345
#define ASYN_REASON_STB 4346
#define ASYN_REASON_REN 4347

// HiSLIP methods
  void HiSLIP::connect(const char *hostname, const char *dev_name, const int port, const char *vendor_id){
  osiSockAddr addr;
  Message_t *msg,*resp;
  int status;
  
  if(hostToIPAddr(hostname, &addr.ia.sin_addr) < 0) {
    errlogPrintf("Unknown host \"%s\"", hostname);
  }
  
  addr.ia.sin_port=htons(port);
  addr.ia.sin_family=AF_INET;
  
  this->sync_channel= ::socket(AF_INET, SOCK_STREAM, 0);
  this->async_channel=::socket(AF_INET, SOCK_STREAM, 0);

  status = ::connect(this->sync_channel, &addr.sa, sizeof(addr));
  if (status!=0){
    // Error handling
    perror(__FUNCTION__);
  }
  status = ::connect(this->async_channel, &addr.sa, sizeof(addr));
  if (status!=0){
    // Error handling
    perror(__FUNCTION__);
  }

  msg= new Message();

  msg->message_type=nsHiSLIP::Initialize;
  msg->message_parameter.initParm.protocol_version=nsHiSLIP::PROTOCOL_VERSION_MAX;
  memcpy(msg->message_parameter.initParm.vendor_id, vendor_id,2);
  msg->send(this->sync_channel);

  resp=new Message();
  resp->recv(this->sync_channel, nsHiSLIP::InitializeResponse);
  this->overlap_mode=resp->control_code;
  this->session_id=resp->message_parameter.initResp.session_id;
  this->server_protocol_version=resp->message_parameter.initResp.protocol_version;
    
  msg= new Message(nsHiSLIP::AsyncInitialize);
  msg->message_parameter.word=this->session_id;
  msg->send(this->sync_channel);

  resp=new Message();
  resp->recv(this->sync_channel, nsHiSLIP::AsyncInitializeResponse);
  this->overlap_mode=resp->control_code;
  this->session_id=resp->message_parameter.initResp.session_id;
  this->server_vendorID=resp->message_parameter.word;

  this->sync_poll.fd=this->sync_channel;
  this->sync_poll.events=POLLIN;
  this->sync_poll.revents=0;

  this->async_poll.fd=this->async_channel;
  this->async_poll.events=POLLIN;
  this->async_poll.revents=0;

    
  };

long HiSLIP::set_max_size(long message_size){
  Message *resp;
  epicsUInt64 msg_size=htobe64(message_size);

  Message *msg=new Message(nsHiSLIP::AsyncMaximumMessageSize,
			   0,
			   new message_parameter(0),
			   sizeof(msg_size),&msg_size);

  msg->send(this->async_channel);

  resp->recv(this->async_channel,nsHiSLIP::AsyncMaximumMessageSizeResponse);
  
  this->maximum_message_size=*((epicsUInt64 *)(resp->payload));
  return this->maximum_message_size;
  
};

asynStatus HiSLIP::device_clear(){
  Message resp;
  epicsUInt8 feature_preference;
  
  Message *msg=new Message(nsHiSLIP::AsyncDeviceClear,
			   0,
			   0,
			   0,NULL);

  msg->send(this->async_channel);

  resp.recv(this->async_channel,nsHiSLIP::AsyncDeviceClearAcknowledge);
  feature_preference=resp.control_code;

  msg=new Message(nsHiSLIP::DeviceClearComplete,
		  feature_preference,
		  0,
		  0, NULL);

  msg->send(this->sync_channel);
  
  resp.recv(this->async_channel,nsHiSLIP::DeviceClearAcknowledge);
  
  this->overlap_mode=resp.control_code;
  this->reset_message_id();
  this->rmt_delivered = false;
  
  return asynSuccess;
  
};

epicsUInt8 HiSLIP::status_query(){
  epicsUInt8 status;
  Message resp;
  
  Message *msg=new Message((epicsUInt8) nsHiSLIP::AsyncStatusQuery,
			   (epicsUInt8) this->rmt_delivered,
			   new nsHiSLIP::message_parameter((epicsUInt32) this->most_recent_message_id),
			   0, NULL);
  msg->send(this->async_channel);

  resp.recv(this->async_channel,nsHiSLIP::AsyncStatusResponse);
  status= resp.control_code &0xff;
  return status;
}

  // EPICS Async Support 
typedef struct drvPvt {
  /*
   * Matched device
   * HiSLIP private
  */
  
  HiSLIP_t *device;
  const char            *hostname;
  const long             port;
  bool      isConnected;
  int       termChar;

  char                   deviceVendorID[IDSTRING_CAPACITY];

  /*
   * Asyn interfaces we provide
   */
  char                  *portName;
  asynInterface          asynCommon;
  asynInterface          asynOctet;
  void                  *asynOctetInterruptPvt;
  // asynInterface          asynInt32;
  //  void                  *asynInt32InterruptPvt;
  asynInterface          asynDrvUser;
  
  /*
     * Interrupt endpoint handling
     */
    char                  *interruptThreadName;
    epicsThreadId          interruptTid;
    epicsMutexId           interruptTidMutex;
    epicsEventId           pleaseTerminate;
    epicsEventId           didTerminate;
    epicsMessageQueueId    statusByteMessageQueue;

    /*
     * I/O buffer
     */
    unsigned char          buf[BULK_IO_HEADER_SIZE+BULK_IO_PAYLOAD_CAPACITY];
    int                    bufCount;
    const unsigned char   *bufp;
    unsigned char          bulkInPacketFlags;

    /*
     * Statistics
     */
    size_t                 connectionCount;
    size_t                 interruptCount;
    size_t                 bytesSentCount;
    size_t                 bytesReceivedCount;
} drvPvt;

static asynStatus disconnect(void *pvt, asynUser *pasynUser);


/*
 * Interrupt endpoint support
 */
static void
interruptThread(void *arg)
{
    drvPvt *pdpvt = (drvPvt *)arg;
    int s;
    
    for (;;) {
      s =  pdpvt->device->wait_for_SRQ(65000); 
      
      if (epicsEventTryWait(pdpvt->pleaseTerminate) == epicsEventWaitOK)
	break;

      Message_t srqmsg=pdpvt->device->get_Service_Request();
      epicsUInt8 st =srqmsg.control_code;
      
      if ((s == 0)&& (st != 0)){
	if (st == 0x81) {
	  ELLLIST *pclientList;
	  interruptNode *pnode;

	  pdpvt->interruptCount++;
	  pasynManager->interruptStart(pdpvt->asynOctetInterruptPvt, &pclientList);
	  pnode = (interruptNode *)ellFirst(pclientList);
	  while (pnode) {
	    asynOctetInterrupt *int32Interrupt = (asynOctetInterrupt *) pnode->drvPvt;
	    pnode = (interruptNode *)ellNext(&pnode->node);
	    if (int32Interrupt->pasynUser->reason == ASYN_REASON_SRQ) {
	      int32Interrupt->callback(int32Interrupt->userPvt,
				       int32Interrupt->pasynUser,
				       st);
	    }
	  }
	  pasynManager->interruptEnd(pdpvt->asynOctetInterruptPvt);
	}
	else if (st== 0x82) {
	  if (epicsMessageQueueTrySend(pdpvt->statusByteMessageQueue,
				       &st, 1) != 0) {
	    errlogPrintf("----- WARNING ----- "
			 "Can't send status byte to worker thread!\n");
	  }
	}
      }
      else if (NULL){
	errlogPrintf("----- WARNING ----- "
		     "libusb_interrupt_transfer failed (%s).  "
		     "Interrupt thread for ASYN port \"%s\" terminating.\n",
		     nsHiSLIP::Error_Messages[s], pdpvt->portName);
	break;
      }
    }
    epicsMutexLock(pdpvt->interruptTidMutex);
    pdpvt->interruptTid = 0;
    epicsEventSignal(pdpvt->didTerminate);
    epicsMutexUnlock(pdpvt->interruptTidMutex);
}

static void
startInterruptThread(drvPvt *pdpvt)
{
    epicsMutexLock(pdpvt->interruptTidMutex);
    if ((pdpvt->interruptTid == 0)) {
        epicsEventTryWait(pdpvt->pleaseTerminate);
        epicsEventTryWait(pdpvt->didTerminate);
        pdpvt->interruptTid = epicsThreadCreate(pdpvt->interruptThreadName,
                                epicsThreadGetPrioritySelf(),
                                epicsThreadGetStackSize(epicsThreadStackSmall),
                                interruptThread, pdpvt);
        if (pdpvt->interruptTid == 0)
            errlogPrintf("----- WARNING ----- "
			 "Can't start interrupt handler thread %s.\n",
			 pdpvt->interruptThreadName);
    }
    epicsMutexUnlock(pdpvt->interruptTidMutex);
}

/*
 * Decode a status byte
 */
static void
showHexval(FILE *fp, const char *name, int val, int bitmask, const char *bitname, ...)
{
    const char *sep = " -- ";
    va_list ap;
    va_start(ap, bitname);
    fprintf(fp, "%28s: ", name);
    if (bitmask)
        fprintf(fp, "%#x", val);
    else
        fprintf(fp, "%#4.4x", val);
    for (;;) {
        if (((bitmask > 0) && ((val & bitmask)) != 0)
         || ((bitmask < 0) && ((val & -bitmask)) == 0)
         || ((bitmask == 0) && (bitname != NULL) && (bitname[0] != '\0'))) {
            fprintf(fp, "%s%s", sep, bitname);
            sep = ", ";
        }
        if (bitmask == 0)
            break;
        bitmask = va_arg(ap, int);
        if (bitmask == 0)
            break;
        bitname = va_arg(ap, char *);
    }
    fprintf(fp, "\n");
    va_end(ap);
}

/*
 * Show a byte number
 */
static void
pcomma(FILE *fp, size_t n)
{
    if (n < 1000) {
        fprintf(fp, "%zu", n);
        return;
    }
    pcomma(fp, n/1000);
    fprintf(fp, ",%03zu", n%1000);
}
    
static void
showCount(FILE *fp, const char *label, size_t count)
{
    fprintf(fp, "%22s Count: ", label);
    pcomma(fp, count);
    fprintf(fp, "\n");
}

/*
 * asynCommon methods
 */
static void
report(void *pvt, FILE *fp, int details)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    fprintf(fp, "%20sonnected, Interrupt handler thread %sactive\n",
                                            pdpvt->isConnected ? "C" : "Disc",
                                            pdpvt->interruptTid ? "" : "in");
    if (details > 0) {
        if (pdpvt->termChar >= 0)
            fprintf(fp, "%28s: %x\n", "Terminator", pdpvt->termChar);
    }
    if (details > 1) {
        showCount(fp, "Connection", pdpvt->connectionCount);
        showCount(fp, "Interrupt", pdpvt->interruptCount);
        showCount(fp, "Send", pdpvt->bytesSentCount);
        showCount(fp, "Receive", pdpvt->bytesReceivedCount);
    }
    if (details >= 100) {
        int l = details % 100;
        fprintf(fp, "==== Set libusb debug level %d ====\n", l);
    }
};

/*
 * Disconnect when it appears that device has gone away
 */
static void
disconnectIfGone(drvPvt *pdpvt, asynUser *pasynUser, int s)
{
  if(s == nsHiSLIP::ServerRefusedConnection){
    disconnect(pdpvt, pasynUser);
  }
}

/*
 * Check results of control transfer
 */
/*
 * Clear input/output buffers
 */
static asynStatus
clearBuffers(drvPvt *pdpvt, asynUser *pasynUser)
{
    int s;
    asynStatus status;
    int pass = 0;

    status = pdpvt->device->device_clear();
    if (status != asynSuccess)
        return status;
    for (;;) {
        epicsThreadSleep(0.01);
	// I don't know why this is necessary, but without some delay here the CHECK_CLEAR_STATUS seems to be stuck at STATUS_PENDING
	status = pdpvt->device->device_clear();
        if (status != asynSuccess)
            return asynError;
        switch (++pass) {
        case 5:
            asynPrint(pasynUser, ASYN_TRACE_ERROR, "Note -- RESET DEVICE.\n");
            //s = libusb_reset_device(pdpvt->handle);
	    status = pdpvt->device->device_clear();
            if (s != 0) {
                epicsSnprintf(pasynUser->errorMessage,
			      pasynUser->errorMessageSize,
			      "hislip_reset_device() failed: %s",
			      nsHiSLIP::Error_Messages[s]);
                return asynError;
            }
            break;

        case 10:
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "CHECK_CLEAR_STATUS remained 'STATUS_PENDING' for too long");
            return asynError;
        }
        asynPrint(pasynUser, ASYN_TRACE_ERROR,
                                    "Note -- retrying CHECK_CLEAR_STATUS\n");
    }
    return asynSuccess;
}

static asynStatus
connect(void *pvt, asynUser *pasynUser)
{
  drvPvt *pdpvt = (drvPvt *)pvt;

  if (!pdpvt->isConnected) {
    pdpvt->device->connect(pdpvt->hostname);
  }
  pdpvt->isConnected = 1;
  pasynManager->exceptionConnect(pasynUser);
  return asynSuccess;
}

static asynStatus
disconnect(void *pvt, asynUser *pasynUser)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (pdpvt->isConnected) {
        int pass = 0;
        epicsThreadId tid;
        for (;;) {

            epicsMutexLock(pdpvt->interruptTidMutex);
            tid = pdpvt->interruptTid;
            epicsMutexUnlock(pdpvt->interruptTidMutex);
            if (tid == 0)
                break;
            if (++pass == 10) {
                errlogPrintf("----- WARNING ----- Thread %s won't terminate!\n",
                                                    pdpvt->interruptThreadName);
                break;
            }

            /*
             * Send signal then force an Interrupt-In message
             */
            epicsEventSignal(pdpvt->pleaseTerminate);
	    pdpvt->device->status_query();
            // libusb_control_transfer(pdpvt->handle,
            //     0xA1, // bmRequestType: Dir=IN, Type=CLASS, Recipient=INTERFACE
            //     128,  // bRequest: READ_STATUS_BYTE
            //     127,                     // wValue (bTag)
            //     pdpvt->bInterfaceNumber, // wIndex
            //     cbuf,                    // data
            //     3,                       // wLength
            //     100);                    // timeout (ms)
            epicsEventWaitWithTimeout(pdpvt->didTerminate, 2.0);
        }
        pdpvt->device->disconnect();
    }
    pdpvt->isConnected = 0;
    pasynManager->exceptionDisconnect(pasynUser);
    return asynSuccess;
}
static asynCommon commonMethods = { report, connect, disconnect };

/*
 * asynOctet methods
 */
static asynStatus
asynOctetWrite(void *pvt, asynUser *pasynUser,
               const char *data, size_t numchars, size_t *nbytesTransfered)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;

    /*
     * Common to all writes
     */

    /*
     * Send
     */
    *nbytesTransfered = 0;
    while (numchars) {
        int nSend, pkSend, pkSent;
        int s;
        s = pdpvt->device->write( pdpvt->buf, timeout);
	
        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "Bulk transfer failed: %s",
			  nsHiSLIP::Error_Messages[s]);
            return asynError;
        }
        if (s!= nSend) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                        "Asked to send %d, actually sent %d", pkSend, pkSent);
            return asynError;
        }
        data += nSend;
        numchars -= nSend;
        *nbytesTransfered += nSend;
    }
    pdpvt->bytesSentCount += *nbytesTransfered;
    return asynSuccess;
}

static asynStatus
asynOctetRead(void *pvt, asynUser *pasynUser,
              char *data, size_t maxchars, size_t *nbytesTransfered,
              int *eomReason)
{
    drvPvt *pdpvt = (drvPvt *)pvt;
    unsigned char bTag;
    int s;
    int nCopy, ioCount, payloadSize;
    int eom = 0;
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;

    *nbytesTransfered = 0;
    for (;;) {
        /*
         * Special case for stream device which requires an asynTimeout return.
         */
        ioCount = pdpvt->device->read(
				 data, BULK_IO_HEADER_SIZE,
				 timeout);
        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage,
			  pasynUser->errorMessageSize,
			  "Bulk transfer request failed: %s",
			  nsHiSLIP::Error_Message[s]);
            return asynError;
        }

        /*
         * Read back
         */
	s =  pdpvt->device.read(sizeof pdpvt->buf, &ioCount, timeout);

        if (s) {
            disconnectIfGone(pdpvt, pasynUser, s);
            epicsSnprintf(pasynUser->errorMessage,
			  pasynUser->errorMessageSize,
			  "Bulk read failed: %s",
			  nsHiSLIP::Error_Messages[2]);
            return asynError;
        }
        asynPrintIO(pasynUser, ASYN_TRACEIO_DRIVER, (const char *)pdpvt->buf,
                        ioCount, "Read %d, flags %#x: ", ioCount, pdpvt->buf[8]);

        /*
         * Sanity check on transfer
         */
        if (ioCount < BULK_IO_HEADER_SIZE) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Incomplete packet header (read only %d)", ioCount);
            return asynError;
        }
        if (payloadSize > (ioCount - BULK_IO_HEADER_SIZE)) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                "Packet header claims %d sent, but packet contains only %d",
                            payloadSize, ioCount - BULK_IO_HEADER_SIZE);
            return asynError;
        }
        if (payloadSize > BULK_IO_PAYLOAD_CAPACITY) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Packet header claims %d sent, but requested only %d",
                                    payloadSize, BULK_IO_PAYLOAD_CAPACITY);
            return asynError;
        }
        pdpvt->bufCount = payloadSize;
    }
}

/*
 * I see no mechanism for determining when it is necessary/possible to issue
 * MESSAGE_ID_REQUEST_DEV_DEP_MSG_IN requests and transfers from the bulk-IN
 * endpoint.  I welcome suggestions from libusb experts.
 */
static asynStatus
asynOctetFlush(void *pvt, asynUser *pasynUser)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    pdpvt->bufCount = 0;
    pdpvt->bulkInPacketFlags = 0;
    return asynSuccess;
}

static asynStatus
asynOctetSetInputEos(void *pvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    switch (eoslen) {
    case 0:
        pdpvt->termChar = -1;
        break;
    case 1:
        if ((pdpvt->tmcDeviceCapabilities & 0x1) == 0) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                            "Device does not support terminating characters");
            return asynError;
        }
        pdpvt->termChar = *eos & 0xFF;
        break;
    default:
        epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Device does not support multiple terminating characters");
        return asynError;
    }
    return asynSuccess;
}

static asynStatus
asynOctetGetInputEos(void *pvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (pdpvt->termChar < 0) {
        *eoslen = 0;
    }
    else {
        if (eossize < 1) return asynError;
        *eos = pdpvt->termChar;
        *eoslen = 1;
    }
    return asynSuccess;
}

static asynStatus
asynOctetSetOutputEos(void *pvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    return asynError;
}

static asynStatus
asynOctetGetOutputEos(void *pvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    return asynError;
}

static asynOctet octetMethods = { 
    .write        = asynOctetWrite, 
    .read         = asynOctetRead, 
    .flush        = asynOctetFlush,
    .setInputEos  = asynOctetSetInputEos,
    .getInputEos  = asynOctetGetInputEos,
    .setOutputEos = asynOctetSetOutputEos,
    .getOutputEos = asynOctetGetOutputEos,
};

/*
 * drvUser methods
 */
static asynStatus
asynDrvUserCreate(void *pvt, asynUser *pasynUser,
                  const char *drvInfo, const char **pptypeName, size_t *psize)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (epicsStrCaseCmp(drvInfo, "SRQ") == 0) {
        pasynUser->reason = ASYN_REASON_SRQ;
        pdpvt->enableInterruptEndpoint = 1;
        if (pdpvt->isConnected)
            startInterruptThread(pdpvt);
    }
    else if (epicsStrCaseCmp(drvInfo, "REN") == 0) {
        pasynUser->reason = ASYN_REASON_REN;
    }
    else if (epicsStrCaseCmp(drvInfo, "STB") == 0) {
        pasynUser->reason = ASYN_REASON_STB;
    }
    return asynSuccess;
}

static asynStatus
asynDrvUserGetType(void *drvPvt, asynUser *pasynUser,
                   const char **pptypeName, size_t *psize)
{
    return asynSuccess;
}

static asynStatus
asynDrvUserDestroy(void *drvPvt, asynUser *pasynUser)
{
    return asynSuccess;
}

static asynDrvUser drvUserMethods = {
    .create  = asynDrvUserCreate,
    .getType = asynDrvUserGetType,
    .destroy = asynDrvUserDestroy,
};

/*
 * Device configuration
 */
void
HiSLIPConfigure(const char *portName,
                const char *hostInfo,
                int priority)
{
    drvPvt *pdpvt;
    int s;
    asynStatus status;
    int flags=0;
    
    /*
     * Set up local storage
     */
    pdpvt = (drvPvt *)callocMustSucceed(1, sizeof(drvPvt), portName);
    pdpvt->portName = epicsStrDup(portName);
    pdpvt->interruptThreadName = callocMustSucceed(1, strlen(portName)+5, portName);
    epicsSnprintf(pdpvt->interruptThreadName, sizeof pdpvt->interruptThreadName,
		  "%sIntr", portName);
    if (priority == 0) priority = epicsThreadPriorityMedium;
    pdpvt->device=new HiSLIP_t(hostInfo);
    if (s != 0) {
        printf("libusb_init() failed: %s\n", nsHiSLIP::Error_Messages[2]);
        return;
    }
    
    pdpvt->termChar = -1;
    pdpvt->interruptTidMutex = epicsMutexMustCreate();
    pdpvt->pleaseTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->didTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->statusByteMessageQueue = epicsMessageQueueCreate(1, 1);
    if (!pdpvt->statusByteMessageQueue) {
        printf("Can't create message queue!\n");
        return;
    }

    /*
     * Create our port
     */
    status = pasynManager->registerPort(pdpvt->portName,
                                        ASYN_CANBLOCK,
                                        (flags & 0x1) == 0,
                                        priority, 0);
    if(status != asynSuccess) {
        printf("registerPort failed\n");
        return;
    }

    /*
     * Register ASYN interfaces
     */
    pdpvt->asynCommon.interfaceType = asynCommonType;
    pdpvt->asynCommon.pinterface  = &commonMethods;
    pdpvt->asynCommon.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName, &pdpvt->asynCommon);
    if (status != asynSuccess) {
        printf("registerInterface failed\n");
        return;
    }

    pdpvt->asynOctet.interfaceType = asynOctetType;
    pdpvt->asynOctet.pinterface  = &octetMethods;
    pdpvt->asynOctet.drvPvt = pdpvt;
    status = pasynOctetBase->initialize(pdpvt->portName, &pdpvt->asynOctet, 0, 0, 0);
    if (status != asynSuccess) {
        printf("pasynOctetBase->initialize failed\n");
        return;
    }

    /*
     * Always register an interrupt source, just in case we use SRQs
     */
    pasynManager->registerInterruptSource(pdpvt->portName,
                                         &pdpvt->asynOctet,
                                         &pdpvt->asynOctetInterruptPvt);

    pdpvt->asynDrvUser.interfaceType = asynDrvUserType;
    pdpvt->asynDrvUser.pinterface = &drvUserMethods;
    pdpvt->asynDrvUser.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName,
					     &pdpvt->asynDrvUser);
    if (status != asynSuccess) {
        printf("Can't register drvUser\n");
        return;
    }
}

/*
 * IOC shell command registration
 */
static const iocshArg HiSLIPConfigureArg0 = {"port name",
					     iocshArgString};
static const iocshArg HiSLIPConfigureArg1 = {"HiSLIP host addressr",
					     iocshArgString};
static const iocshArg HiSLIPConfigureArg2 = {"priority",
					     iocshArgInt};
static const iocshArg *HiSLIPConfigureArgs[] = {
						&HiSLIPConfigureArg0
                                                , &HiSLIPConfigureArg1
                                                , &HiSLIPConfigureArg2
}

  static const iocshFuncDef HiSLIPConfigureFuncDef =
  {"HiSLIPConfigure", 3, HiSLIPConfigureArgs};

static void HiSLIPConfigureCallFunc(const iocshArgBuf *args)
{
    HiSLIPConfigure (args[0].sval,
                     args[1].sval, args[2].ival)
}

/*
 * This routine is called before multitasking has started, so there's
 * no race condition in the test/set of firstTime.
 */
static void HiSLIPRegisterCommands (void)
{
    static int firstTime = 1;
    if (firstTime) {
        firstTime = 0;
        iocshRegister(&HiSLIPConfigureFuncDef, HiSLIPConfigureCallFunc);
    }
}
epicsExportRegistrar(HiSLIPRegisterCommands);
