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

// HiSLIP methods
namespace nsHiSLIP{
  void HiSLIP::connect(const char *hostname,
		       const char *dev_name,
		       const int port //,
		       //const char vendor_id[2]
		       ){
    osiSockAddr addr;
    //Message_t *msg;
    int status;
    //const char vendor_id[]=Default_vendor_id;
  
    errlogPrintf("connecting to host \"%s\"\n", hostname);
  
    if(hostToIPAddr(hostname, &addr.ia.sin_addr) < 0) {
      errlogPrintf("Unknown host \"%s\"", hostname);
    }
  
    addr.ia.sin_port=htons(port);
    addr.ia.sin_family=AF_INET;
  
    errlogPrintf("address : %x %x %x\n",addr.ia.sin_addr.s_addr,addr.ia.sin_port,
		 addr.ia.sin_family);

    //this->sync_channel= ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    //this->async_channel=::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    this->sync_channel= ::socket(AF_INET, SOCK_STREAM , 0);
    this->async_channel=::socket(AF_INET, SOCK_STREAM , 0);

    errlogPrintf("socceks created %d %d\n",this->sync_channel, this->async_channel);
  
    errlogPrintf("address : %x %x %x\n",addr.ia.sin_addr.s_addr,addr.ia.sin_port,
		 addr.ia.sin_family);

    status = ::connect(this->sync_channel, &(addr.sa), sizeof(addr));
  
    errlogPrintf("connected to sync channel %d\n",this->sync_channel);
  
    if (status!=0){
      // Error handling
      perror(__FUNCTION__);
      errlogPrintf("Error !! %d\n",status);
    }

    {
      Message msg(nsHiSLIP::Initialize,
		  0,
		  message_parameter((epicsUInt16) nsHiSLIP::PROTOCOL_VERSION_MAX,
				    (char *) Default_vendor_id),
		  (epicsUInt64) 0, (epicsUInt8 *) NULL);
      errlogPrintf("sending message %d \n", msg.message_type);
      msg.send(this->sync_channel);
    }
  
    errlogPrintf("Sent a initialize message\n");

    { Message resp(AnyMessages);

      errlogPrintf("Receive message created\n");
    
      resp.recv(this->sync_channel, nsHiSLIP::InitializeResponse);
    
      this->overlap_mode=resp.control_code;
      this->session_id=resp.message_parameter.getSessionId();
      this->server_protocol_version=resp.message_parameter.getServerProtocolVersion();
    }
    errlogPrintf("Receive a initialized message %d 0x%x %d \n",
		 this->overlap_mode,this->session_id, this->server_protocol_version
		 );
  
    errlogPrintf("Sending Async initialize message %x\n",this->session_id);
  
    status = ::connect(this->async_channel, &addr.sa, sizeof(addr));
    if (status!=0){
      // Error handling
      perror(__FUNCTION__);
      errlogPrintf("Error !!:%d\n",status);
    }
    errlogPrintf("connected to async channel %d\n",this->async_channel);
  
    {
      Message msg(nsHiSLIP::AsyncInitialize);
      msg.message_parameter.word=this->session_id;
      msg.send(this->async_channel);
    }
    errlogPrintf("reading Async initialize response\n");
  
    {
      Message resp(AnyMessages);
      resp.recv(this->async_channel, nsHiSLIP::AsyncInitializeResponse);
      this->overlap_mode=resp.control_code;
      this->server_vendorID=resp.message_parameter.word;
    }
    errlogPrintf("reading Async initialize done\n");
  
    //now setup poll object
    this->reset_message_id();
  
    this->sync_poll.fd=this->sync_channel;
    this->sync_poll.events=POLLIN;
    this->sync_poll.revents=0;

    this->async_poll.fd=this->async_channel;
    this->async_poll.events=POLLIN;
    this->async_poll.revents=0;

    errlogPrintf("Receive a Async initialized message\n");    
  };

  long HiSLIP::set_max_size(long message_size){
    Message resp(AnyMessages);
    epicsUInt64 msg_size=htobe64(message_size);

    Message msg=Message(nsHiSLIP::AsyncMaximumMessageSize,
			0,
			message_parameter(0),
			sizeof(msg_size), (epicsUInt8 *) &msg_size);
    msg.send(this->async_channel);
  
    int ready=poll(&this->async_poll,  1, this->socket_timeout*1000);
    if ( ready == 0){
      return -1;
    }
  
    resp.recv(this->async_channel,nsHiSLIP::AsyncMaximumMessageSizeResponse);
  
    this->maximum_message_size=*((epicsUInt64 *)(resp.payload));
    this->maximum_payload_size = this->maximum_message_size - HEADER_SIZE;
    return this->maximum_message_size;
  };

  int HiSLIP::device_clear(){
    Message resp(AnyMessages);
    epicsUInt8 feature_preference;
    int ready;
  
    Message *msg=new Message(nsHiSLIP::AsyncDeviceClear,
			     0,
			     0,
			     0,NULL);

    msg->send(this->async_channel);

    ready=poll(&this->async_poll,  1, this->socket_timeout*1000);
    if ( ready == 0){
      return -1;
    }
  
    resp.recv(this->async_channel,nsHiSLIP::AsyncDeviceClearAcknowledge);
    feature_preference=resp.control_code;

    msg=new Message(nsHiSLIP::DeviceClearComplete,
		    feature_preference,
		    0,
		    0, NULL);

    msg->send(this->sync_channel);
  
    ready=poll(&this->sync_poll,  1, this->socket_timeout*1000);
    if ( ready == 0){
      return asynTimeout;
    }
    resp.recv(this->sync_channel,nsHiSLIP::DeviceClearAcknowledge);
  
    this->overlap_mode=resp.control_code;
    this->reset_message_id();
    this->rmt_delivered = false;
  
    return 0;
  
  };

  epicsUInt8 HiSLIP::status_query(){
    epicsUInt8 status;
    int ready;
  
    Message resp(AnyMessages);
  
    Message msg((epicsUInt8) nsHiSLIP::AsyncStatusQuery,
		(epicsUInt8) this->rmt_delivered,
		message_parameter((epicsUInt32) this->most_recent_message_id),
		0, NULL);
    msg.send(this->async_channel);

    ready=poll(&this->async_poll,  1, this->socket_timeout*1000);
    if ( ready == 0){
      return -1;
    }
    resp.recv(this->async_channel,nsHiSLIP::AsyncStatusResponse);
  
    status= resp.control_code &0xff;
  
    return status;
  }


  // long HiSLIP::write(epicsUInt8 *data_str, long timeout){
  //   return this->write(data_str, this->maximum_message_size,timeout);
  // };

  long HiSLIP::write(epicsUInt8 const* data_str, size_t const dsize, long timeout){
  
    size_t max_payload_size = this->maximum_message_size - nsHiSLIP::HEADER_SIZE;
    size_t bytestosend=dsize;
    const epicsUInt8 *buffer=data_str;
    size_t delivered=0;
    size_t count;

    errlogPrintf("HiSLIP::write sending data %s\n", data_str);
  
    while(bytestosend){
      if (bytestosend < max_payload_size){
	Message msg(nsHiSLIP::DataEnd,
		    this->rmt_delivered,
		    nsHiSLIP::message_parameter(this->message_id),
		    bytestosend, (epicsUInt8 *) buffer);
	buffer += bytestosend;
	errlogPrintf("sending message %s\n",(char *) msg.payload);
	count=msg.send(this->sync_channel);
	count -=HEADER_SIZE;
	bytestosend = 0;
	delivered += count ;
      }
      else{
	Message msg(nsHiSLIP::Data,
		    this->rmt_delivered,
		    nsHiSLIP::message_parameter(this->message_id),
		    max_payload_size, (epicsUInt8 *) buffer);
	count=msg.send(this->sync_channel);
	count -= HEADER_SIZE;
	bytestosend -=count;
	delivered += count;
	buffer += max_payload_size;
      }
      errlogPrintf("data sent= %lu\n",count);
      this->increment_message_id();
    }
    return delivered;
  };
  
  int HiSLIP::read(size_t *received, epicsUInt8 **buffer, long timeout){
    bool eom=false;
    size_t rsize=0;
    
    errlogPrintf("entered to HiSLIP::read(**buffer:%p, timeout:%ld)\n",
		 buffer, timeout);

    *received=0;
    this->rmt_delivered = false;

    while(!eom) {
      int ready;
      Message resp(AnyMessages);
      
      ready=poll(&this->sync_poll,  1, this->socket_timeout*1000);
      if ( ready == 0){
	return -1;
      }
      rsize=resp.recv(this->sync_channel);
      errlogPrintf("HiSLIP read rsize %ld\n",rsize);
      if (rsize < resp.payload_length){
	//Error!!
	return -1;
      };
      
      // may not be a good idea.
      {	epicsUInt8 *newbuf;
	
	newbuf=(epicsUInt8 *) reallocarray(*buffer, 1,
				   *received+resp.payload_length);
	if (newbuf == NULL){
	  errlogPrintf("Cannot extend memory area\n");
	  return -1;
	}
	else{
	  *buffer=newbuf;
	}
      }
      ::memcpy((*buffer + *received), resp.payload, resp.payload_length);
      *received +=resp.payload_length;
      if ( resp.message_type == nsHiSLIP::Data){
	continue;
      } else if ( resp.message_type == nsHiSLIP::DataEnd){
	eom=true;
	this->rmt_delivered=true;
	return 0;
      } else{
	// error unexpected message type.
	return -1;
      }
    }
    return -1;
  };
  
  int HiSLIP::read(size_t *received,
		   epicsUInt8 *buffer, size_t bsize, long timeout){
    bool eom=false;
    size_t rsize=0;

    errlogPrintf("entered to HiSLIP::read(buffer %p, bsize:%ld, timeout:%ld\n",
		 buffer, bsize, timeout);
    *received=0;
    this->rmt_delivered = false;
    
    if (buffer==NULL || bsize <= 0){
      errlogPrintf("exit HiSLIP::read improper input buffer:%p bsize:%lu, timeout:%ld\n",
		   buffer, bsize, timeout);
      return -1;
    }
    if (bsize < this->maximum_payload_size){
      errlogPrintf("exit HiSLIP::buffer size:%ld should be larger than maximum playload size:%ld \n",
		   bsize, this->maximum_payload_size);
    }
    while(!eom) {
      int ready;
      Message resp(AnyMessages);

      ready=::poll(&this->sync_poll, 1, timeout);
      
      if (ready == 0){
	errlogPrintf("HiSLIP::read read timeout %d %ld \n", ready, lock_timeout);
	return -1;
      }
      
      rsize=resp.recv(this->sync_channel);

      if (rsize < resp.payload_length){
	errlogPrintf("read data too short %ld %qd \n", rsize, resp.payload_length);
	return -1;
      };
      if (( (*received) + resp.payload_length) > bsize){
	errlogPrintf("not enough space to store received:%ld resp.payload:%qd bsize:%ld\n",
		     *received, resp.payload_length, bsize);
	
	::memcpy( (buffer + *received), resp.payload, (bsize - *received));
	*received = bsize;
	return 0;
      }
      else{
	errlogPrintf("received message size %ld %ld  data:%s mt:%d\n",
		     rsize, *received, (char *) resp.payload, resp.message_type);
	::memcpy( (buffer + *received), resp.payload, resp.payload_length);
      
	*received +=resp.payload_length;
      }
      
      if ( resp.message_type == nsHiSLIP::Data){
	continue;
      } else if (resp.message_type == nsHiSLIP::DataEnd){
	eom=true;
	this->rmt_delivered=true;
	errlogPrintf("received message: %s %s ,eom:%d rmt:%d\n",
		     buffer, (char *) resp.payload, eom,this->rmt_delivered);
	return 0;
      } else{
	errlogPrintf("Unexpected message type:%d\n",
		     resp.message_type);
	resp.printf();
	// error unexpected message type.
	return -1;
      }
    }
    return -1;
  };

  size_t HiSLIP::ask(epicsUInt8  *const data_str, size_t const dsize,
		     epicsUInt8 **rbuffer,
		     long wait_time){
    size_t rsize=-1;
    epicsUInt8 *buffer=NULL;
    int status;
    
    errlogPrintf("sending a command %s %lu",data_str, dsize);
  
    this->write(data_str, dsize);
    if(this->wait_for_answer(wait_time) == 0){
      // error
      return -1;
    };
    status=this->read(&rsize, &buffer);
    if (status !=0){
      rsize=-1;
    }
    *rbuffer=buffer;
    return rsize;
  };
 
  long HiSLIP::trigger_message(void){
    return 0;
  };
  long HiSLIP::remote_local(bool request){
    return 0;
  };
  long HiSLIP::request_lock(const char* lock_string){
    return 0;
  };
  long HiSLIP::release_lock(void){
    return 0;
  };
  long HiSLIP::request_srq_lock(void){
    return 0;
  };
  long HiSLIP::release_srq_lock(void){
    return 0;
  };

} // end of namespace HiSLIP
//
// EPICS Async Support 
typedef struct drvPvt {
  /*
   * Matched device
   * HiSLIP private
  */
  
  HiSLIP_t *device;
  char      *hostname;
  long      port;
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
  asynInterface          asynDrvUser;
  
  /*
   * Interrupt through async_channel  handling
   */
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
    
    for (;;) {
      s =  pdpvt->device->wait_for_SRQ(65000); 
      
      if (epicsEventTryWait(pdpvt->pleaseTerminate) == epicsEventWaitOK)
	break;

      Message_t *srqmsg=pdpvt->device->get_Service_Request();
      
      epicsUInt8 st =srqmsg->control_code;
      
      if (s != 0 ){
	if (st != 0){
	  ELLLIST *pclientList;
	  interruptNode *pnode;

	  pdpvt->interruptCount++;
	  pasynManager->interruptStart(pdpvt->asynOctetInterruptPvt,
				       &pclientList);
	  {
	    pnode = (interruptNode *)ellFirst(pclientList);
	    // see asynDriver/asynDriver.h
	    while (pnode) {
	      int eomReason=0; // 0:None, ASYN_EOM_CNT:0x0001,ASYN_EOM_EOS:0x0002 End of StrIng detected ASYN_EOM_END: 0x0004 End indicator detected

	      size_t numchars=1;
	      asynOctetInterrupt *octetInterrupt =
		(asynOctetInterrupt *) pnode->drvPvt;
	      pnode = (interruptNode *)ellNext(&pnode->node);
	      if (octetInterrupt->pasynUser->reason == ASYN_REASON_SRQ) {
		octetInterrupt->callback(octetInterrupt->userPvt,
					 octetInterrupt->pasynUser,
					 (char *) &st,
					 numchars,
					 eomReason);
	      }
	    }
	    pasynManager->interruptEnd(pdpvt->asynOctetInterruptPvt);
	  }
	}
	else{
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
	fprintf(fp, "%s: %x\n", "Terminator", pdpvt->termChar);
	fprintf(fp, "HiSLIP device address:%s\n",
		pdpvt->hostname);
	fprintf(fp, "overlap/sync. mode:%s\n",
		pdpvt->device->overlap_mode?"ovelap":"synchronized");
	fprintf(fp, "Session ID:%d\n",
		pdpvt->device->session_id);
	fprintf(fp, "server_protocol_version/server_vendorID : %#x %#x\n",
		pdpvt->device->server_protocol_version,
		pdpvt->device->server_vendorID);
	fprintf(fp, "sync/async socket number %d/%d\n",
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
  pdpvt->bufSize=pdpvt->device->maximum_payload_size;
  
  if (pdpvt->bufSize <=0){
    errlogPrintf("HiSLIP::connect invalid buffersize\n");
    return asynError;
  }
  
  pdpvt->buf=(unsigned char *)callocMustSucceed(1,
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

      nSent = pdpvt->device->write((epicsUInt8 *) data, numchars, timeout);
	
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
    int timeout = pasynUser->timeout * 1000;
    if (timeout == 0) timeout = 1;
    
    assert(pdpvt->device);
    assert(nbytesTransfered);
    
    *nbytesTransfered = 0;
    if(eomReason) *eomReason=0;
    
    errlogPrintf("asynOctetRead timeout:%g host:%s maxchars:%ld bufCount:%ld\n",
		 pasynUser->timeout,
		 pdpvt->hostname,
		 maxchars, pdpvt->bufCount);
    /*
     * Special case for stream device which requires an asynTimeout return.
     */
    while(eom==0){
      if ((pasynUser->timeout == 0) && (pdpvt->bufCount == 0))
	return asynTimeout;
      if (pdpvt->bufCount ==0) {
	// read adittional data from device
	status = pdpvt->device->read( &ioCount,
				    (epicsUInt8 *) pdpvt->buf,
				    pdpvt->bufSize,
				    timeout);
	if (status != 0){
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
	errlogPrintf("asynOctetRead data:%s, maxchars:%ld,nbytesTransfered:%ld, eomReason:%d\n",
		     data, maxchars, *nbytesTransfered, eom);
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
	*nbytesTransfered += nCopy;
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

    if (eoslen ==0) {
      pdpvt->termChar = -1;
      return asynSuccess;
    }
    else{
      pdpvt->termChar = -1;
      epicsSnprintf(pasynUser->errorMessage, pasynUser->errorMessageSize,
                    "Device does not support multiple terminating characters");
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

    errlogPrintf("DrvUserCreate  drvinfo:%s typeName:%s size:%ld\n",
		 drvInfo, *pptypeName, *psize
		 );
  
    if (epicsStrCaseCmp(drvInfo, "SRQ") == 0) {
        pasynUser->reason = ASYN_REASON_SRQ;
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
    
    // errlogPrintf("creating HiSLIP object %s", hostInfo);
    // pdpvt->device=new HiSLIP_t();
    // errlogPrintf("connecting  HiSLIP object to host:%s\n", hostInfo);
    // pdpvt->device->connect(hostInfo);
    // pdpvt->isConnected = true;
    // errlogPrintf("HiSLIP object connected to host:%s\n", hostInfo);
    // if (pdpvt->device == 0) {
    //   errlogPrintf("Failed to create HiSLIP device for %s(%s)\n",
    //  hostInfo, nsHiSLIP::Error_Messages[2]);
    //     return;
    // }
    
    pdpvt->termChar = -1;
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
static const iocshArg HiSLIPConfigureArg1 = {"HiSLIP host addressr",
					     iocshArgString};
static const iocshArg HiSLIPConfigureArg2 = {"priority",
					     iocshArgInt};
static const iocshArg *HiSLIPConfigureArgs[] = {
						&HiSLIPConfigureArg0
                                                , &HiSLIPConfigureArg1
                                                , &HiSLIPConfigureArg2
};
 
static const iocshFuncDef HiSLIPConfigureFuncDef =
  {"HiSLIPConfigure", 3, HiSLIPConfigureArgs};

static void HiSLIPConfigureCallFunc(const iocshArgBuf *args)
{
    HiSLIPConfigure (args[0].sval,
                     args[1].sval, args[2].ival);
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
