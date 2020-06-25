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
    //osiSockAddr addr;
    //Message_t *msg;
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
      asprintf(&service, "%d",port);// "4880" for example.
      status=getaddrinfo(hostname, service, &hints, &res);
      free(service);
    }
    
    if ((status !=0) || res == NULL){
      char *msg;
      asprintf(&msg, "getaddrinfo error status:%d res %p",status,res);
      perror(msg);
      free(msg);
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
    // errlogPrintf("connected to async channel %d\n",this->async_channel);

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
  
    // errlogPrintf("Sent a initialize message\n");

    { Message resp(AnyMessages);

      // errlogPrintf("Receive message created\n");
    
      resp.recv(this->sync_channel, nsHiSLIP::InitializeResponse);
    
      this->overlap_mode=resp.control_code;
      this->session_id=resp.message_parameter.getSessionId();
      this->server_protocol_version=resp.message_parameter.getServerProtocolVersion();
    }
    // errlogPrintf("Receive a initialized message %d 0x%x %d \n",
    // 		 this->overlap_mode,this->session_id, this->server_protocol_version
    // 		 );
  
    // errlogPrintf("Sending Async initialize message %x\n",this->session_id);
  
    
  
    {
      Message msg(nsHiSLIP::AsyncInitialize);
      msg.message_parameter.word=this->session_id;
      msg.send(this->async_channel);
    }
    // errlogPrintf("reading Async initialize response\n");
  
    {
      Message resp(AnyMessages);
      resp.recv(this->async_channel, nsHiSLIP::AsyncInitializeResponse);
      this->overlap_mode=resp.control_code;
      this->server_vendorID=resp.message_parameter.word;
    }
    // errlogPrintf("reading Async initialize done\n");
  
    //now setup poll object
    this->reset_message_id();
  
    this->sync_poll.fd=this->sync_channel;
    this->sync_poll.events=POLLIN;
    this->sync_poll.revents=0;

    this->async_poll.fd=this->async_channel;
    this->async_poll.events=POLLIN;
    this->async_poll.revents=0;

    // errlogPrintf("Receive a Async initialized message\n");    
  };

  long HiSLIP::set_max_size(long message_size){
    Message resp(AnyMessages);
    u_int64_t msg_size=htobe64(message_size);

    Message msg=Message(nsHiSLIP::AsyncMaximumMessageSize,
			0,
			message_parameter(0),
			sizeof(msg_size), (u_int8_t *) &msg_size);
    msg.send(this->async_channel);
  
    int ready=poll(&this->async_poll,  1, this->socket_timeout*1000);
    if ( ready == 0){
      return -1;
    }
  
    resp.recv(this->async_channel,nsHiSLIP::AsyncMaximumMessageSizeResponse);
  
    this->maximum_message_size=*((u_int64_t *)(resp.payload));
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

    ready=poll(&this->async_poll,  1, this->socket_timeout*1000);
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
      
      ready=poll(&this->sync_poll,  1, this->socket_timeout*1000);
      if ( ready == 0){
	return -1;
      }
      rsize=resp.recv(this->sync_channel);
      // errlogPrintf("HiSLIP read rsize %ld\n",rsize);
      if (rsize < resp.payload_length){
	//Error!!
	return -1;
      };
      
      // may not be a good idea.
      {	u_int8_t *newbuf;
	
	newbuf=(u_int8_t *) reallocarray(*buffer, 1,
				   *received+resp.payload_length);
	if (newbuf == NULL){
	  // errlogPrintf("Cannot extend memory area\n");
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
    return 0;
  };
  long HiSLIP::remote_local(bool request){
    return 0;
  };
  long HiSLIP::request_lock(const char* lock_string){
    {
      // errlogPrintf("request_lock not implemented yet\n");
      return -1;
    };
    return 0;
  };
  long HiSLIP::release_lock(void){
    {
      // errlogPrintf("release_lock not implemented yet\n");
      return -1;
    };
    return 0;
  };
  long HiSLIP::request_srq_lock(void){
    {
      // errlogPrintf("request_srq_lock not implemented yet\n");
      return -1;
    };
    return 0;
  };
  long HiSLIP::release_srq_lock(void){
    {
      // errlogPrintf("release_srq_lock not implemented yet\n");
      return -1;
    };
    return 0;
  };

} // end of namespace HiSLIP

