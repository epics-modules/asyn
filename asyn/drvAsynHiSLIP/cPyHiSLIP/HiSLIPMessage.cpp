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
#include "HiSLIPMessage.h"

using nsHiSLIP::CC_request_t;
using nsHiSLIP::CC_Lock_t;
using nsHiSLIP::HiSLIP_t;
using nsHiSLIP::HiSLIP;
using nsHiSLIP::Message_t;

#define MAX_PAYLOAD_CAPACITY 4096
#define IDSTRING_CAPACITY        100

// HiSLIP methods
namespace nsHiSLIP{
  void HiSLIP::connect(const char *hostname,
		       const char *dev_name,
		       const int port //,
		       //const char vendor_id[2]
		       ){
    int status;
    //const char vendor_id[]=Default_vendor_id;

    struct addrinfo hints, *res=NULL;
    memset(&hints,0, sizeof(struct addrinfo));
    hints.ai_flags=AI_NUMERICSERV | AI_CANONNAME;
    hints.ai_flags=AI_NUMERICSERV;
    hints.ai_family=AF_INET; // IPv4
    hints.ai_socktype=SOCK_STREAM; 
    hints.ai_protocol=0; // any protocol
    hints.ai_addrlen=0;
    hints.ai_addr=NULL;
    hints.ai_canonname=NULL;
    hints.ai_next=NULL;
    
    {
      char *service=NULL;
      if (asprintf(&service, "%d",port)// "4880" for example.
	  > 0){
	status=getaddrinfo(hostname, service, &hints, &res);
	free(service);
      }
      else{
	status=getaddrinfo(hostname, "hislip", &hints, &res);
	perror("asprintf failes");
      }
    }
    
    if ((status !=0) || res == NULL){
      char *msg;
      if (asprintf(&msg, "getaddrinfo error status:%d res %p",status,res) >0){
	perror(msg);
	free(msg);
      }
      else{
	perror("getaddrinfo");
      }
      exit (9999);
    }

    //this->sync_channel= ::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    //this->async_channel=::socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK, 0);
    
    this->sync_channel= ::socket(AF_INET, SOCK_STREAM , 0);
    this->async_channel=::socket(AF_INET, SOCK_STREAM , 0);

    if (res-> ai_addr == NULL || res->ai_addrlen == 0){
      perror("empty addinfo");
      freeaddrinfo(res);
      exit (999);
    };
    status = ::connect(this->sync_channel, res->ai_addr, res->ai_addrlen);
    if (status!=0){
      // Error handling
      perror(__FUNCTION__);
    }
    status = ::connect(this->async_channel, res->ai_addr,res->ai_addrlen);
    if (status!=0){
      // Error handling
      perror(__FUNCTION__);
    }
    freeaddrinfo(res);

    {
      Message msg(nsHiSLIP::Initialize,
		  0,
		  message_parameter((u_int16_t) nsHiSLIP::PROTOCOL_VERSION_MAX,
				    (char *) Default_vendor_id),
		  (u_int64_t) 0, (u_int8_t *) NULL);
      // errlogPrintf("sending message %d \n", msg.message_type);
      msg.send(this->sync_channel);
    }
  
    { Message resp(AnyMessages);

      resp.recv(this->sync_channel, nsHiSLIP::InitializeResponse);
    
      this->overlap_mode=resp.control_code;
      this->session_id=resp.message_parameter.getSessionId();
      this->server_protocol_version=resp.message_parameter.getServerProtocolVersion();
    }
    {
      Message msg(nsHiSLIP::AsyncInitialize);
      msg.message_parameter.word=this->session_id;
      msg.send(this->async_channel);
    }
    {
      Message resp(AnyMessages);
      resp.recv(this->async_channel, nsHiSLIP::AsyncInitializeResponse);
      this->overlap_mode=resp.control_code;
      this->server_vendorID=resp.message_parameter.word;
    }
  
    //now setup poll object
    this->reset_message_id();
    this->rmt_delivered = false;
      
    this->sync_poll.fd=this->sync_channel;
    this->sync_poll.events=POLLIN;
    this->sync_poll.revents=0;

    this->async_poll.fd=this->async_channel;
    this->async_poll.events=POLLIN;
    this->async_poll.revents=0;
  };

  long HiSLIP::set_max_size(long message_size){
    Message resp(AnyMessages);
    u_int64_t msg_size=htobe64(message_size);

    Message msg=Message(nsHiSLIP::AsyncMaximumMessageSize,
			0,
			message_parameter(0),
			sizeof(msg_size), (u_int8_t *) &msg_size);
    msg.send(this->async_channel);
  
    int ready=poll(&this->async_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
  
    resp.recv(this->async_channel,nsHiSLIP::AsyncMaximumMessageSizeResponse);
    //The 8-byte buffer size is sent in network order as a 64-bit integer.
    this->maximum_message_size=be64toh(*((u_int64_t *)(resp.payload)));
    this->maximum_payload_size = this->maximum_message_size - HEADER_SIZE;
    return this->maximum_message_size;
  };

  int HiSLIP::device_clear(){
    Message resp(AnyMessages);
    u_int8_t feature_preference;
    int ready;
  
    Message *msg=new Message(nsHiSLIP::AsyncDeviceClear,
			     0,
			     0,
			     0,NULL);

    msg->send(this->async_channel);

    ready=poll(&this->async_poll,  1, this->socket_timeout);
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
  
    ready=poll(&this->sync_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
    resp.recv(this->sync_channel,nsHiSLIP::DeviceClearAcknowledge);
  
    this->overlap_mode=resp.control_code;
    this->reset_message_id();
    this->rmt_delivered = false;
  
    return 0;
  
  };

  u_int8_t HiSLIP::status_query(){
    u_int8_t status;
    int ready;
  
    Message resp(AnyMessages);
  
    Message msg((u_int8_t) nsHiSLIP::AsyncStatusQuery,
		(u_int8_t) this->rmt_delivered,
		message_parameter((u_int32_t) this->most_recent_message_id),
		0, NULL);
    msg.send(this->async_channel);

    ready=poll(&this->async_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
    resp.recv(this->async_channel,nsHiSLIP::AsyncStatusResponse);
  
    status= resp.control_code &0xff;
    return status;
  }


  // long HiSLIP::write(u_int8_t *data_str, long timeout){
  //   return this->write(data_str, this->maximum_message_size,timeout);
  // };

  long HiSLIP::write(u_int8_t const* data_str, size_t const dsize, long timeout){
  
    size_t max_payload_size = this->maximum_message_size - nsHiSLIP::HEADER_SIZE;
    size_t bytestosend=dsize;
    const u_int8_t *buffer=data_str;
    size_t delivered=0;
    size_t count;

    // errlogPrintf("HiSLIP::write sending data %s\n", data_str);
  
    while(bytestosend){
      if (bytestosend < max_payload_size){
	Message msg(nsHiSLIP::DataEnd,
		    this->rmt_delivered,
		    nsHiSLIP::message_parameter(this->message_id),
		    bytestosend, (u_int8_t *) buffer);
	buffer += bytestosend;
	// errlogPrintf("sending message %s\n",(char *) msg.payload);
	count=msg.send(this->sync_channel);
	count -=HEADER_SIZE;
	bytestosend = 0;
	delivered += count ;
      }
      else{
	Message msg(nsHiSLIP::Data,
		    this->rmt_delivered,
		    nsHiSLIP::message_parameter(this->message_id),
		    max_payload_size, (u_int8_t *) buffer);
	count=msg.send(this->sync_channel);
	count -= HEADER_SIZE;
	bytestosend -=count;
	delivered += count;
	buffer += max_payload_size;
      }
      // errlogPrintf("data sent= %lu\n",count);
      this->increment_message_id();
    }
    return delivered;
  };
  
  int HiSLIP::read(size_t *received, u_int8_t **buffer, long timeout){
    bool eom=false;
    size_t rsize=0;
    
    // errlogPrintf("entered to HiSLIP::read(**buffer:%p, timeout:%ld)\n",
    // 		 buffer, timeout);

    *received=0;
    this->rmt_delivered = false;

    while(!eom) {
      int ready;
      Message resp(AnyMessages);
      
      ready=poll(&this->sync_poll,  1, this->socket_timeout);
      if ( ready == 0){
	return -1;
      }
      rsize=resp.recv(this->sync_channel);
      // errlogPrintf("HiSLIP read rsize %ld\n",rsize);
      if (rsize < resp.payload_length){
	//Error!!
	return -2;
      };
      
      // may not be a good idea.
      {	u_int8_t *newbuf;
	
	newbuf=(u_int8_t *) reallocarray(*buffer, 1,
				   *received+resp.payload_length);
	if (newbuf == NULL){
	  // errlogPrintf("Cannot extend memory area\n");
	  return -3;
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
	return -999;
      }
    }
    return -999;
  };
  
  int HiSLIP::read(size_t *received,
		   u_int8_t *buffer, size_t bsize, long timeout){
    bool eom=false;
    size_t rsize=0;

    // errlogPrintf("entered to HiSLIP::read(buffer %p, bsize:%ld, timeout:%ld\n",
    // 		 buffer, bsize, timeout);
    *received=0;
    this->rmt_delivered = false;
    
    if (buffer==NULL || bsize <= 0){
      // errlogPrintf("exit HiSLIP::read improper input buffer:%p bsize:%lu, timeout:%ld\n",
      // 		   buffer, bsize, timeout);
      return -1;
    }
    if (bsize < this->maximum_payload_size){
      // errlogPrintf("exit HiSLIP::buffer size:%ld should be larger than maximum playload size:%ld \n",
      // 		   bsize, this->maximum_payload_size);
    }
    while(!eom) {
      int ready;
      Message resp(AnyMessages);

      ready=::poll(&this->sync_poll, 1, timeout);
      
      if (ready == 0){
	// errlogPrintf("HiSLIP::read read timeout %d %ld \n", ready, lock_timeout);
	return -1;
      }
      
      rsize=resp.recv(this->sync_channel);

      if (rsize < resp.payload_length){
	// errlogPrintf("read data too short %ld %qd \n", rsize, resp.payload_length);
	return -1;
      };
      if (( (*received) + resp.payload_length) > bsize){
	// errlogPrintf("not enough space to store received:%ld resp.payload:%qd bsize:%ld\n",
	// 	     *received, resp.payload_length, bsize);
	
	::memcpy( (buffer + *received), resp.payload, (bsize - *received));
	*received = bsize;
	return 0;
      }
      else{
	// errlogPrintf("received message size %ld %ld  data:%s mt:%d\n",
	// 	     rsize, *received, (char *) resp.payload, resp.message_type);
	::memcpy( (buffer + *received), resp.payload, resp.payload_length);
      
	*received +=resp.payload_length;
      }
      
      if ( resp.message_type == nsHiSLIP::Data){
	continue;
      } else if (resp.message_type == nsHiSLIP::DataEnd){
	eom=true;
	this->rmt_delivered=true;
	// errlogPrintf("received message: %s %s ,eom:%d rmt:%d\n",
	// 	     buffer, (char *) resp.payload, eom,this->rmt_delivered);
	return 0;
      } else{
	// errlogPrintf("Unexpected message type:%d\n",
	// 	     resp.message_type);
	resp.printf();
	// error unexpected message type.
	return -1;
      }
    }
    return -1;
  };

  size_t HiSLIP::ask(u_int8_t  *const data_str, size_t const dsize,
		     u_int8_t **rbuffer,
		     long wait_time){
    size_t rsize=-1;
    u_int8_t *buffer=NULL;
    int status;
    
    // errlogPrintf("sending a command %s %lu",data_str, dsize);
  
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
    Message msg(nsHiSLIP::Trigger,
		(u_int8_t) this->rmt_delivered,
		message_parameter((u_int32_t) this->most_recent_message_id),
		0, NULL);
    msg.send(this->sync_channel);
    this->increment_message_id();
    this->rmt_delivered=false;
    return 0;
  };
  
  long HiSLIP::remote_local(u_int8_t request){
    Message msg(nsHiSLIP::AsyncRemoteLocalControl,
		request,
		message_parameter((u_int32_t) this->most_recent_message_id),
		0, NULL), resp(AnyMessages);
    msg.send(this->async_channel);
    resp.recv(this->async_channel, nsHiSLIP::AsyncRemoteLocalResponse);
    return 0;
  };
  
  long HiSLIP::request_lock(const char* lock_string){
    Message msg(nsHiSLIP::AsyncLock,
		1, // request
		this->lock_timeout,
		strnlen(lock_string,256), (u_int8_t *) lock_string),
      resp(AnyMessages);
    
    msg.send(this->async_channel);
    
    resp.recv(this->async_channel, nsHiSLIP::AsyncLockResponse);
      
    return resp.control_code;
  };
  
  long HiSLIP::release_lock(void){
    long message_id =this->most_recent_message_id;
    if ( message_id == nsHiSLIP::INITIAL_MESSAGE_ID){
      message_id=0;
    }
    Message msg(nsHiSLIP::AsyncLock,
		0, // release
		message_id,
		0, NULL),
      resp(AnyMessages);
    
    msg.send(this->async_channel);
    
    resp.recv(this->async_channel, nsHiSLIP::AsyncLockResponse);
      
    return resp.control_code;
  };
  
  long HiSLIP::request_srq_lock(void){
    if (sem_wait(&(this->srq_lock)) == 0){
      return 0;
    }
    else{
      perror("request_srq_lock");
      return -1;
    }
  };
  
  long HiSLIP::release_srq_lock(void){
    int sval=this->check_srq_lock();
    if (sval != 0){
      return sval;
    }
    if (sem_post(&(this->srq_lock)) == 0){
      return 0;
    }
    else{
      perror("release_srq_lock");
      return -1;
    }
  };
  int HiSLIP::check_srq_lock(void){
    int sval;
    if (sem_getvalue(&(this->srq_lock),&sval) == 0){
      return sval;
    }
    else {
      perror("check_srq_lock");
      return -1;
    }
  }
  
  int HiSLIP::check_and_lock_srq_lock(void){
    int rc=sem_trywait(&(this->srq_lock));
    switch (rc){
    case 0:
      break;
    case EAGAIN:
      break;
    default:
      perror("check_and_lock_srq_lock");
    }
    return rc;
  }
} // end of namespace HiSLIP

