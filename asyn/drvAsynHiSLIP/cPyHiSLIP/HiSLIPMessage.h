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
//-*- coding:utf-8 -*-
#define NDEBUG 1
#define DEBUG 1

#include <assert.h>
#include <errno.h>
//#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <sys/types.h>
#include <string.h> //memcpy, etc
#include <sys/param.h> /* for MAXHOSTNAMELEN */
#include <unistd.h> /* close() and others */

#include <poll.h>
#include <endian.h> // network endian is "be".
#include <sys/socket.h>
#include <netdb.h>

//#include <sys/time.h>
//#include <arpa/inet.h>
//#include <sys/ioctl.h>
//#include <netinet/in.h>
//#include <netinet/tcp.h>
//#include <net/if.h>



template<class T> struct Property {
  T& r;
  operator T() {return r;}
  void operator =(const T v){ r=v;}
};
   
namespace nsHiSLIP{
  
  //constants
  typedef enum CC_reuqest{
			  RemoteDisable=0,
			  RemoteEnable=1,
			  RemoteDisableGTL=2, // disable remote  and goto local
			  RemoteEnableGTR=3, // Enable remote and goto remote
			  RemoteEnableLLO=4, // Enable remote and lock out local
			  RemoteEnableGTRLLO=5, //
			  RTL=6
  } CC_request_t;
  
  typedef enum CC_Lock{
		       release =0,
		       request =1
  } CC_Lock_t;

  typedef enum CC_LockResponse{
			       fail=0,         //Lock was requested but not granted
			       success=1,      //release of exclusive lock
			       success_shared=2, //release of shared lock
			       error=3 // Invalide
  } CC_LockResponse_t;

  static const long  PROTOCOL_VERSION_MAX = 257 ; // # <major><minor> = <1><1> that is 257
  static const long  INITIAL_MESSAGE_ID = 0xffffff00 ;
  static const long  UNKNOWN_MESSAGE_ID  = 0xffffffff ;
  static const long  MAXIMUM_MESSAGE_SIZE_VISA = 272;//Following VISA 256 bytes + header length 16 bytes
  static const long  MAXIMUM_MESSAGE_SIZE= 4096;//R&S accept 
  static const long  HEADER_SIZE=16;
  static const long  SOCKET_TIMEOUT = 1000; //# Socket timeout in msec
  static const long  LOCK_TIMEOUT = 3000;//# Lock timeout
  static const long  Default_Port = 4880;
  static const char  Default_device_name[]="hslip0";
  static const char  Default_vendor_id[]={'E','P'};
  static const char  Prologue[]={'H','S'};
  //
  typedef enum Message_Types{
			     Initialize = 0,
			     InitializeResponse = 1,
			     FatalError = 2,
			     Error = 3,
			     AsyncLock = 4,
			     AsyncLockResponse = 5,
			     Data = 6,
			     DataEnd = 7,
			     DeviceClearComplete = 8,
			     DeviceClearAcknowledge = 9,
			     AsyncRemoteLocalControl = 10,
			     AsyncRemoteLocalResponse = 11,
			     Trigger = 12,
			     Interrupted = 13,
			     AsyncInterrupted = 14,
			     AsyncMaximumMessageSize = 15,
			     AsyncMaximumMessageSizeResponse = 16,
			     AsyncInitialize = 17,
			     AsyncInitializeResponse = 18,
			     AsyncDeviceClear = 19,
			     AsyncServiceRequest = 20,
			     AsyncStatusQuery = 21,
			     AsyncStatusResponse = 22,
			     AsyncDeviceClearAcknowledge = 23,
			     AsyncLockInfo = 24,
			     AsyncLockInfoResponse = 25,
			     // 26-127 are reserved for future use.
			     // I don't watn to use negative value to represent ANY message. So I picked 127 from reserved values for this purpose.
			     AnyMessages=127 // 128-255 are reserved for vendor use.
  } Message_Types_t;

  typedef enum Error_code{
			 UnidentifiedError,
			 UnrecognizedMessageType,
			 UnrecognizedControlCode,
			 UnrecognizedVendorDefinedMessage,
			 MessageTooLarge
  } Error_code_t;
  static const char *Error_Messages[] =
    {
     "Unidentified error",
     "Unrecognized Message Type",
     "Unrecognized control code",
     "Unrecognized Vendor Defined Message",
     "Message too large"
    };
  typedef enum Fatal_Error_code {
				 UnidentifiedFatalError,
				 PoorlyFormedMmessageHeader,
				 AttemptToUseConnectionWithoutBothChannels,
				 InvalidInitializationSequence,
				 ServerRefusedConnection
  } Fatal_Erro_code_t;
  static const char *Fatal_Error_Messages[] =
    {
     "Unidentified error",
     "Poorly formed message header",
     "Attempt to use connection without both channels established",
     "Invalid Initialization Sequence",
     "Server refused connection due to maximum number of clients exceeded"
  };

  typedef class message_parameter{
  public:
    u_int32_t word;
    // struct InitializeParameter{
    //   u_int16_t  protocol_version;
    //   char vendor_id[2]={0x00 ,0x00};
    // } initParm;
    // struct InitializeResponseParameter{
    //   u_int16_t  session_id;
    //   u_int16_t  protocol_version;
    // }  initResp;
    message_parameter(u_int32_t word){this->word=word;};
    message_parameter(u_int16_t proto, char vers[2]){
      this->word= ((int32_t)proto << 16) +
	(vers[1]  << 8) + (vers[0]<<0);
      // errlogPrintf("messagParameter:%#010x, protocol:%#06x, version:[%c%c]\n",
      // 		   this->word,
      // 		   proto, vers[0], vers[1]);
    };
    message_parameter(u_int16_t proto, u_int16_t sid){
      this->word = (proto << 16) + sid;
      // errlogPrintf("messagParameter:%#010x, protocol:%#06x, session id:%#06x\n",
      // 		   this->word,
      // 		   proto, sid);
    };
    u_int16_t getServerProtocolVersion(){
      // errlogPrintf("getServerProtocolVersion messagParameter:%#010x, ServerProtocolVersion %d\n",
      // 		   this->word,
      // 		   (u_int16_t) (((this->word) & 0xffff0000)>>16));
      return (u_int16_t) (((this->word) & 0xffff0000)>>16);
    }
    u_int16_t getSessionId(){
      // errlogPrintf("getSessionId messagParameter %#010x, sessionID:%d\n",
      // 		   this->word,
      // 		   (u_int16_t) ((this->word) & 0xffff));
      return ((u_int16_t) ((this->word) & 0xffff));
    }
  } message_parameter_t;

  class Header{
  public:
    const char prologue[2]={'H','S'};
    u_int8_t  message_type;
    u_int8_t  control_code;
    message_parameter_t message_parameter;
    u_int64_t payload_length;
    
    Header(Message_Types_t message_type):control_code(0),message_parameter(0),payload_length(0){
      this->message_type=message_type;
    }
    Header(const void *buffer):message_parameter(0){
      assert(memcmp(this->prologue, buffer,2) == 0);
      this->message_type=*((u_int8_t *) ((char *) buffer+2));
      this->control_code=*((u_int8_t *) ((char *) buffer+3));
      this->message_parameter.word = be32toh(*(u_int32_t *) ((char *) buffer+4));
      this->payload_length = be64toh(*(u_int64_t *) ((char *) buffer+8));
    }
    Header(const u_int8_t  type,
	   const u_int8_t  cce,
	   const message_parameter_t param,
	   const u_int64_t length):message_parameter(param.word){
      this->message_type=type;
      this->control_code=cce;
      this->payload_length=length;
    }
    void printf(void){
      // errlogPrintf("message type:%d\n",this->message_type);
      // errlogPrintf("control_code:%d\n",this->control_code);
      // errlogPrintf("message_parameter: 0x%0x\n",this->message_parameter.word);
      // errlogPrintf("payload length: %qd\n",this->payload_length);
    }
    
    size_t send(int socket){
      char hbuf[HEADER_SIZE];
      ssize_t ssize;
      
      this->toRawData(hbuf);
      // errlogPrintf("sending header dump: %#016qx,%#016qx\n",
      // 		   *((u_int64_t*)hbuf),*(((u_int64_t*)hbuf) +1));      
      ssize=::send(socket, hbuf, sizeof(hbuf), 0);
      
      return ssize;
    }
    
    size_t recv(int socket, Message_Types_t expected_message_type = AnyMessages){
      

      char buffer[HEADER_SIZE];
      ssize_t rsize;
      
      
      // errlogPrintf("recv header\n");
      
      rsize= ::recv(socket, buffer, HEADER_SIZE, 0);
      
      // errlogPrintf("read header dump: %#016qx, %#016qx\n",
      // 		   *((u_int64_t*)buffer),
      // 		   *(((u_int64_t*)buffer) +1) );
      
      if (rsize < HEADER_SIZE){
	//raise exception?
	// errlogPrintf("too short header %ld\n",rsize);
	return -1;
      }
      else if (memcmp(this->prologue, buffer,2) != 0){
	//error
	// errlogPrintf("incorrect prologue %2s %2s\n",buffer,this->prologue);
	return -1;
      }

      //errlogPrintf("Recieved message %2s %2s\n",buffer,this->prologue);
      
      this->message_type=*((u_int8_t *) ((char *) buffer+2));
      this->control_code=*((u_int8_t *) ((char *) buffer+3));
      this->message_parameter.word = be32toh(*(u_int32_t *) ((char *) buffer+4));
      this->payload_length = be64toh(*(u_int64_t *) ((char *) buffer+8));

      this->printf();

      if((expected_message_type != AnyMessages) &&
	 (expected_message_type != this->message_type)){
	//error!
	// in overlapped mode, should we keep it?
	// errlogPrintf("Error message types does not match %d vs %d\n",expected_message_type,
	// 	     this->message_type);
	return -1;
      }
      
      // errlogPrintf("successfull recieved message header %ld \n",rsize);
      
      return rsize;
    }
    
    int fromRawData(void *buffer){ //DeSerialize
      if (memcmp(this->prologue, buffer,2) != 0){
	//error
	return -1;
      }
      this->message_type=*((u_int8_t *) ((char *) buffer+2));
      this->control_code=*((u_int8_t *) ((char *) buffer+3));
      this->message_parameter.word = be32toh(*(u_int32_t *) ((char *) buffer+4));
      this->payload_length = be64toh(*(u_int64_t *) ((char *) buffer+8));
      return 0;
    }
    int toRawData(void *buffer){ //Serialize this as bytes data in buffer.
      memcpy( buffer, this->prologue, 2);
      *((char *) buffer + 2) = this->message_type;
      *((char *) buffer + 3) = this->control_code;
      *((u_int32_t *)((char *) buffer+4))=htobe32(this->message_parameter.word);
      *((u_int64_t *)((char *) buffer+8))=htobe64(this->payload_length);
      return 0;
    }
  };
  
  typedef class Message:public Header{
  public:
    void   *payload=NULL;

    Message(Message_Types_t message_type):Header(message_type){
    };
    Message(void *raw_header):Header(raw_header){
      //this->fromRawData(raw_header);
    };
    Message(void *raw_header, void *payload):Message(raw_header){
      this->payload =  calloc(this->payload_length, 1);
      if (this->payload != NULL){
	memcpy(this->payload, payload, this->payload_length);
      };
    }
    Message(u_int8_t  type,
	    u_int8_t  cce,
	    message_parameter_t param,
	    u_int64_t length,
	    u_int8_t *payload):Header(type,cce,param,length),payload(payload) {
      //this->payload= (void *) callocMustSucceed(1, length, "HiSLIP pyload buffer");
      //memcpy(this->payload, payload, length);
      //this->payload = payload;
    }
    size_t send(int socket){
      size_t ssize;
      ssize=this->Header::send(socket);
      if (ssize < HEADER_SIZE){
	return -1;
      }
      return (ssize + ::send(socket, this->payload, this->payload_length,0));
    }
    
    ssize_t recv(int socket, Message_Types_t expected_message_type=AnyMessages){
      size_t rsize;
      size_t status;
      
      /* setsockopt(sock, SOL_SOCKET, */
      /* 		 SO_RCVTIMEO, &(msg->socket_timeout), sizeof(int)); */

      // read header part and update header.
      
      rsize=this->Header::recv(socket, expected_message_type);

      if (rsize < 0){
	// Error!
	// errlogPrintf("failed to read header.%ld \n",rsize);
	return rsize;
      }

      // now prepare for a pyload.
      if (this->payload==NULL && this->payload_length > 0){
	this->payload = (void *) calloc(this->payload_length,1);
	if (this->payload == NULL){
	  perror("faile to allocate memory for payload.");
	  return -1;
	}
      }
      
      rsize=0; //returns size of recieved payload.
      if (this->payload_length > 0){
	size_t bytestoread=this->payload_length;
	
	while (bytestoread){
	  status = ::recv(socket, ((u_int8_t *)this->payload+rsize), bytestoread, 0);
	
	  if (status <= 0){
	    perror("payload read error:");
	    // errlogPrintf("recive error\n");
	    return -1;
	  }
	  rsize +=status;
	  if (status >= bytestoread){
		break;
	  }
	  bytestoread -=status;
	}	  
      } 
      // errlogPrintf("successfull recieved message %ld \n",rsize);
      return (rsize);
    }
  } Message_t;
    
  typedef class HiSLIP {
  public:
    unsigned long maximum_message_size=MAXIMUM_MESSAGE_SIZE;
    unsigned long maximum_payload_size=MAXIMUM_MESSAGE_SIZE - HEADER_SIZE;
    long socket_timeout=SOCKET_TIMEOUT;
    long lock_timeout=LOCK_TIMEOUT;
    int sync_channel;
    int async_channel;
    struct pollfd sync_poll;
    struct pollfd async_poll;
    int overlap_mode;
    int session_id;
    int server_protocol_version;
    unsigned int server_vendorID;

    bool rmt_delivered;
    u_int32_t message_id;
    u_int32_t most_recent_message_id;

    void set_timeout( long timeout){
      this->socket_timeout=timeout;
    };
    long get_timeout(void){
      return this->socket_timeout;
    };
    void set_lock_timeout( long timeout){
      this->lock_timeout=timeout;
    }
      ;
    long get_lock_timeout(void){
      return this->lock_timeout;
    };

    HiSLIP(){};
    void connect(char const* hostname){
      this->connect(hostname,
		    Default_device_name,
		    Default_Port);
    };
    void connect(char const* hostname,
		 char const* dev_name,
		 int  port);
    long set_max_size(long message_size);
    int  device_clear(void);
    u_int8_t status_query(void);
    //long write(u_int8_t *data_str, long timeout=LOCK_TIMEOUT);
    long write(const u_int8_t *data_str, const size_t size, long timeout=LOCK_TIMEOUT);
    size_t ask(u_int8_t *data_str, size_t size,
	       u_int8_t **rbuffer, long wait_time=LOCK_TIMEOUT);
    int  read(size_t *received, long timeout=LOCK_TIMEOUT );
    int  read(size_t *received, u_int8_t **buffer, long timeout=LOCK_TIMEOUT);
    int  read(size_t *received, u_int8_t *buffer,  size_t bsize, long timeout=LOCK_TIMEOUT);
    long trigger_message(void);
    long remote_local(u_int8_t request);
    long request_lock(const char* lock_string=NULL);
    long release_lock(void);
    long request_srq_lock(void);
    long release_srq_lock(void);
    Message *get_Service_Request(void){
      Message *msg=new Message(AnyMessages);
      long status;
      // errlogPrintf("In get_service_Request\n");
      this->request_srq_lock();
      
      //status= msg->recv(this->async_channel, AsyncServiceRequest);
      status= msg->recv(this->async_channel);
      
      if (status != 0){
	// errlogPrintf("get_service_Request: failed to read error.\n");
	// should handle Error/Fatal Error/Async Interrupted messages.
	perror(__FUNCTION__);
      }
      this->release_srq_lock();
      // errlogPrintf("Exit from get_service_Request\n");
      return msg;
    };
    int wait_for_SRQ(int wait_time){
      return ::poll(&this->async_poll,  1,   wait_time);
    }
    void disconnect(){
      if (this->sync_channel){
	close(this->sync_channel);
      };
      if (this->async_channel){
	close(this->async_channel);
      };
    };
    
  private:
    int wait_for_answer(int wait_time){
      return ::poll(&this->sync_poll,  1,   wait_time);
    }
    void reset_message_id(void){
      this->most_recent_message_id=0;
      this->message_id = 0xffffff00;
    }
    u_int32_t increment_message_id(void){
      this->most_recent_message_id=this->message_id;
      this->message_id = (this->message_id +2) & 0xffffffff;
      return this->message_id;
    };
  } HiSLIP_t;
}; // namespace
