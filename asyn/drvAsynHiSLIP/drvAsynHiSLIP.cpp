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

#define MAX_PAYLOAD_CAPACITY 4096
#define IDSTRING_CAPACITY        100

#define ASYN_REASON_SRQ 4345
#define ASYN_REASON_STB 4346
#define ASYN_REASON_REN 4347

// EPICS Async Support 
typedef struct drvPvt {
  /*
   * Matched device
   * HiSLIP private
  */
  
  HiSLIP_t  *device;
  char      *hostname;
  long      port;
  bool      isConnected;
  int       termChar;

  char      deviceVendorID[IDSTRING_CAPACITY];

  /*
   * Asyn interfaces we provide
   */
  char                  *portName;
  asynInterface          asynCommon;
  asynInterface          asynOctet;
  void                  *asynOctetInterruptPvt;
  asynInterface          asynDrvUser;
  
  /*
   * Interrupt through async_channel  handling
   */
  bool                   enableInterruptSRQ;
  char                  *interruptThreadName;
  epicsThreadId          interruptTid;
  epicsMutexId           interruptTidMutex;
  epicsEventId           pleaseTerminate;
  epicsEventId           didTerminate;
  epicsMessageQueueId    statusByteMessageQueue;
  
  /*
   * I/O buffer: Asyn clients may request data size smaller than 
   max_payload size.
   */
  unsigned char          *buf;// 
  size_t                 bufSize;
  size_t                 bufCount;
  const unsigned char    *bufp; //top of unread data buffer.

  /*
   * Statistics
   */

  size_t                 connectionCount;
  size_t                 interruptCount;
  size_t                 bytesSentCount;
  size_t                 bytesReceivedCount;
} drvPvt;

static asynStatus disconnect(void *pvt, asynUser *pasynUser);

// forward declarations.
static asynStatus
asynOctetFlush(void *pvt, asynUser *pasynUser);

/*
 * Interrupt endpoint support
 */
static void
interruptThread(void *arg)
{
    drvPvt *pdpvt = (drvPvt *)arg;
    int s;
    assert(pdpvt);
    if (pdpvt == NULL){
      errlogPrintf(" NULL drvPvt as an argument.\n");
    }
    
    while(true) {
      s =  pdpvt->device->wait_for_Async(60000); // wait for async-channel.

      if (s == 0){
	errlogPrintf("timeout poll for async channel.\n");
	continue;
      }
      if (s > 0 ){
	u_int8_t stb = pdpvt->device->get_Service_Request();
	errlogPrintf("Get SRQ with STB: 0x%2x\n", stb);
	if ((stb & 0x40) != 0){ // may need mask here. Just SRQ
	  ELLLIST *pclientList;
	  interruptNode *pnode;

	  pdpvt->interruptCount +=1;
	  pasynManager->interruptStart(pdpvt->asynOctetInterruptPvt,
				       &pclientList);
	  {
	    pnode = (interruptNode *)ellFirst(pclientList);
	    // see asynDriver/asynDriver.h
	    while (pnode) {
	      //int eomReason=0;
	      // 0:None, ASYN_EOM_CNT:0x0001,
	      //ASYN_EOM_EOS:0x0002 End of StrIng detected
	      //ASYN_EOM_END: 0x0004 End indicator detected
	      //size_t numchars=1;
	      asynOctetInterrupt *octetInterrupt =
		(asynOctetInterrupt *) pnode->drvPvt;
	      errlogPrintf("scan pnode in InterruptThread "
			   "pnode:%p reason:0x%x\n",
			   pnode, octetInterrupt->pasynUser->reason
			   );
	      if (octetInterrupt->pasynUser->reason == ASYN_REASON_SRQ) {
		octetInterrupt->callback(octetInterrupt->userPvt,
					 octetInterrupt->pasynUser,
					 (char *) &stb,
					 sizeof(stb),
					 ASYN_REASON_SRQ);
	      }
	      pnode = (interruptNode *)ellNext(&pnode->node);
	    }
	    pasynManager->interruptEnd(pdpvt->asynOctetInterruptPvt);
	    stb = pdpvt->device->status_query();
	    errlogPrintf("Finish SRQ process 0x%2x\n", stb);
	  }
	}
	continue;
      }
      else{
	errlogPrintf("srq poll return value:%d.\n",s);
      }
      if (epicsEventTryWait(pdpvt->pleaseTerminate) == epicsEventWaitOK){
	errlogPrintf("Terminate Interrupt Thread.\n");
	break;
      }
    }// while
    epicsMutexLock(pdpvt->interruptTidMutex);
    pdpvt->interruptTid = 0;
    epicsEventSignal(pdpvt->didTerminate);
    epicsMutexUnlock(pdpvt->interruptTidMutex);
}

static void
startInterruptThread(drvPvt *pdpvt)
{
    epicsMutexLock(pdpvt->interruptTidMutex);
    if (pdpvt->interruptTid == 0) {
        epicsEventTryWait(pdpvt->pleaseTerminate);
        epicsEventTryWait(pdpvt->didTerminate);
        pdpvt->interruptTid = epicsThreadCreate(pdpvt->interruptThreadName,
                                epicsThreadGetPrioritySelf(),
                                epicsThreadGetStackSize(epicsThreadStackSmall),
                                interruptThread,
				pdpvt);
        if (pdpvt->interruptTid == 0)
            errlogPrintf("----- WARNING ----- "
			 "Can't start interrupt handler thread %s.\n",
			 pdpvt->interruptThreadName);
    }
    epicsMutexUnlock(pdpvt->interruptTidMutex);
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
	//
	fprintf(fp, "HiSLIP device Info:\n");
	fprintf(fp, "\t" "%s: %lu\n", "maximum_message_size", pdpvt->device->maximum_message_size);
	fprintf(fp, "\t" "%s: %lu\n", "maximum_payload_size", pdpvt->device->maximum_payload_size);
	fprintf(fp, "\t" "%s: %x\n", "Terminator", pdpvt->termChar);
	fprintf(fp, "\t" "HiSLIP device address:%s\n",
		pdpvt->hostname);
	fprintf(fp, "\t" "overlap/sync. mode:%s\n",
		pdpvt->device->overlap_mode?"ovelap":"synchronized");
	fprintf(fp, "\t" "Session ID:%d\n",
		pdpvt->device->session_id);
	fprintf(fp, "\t" "server_protocol_version/server_vendorID : %#x %#x\n",
		pdpvt->device->server_protocol_version,
		pdpvt->device->server_vendorID);
	fprintf(fp, "\t" "sync/async socket number %d/%d\n",
		pdpvt->device->sync_channel,
		pdpvt->device->async_channel);
	fprintf(fp, "socket/locktime  %ld/%ld\n",
		pdpvt->device->socket_timeout,
		pdpvt->device->lock_timeout);
	fprintf(fp, "current message id/most recent message id %x/%x\n",
		pdpvt->device->message_id,pdpvt->device->most_recent_message_id);
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


static asynStatus
connect(void *pvt, asynUser *pasynUser)
{
  long rstatus;
  drvPvt *pdpvt = (drvPvt *)pvt;

  if (!pdpvt->isConnected || (pdpvt->device == NULL)) {
    if (pdpvt->device == NULL){
      pdpvt->device=new HiSLIP();
    }
    pdpvt->device->connect(pdpvt->hostname);
  }
  pdpvt->isConnected = true;
  pdpvt->connectionCount +=1;
  
  // setup private buffer for read.
  if (pdpvt->bufSize > 0){
    rstatus=pdpvt->device->set_max_size(pdpvt->bufSize);
  }
  else{
    rstatus=pdpvt->device->set_max_size(nsHiSLIP::MAXIMUM_MESSAGE_SIZE);
  }
  pdpvt->bufSize=pdpvt->device->maximum_payload_size;
  
  if (pdpvt->bufSize <=0){
    errlogPrintf("HiSLIP::connect invalid buffersize\n");
    return asynError;
  }
  
  pdpvt->buf=(unsigned char *)callocMustSucceed(
						1,
						pdpvt->bufSize+1,
						pdpvt->portName);
  //ensure buffer ends with a null character.
  *(pdpvt->buf + pdpvt->bufSize)=0x00;
  
  if(pdpvt->buf ==0){
    errlogPrintf("HiSLIP::failed to allocate input buffer\n");
    return asynError;
  }
  asynOctetFlush(pvt, pasynUser);
  // pdpvt->bufCount=0;
  // pdpvt->bufp=0; // NO data in buf.
  pasynManager->exceptionConnect(pasynUser);
  return asynSuccess;
}

static asynStatus disconnect(void *pvt, asynUser *pasynUser)
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
                errlogPrintf
		  ("----- WARNING ----- Thread %s won't terminate!\n",
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
    pdpvt->isConnected = false;
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
    errlogPrintf("OctetWrite %s %ld %ld\n",data,numchars,*nbytesTransfered);
    /*
     * Send
     */
    *nbytesTransfered = 0;
    while (numchars) {
      size_t nSent;

      nSent = pdpvt->device->write((u_int8_t *) data, numchars, timeout);
	
        if (nSent < 0) {
            disconnectIfGone(pdpvt, pasynUser, 0);
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                                "data transfer failed: %s",
			  "Error in write operation");
            return asynError;
        }
        if (nSent != numchars) {
            epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
			  "Asked to send %lu, actually sent %lu", numchars,nSent);
            return asynError;
        }
        data += nSent;
        numchars -= nSent;
        *nbytesTransfered += nSent;
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
    size_t ioCount, nCopy;
    int status;
    int eom=0;
    size_t nout=0;
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;
    
    assert(pdpvt->device);
    assert(nbytesTransfered);
    
    if(eomReason) {*eomReason=eom;}
    if(nbytesTransfered) {*nbytesTransfered = nout = 0;}

    // errlogPrintf("asynOctetRead timeout:%g host:%s maxchars:%ld bufCount:%ld reason:%x\n",
    // 		 pasynUser->timeout,
    // 		 pdpvt->hostname,
    // 		 maxchars, pdpvt->bufCount,
    // 		 pasynUser->reason);
    // /*
    //  * Special case for stream device which requires an asynTimeout return.
    //  */
    while(eom==0){
      if ((pasynUser->timeout == 0) && (pdpvt->bufCount == 0))
	return asynTimeout;
      if (pdpvt->bufCount ==0) {
	// read adittional data from device
	status = pdpvt->device->read( &ioCount,
				    (u_int8_t *) pdpvt->buf,
				    pdpvt->bufSize,
				    timeout);
	epicsSnprintf(pasynUser->errorMessage,
			pasynUser->errorMessageSize,
		      "HiSLIP-asynOctetRead"
		      " ioCount: %ld, buffer: %p, bffersize:%lu, timeout:%d status:%d\n",
		      ioCount, pdpvt->buf, pdpvt->bufSize, timeout, status);
	
	if (status != 0) {
	  disconnectIfGone(pdpvt, pasynUser, 0);
	  epicsSnprintf(pasynUser->errorMessage,
			pasynUser->errorMessageSize,
			"Data transfer request failed: %s %d",
			"Error in read operation",status);
	  return asynError;
	}
	assert(ioCount);
	pdpvt->bufp=pdpvt->buf;
	pdpvt->bufCount = ioCount;
	pdpvt->bytesReceivedCount += ioCount;
	epicsSnprintf(pasynUser->errorMessage,
		      pasynUser->errorMessageSize,
		      "HiSLIP-asynOctetRead data:%s, "
		      "maxchars:%ld, nbytesTransfered:%ld, eomReason:%d\n",
		      data, maxchars, nout, eom);
      }
      if (pdpvt->bufCount) {
	if (maxchars > pdpvt->bufCount){
	  nCopy = pdpvt->bufCount;
	}
	else{
	  nCopy = maxchars;
	}	
	memcpy(data, pdpvt->bufp, nCopy);
	pdpvt->bufCount -= nCopy;
	if(pdpvt->bufCount<0){
	  errlogPrintf("woo. something wrong");
	  exit(-1);
	}
	if (pdpvt->bufCount == 0){
	  pdpvt->bufp = 0;
	}
	else{
	  pdpvt->bufp += nCopy;
	}
	maxchars -= nCopy;
	nout += nCopy;
	pdpvt->bytesReceivedCount += nCopy;
	data += nCopy;
	if (maxchars == 0){
	  eom |= ASYN_EOM_CNT;
	}
	if(pdpvt->bufCount == 0 ){
	  if (pdpvt->device->rmt_delivered){
	    eom |=ASYN_EOM_END;
	  }
	}
      }
    }
    if(eomReason) *eomReason = eom;
    if(nbytesTransfered) {*nbytesTransfered = nout;}
    
    return asynSuccess;
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

    errlogPrintf("OctetFlush:\n");
    pdpvt->bufCount = 0;
    pdpvt->bufp = 0;
    return asynSuccess;
}

static asynStatus
asynOctetSetInputEos(void *pvt, asynUser *pasynUser, const char *eos, int eoslen)
{
    drvPvt *pdpvt = (drvPvt *)pvt;

    if (eoslen == 0) {
      pdpvt->termChar = -1;
      return asynSuccess;
    }
    else if (eoslen == 1) {
      pdpvt->termChar = *eos & 0xff;
      return asynSuccess;
    }
    else{
      pdpvt->termChar = -1;
      epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Device does not support multiple terminating characters %s %d",
		    eos, eoslen
		    );
      return asynError;
    }
}

static asynStatus
asynOctetGetInputEos(void *pvt, asynUser *pasynUser, char *eos, int eossize, int *eoslen)
{
    *eoslen = 0;
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

    errlogPrintf("DrvUserCreate  drvinfo:%s\n",
		 drvInfo);
  
    if (epicsStrCaseCmp(drvInfo, "SRQ") == 0) {
        pasynUser->reason = ASYN_REASON_SRQ;
	pdpvt->enableInterruptSRQ = true;
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
		const int messagesize,
                int priority)
{
    drvPvt *pdpvt;
    asynStatus status;
    int flags=0;
    
    /*
     * Set up local storage
     */
    pdpvt = (drvPvt *)callocMustSucceed(1, sizeof(drvPvt), portName);
    pdpvt->portName = epicsStrDup(portName);
    pdpvt->interruptThreadName = (char *) callocMustSucceed(1,
							    strlen(portName)+5,
							    portName);
    epicsSnprintf(pdpvt->interruptThreadName,
		  sizeof pdpvt->interruptThreadName,
		  "%sIntr", portName);
    if (priority == 0) priority = epicsThreadPriorityMedium;
    
    pdpvt->hostname=(char *)callocMustSucceed(1,
					      strlen(hostInfo)+1,
					      "HiSLIPConfigure");
    strcpy(pdpvt->hostname, hostInfo);

    pdpvt->bufSize=messagesize;
    
    pdpvt->termChar = 0;
    pdpvt->interruptTidMutex = epicsMutexMustCreate();
    pdpvt->pleaseTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->didTerminate = epicsEventMustCreate(epicsEventEmpty);
    pdpvt->statusByteMessageQueue = epicsMessageQueueCreate(1, 1);

    if (!pdpvt->statusByteMessageQueue) {
        errlogPrintf("Can't create message queue!\n");
        return;
    }

    /*
     * Create our port
     */
    errlogPrintf("* registerPort to asynManager \n");
    status = pasynManager->registerPort(pdpvt->portName,
                                        ASYN_CANBLOCK,
                                        (flags & 0x1) == 0,
                                        priority, 0);
    if(status != asynSuccess) {
      errlogPrintf("registerPort failed portname %s\n",
		   pdpvt->portName);
      return;
    }

    /*
     * Register ASYN interfaces
     */
    errlogPrintf("* Register ASYN interfaces \n");
    
    pdpvt->asynCommon.interfaceType = asynCommonType;
    pdpvt->asynCommon.pinterface  = &commonMethods;
    pdpvt->asynCommon.drvPvt = pdpvt;

    status = pasynManager->registerInterface(pdpvt->portName, &pdpvt->asynCommon);
    
    if (status != asynSuccess) {
        errlogPrintf("registerInterface failed\n");
        return;
    }

    errlogPrintf("* Register ASYN Octet interfaces \n");
    pdpvt->asynOctet.interfaceType = asynOctetType;
    pdpvt->asynOctet.pinterface  = &octetMethods;
    pdpvt->asynOctet.drvPvt = pdpvt;
    status = pasynOctetBase->initialize(pdpvt->portName,
					&pdpvt->asynOctet,
					0,
					0,
					0);
    if (status != asynSuccess) {
        errlogPrintf("pasynOctetBase->initialize failed\n");
        return;
    }

    /*
     * Always register an interrupt source, just in case we use SRQs
     */
    errlogPrintf("* Register Interrupt source \n");
    pasynManager->registerInterruptSource(pdpvt->portName,
                                         &pdpvt->asynOctet,
                                         &pdpvt->asynOctetInterruptPvt);

    pdpvt->asynDrvUser.interfaceType = asynDrvUserType;
    pdpvt->asynDrvUser.pinterface = &drvUserMethods;
    pdpvt->asynDrvUser.drvPvt = pdpvt;
    status = pasynManager->registerInterface(pdpvt->portName,
					     &pdpvt->asynDrvUser);
    if (status != asynSuccess) {
        errlogPrintf("Can't register drvUser\n");
        return;
    }
}

/*
 * IOC shell command registration
 */
static const iocshArg HiSLIPConfigureArg0 = {"port name",
					     iocshArgString};
static const iocshArg HiSLIPConfigureArg1 = {"HiSLIP host address",
					     iocshArgString};
static const iocshArg HiSLIPConfigureArg2 = {"MessageSize",
					     iocshArgInt};
static const iocshArg HiSLIPConfigureArg3 = {"priority",
					     iocshArgInt};
static const iocshArg *HiSLIPConfigureArgs[] = {
						&HiSLIPConfigureArg0
                                                , &HiSLIPConfigureArg1
                                                , &HiSLIPConfigureArg2
						, &HiSLIPConfigureArg3
};
 
static const iocshFuncDef HiSLIPConfigureFuncDef =
  {"HiSLIPConfigure", 4, HiSLIPConfigureArgs};

static void HiSLIPConfigureCallFunc(const iocshArgBuf *args)
{
    HiSLIPConfigure (args[0].sval,
                     args[1].sval,
		     args[2].ival,		     
		     args[3].ival);
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
