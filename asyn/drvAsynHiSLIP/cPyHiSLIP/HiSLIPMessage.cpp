#include "HiSLIPMessage.h"

using nsHiSLIP::CC_request_t;
using nsHiSLIP::CC_Lock_t;
using nsHiSLIP::HiSLIP_t;
using nsHiSLIP::HiSLIP;
using nsHiSLIP::Message_t;

#define MAX_PAYLOAD_CAPACITY 4096
#define IDSTRING_CAPACITY    100
#define MAX_ERRMSG_CAPACITY 1024

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
      char service[IDSTRING_CAPACITY]={'\x0'};
      if (snprintf(service, IDSTRING_CAPACITY, "%d",port) // "4880" for example.
	  > 0){
	status=getaddrinfo(hostname, service, &hints, &res);
      }
      else{
	status=getaddrinfo(hostname, "hislip", &hints, &res);
	perror("snprintf failes");
      }
    }
    
    if ((status !=0) || res == NULL){
      char msg[MAX_ERRMSG_CAPACITY]={'\x0'};
      if (snprintf(msg, MAX_ERRMSG_CAPACITY, "getaddrinfo error status:%d res %p",status,res) >0){
	perror(msg);
      }
      else{
	perror("getaddrinfo");
      }
      exit (9999);
    }

    this->sync_channel= ::socket(AF_INET, SOCK_STREAM , 0);
    this->async_channel=::socket(AF_INET, SOCK_STREAM , 0);
    // now Message::send put both header portion and pyload portion as single transaction. So we dont need to use this
    // portion of codes(?).
#   ifdef USE_TCP_NODELAY
    {
      // disable the Nagle algorithm on socket to improve responce with small packet size
      // this may increase number of small packets. So may degrade overall network performance.
      int flag=1, reta, rets;
      rets = setsockopt( this->sync_channel, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag) );
      reta = setsockopt( this->sync_channel, IPPROTO_TCP, TCP_NODELAY, (char *)&flag, sizeof(flag) );
      if ((rets == -1) || (reta == -1)){
    	perror("Couldn't set  sockopt(TCP_DELAY).\n");
      }
    }
#   endif    
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
      msg.send(this->sync_channel);
    }
  
    { Message resp(AnyMessages); 

      int rc=resp.recv(this->sync_channel, nsHiSLIP::InitializeResponse);
      if (rc !=0){
	//Error!
      }
    
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
      int rc=resp.recv(this->async_channel, nsHiSLIP::AsyncInitializeResponse);
      if (rc !=0){
	//
      }
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

    this->set_max_size(this->maximum_message_size);
    
  };

  long HiSLIP::set_max_size(unsigned long message_size){
    Message resp(AnyMessages);
    u_int64_t msg_size=htobe64(message_size);

    Message msg=Message(nsHiSLIP::AsyncMaximumMessageSize,
			0,
			message_parameter(0),
			sizeof(msg_size), (u_int8_t *) &msg_size);
    msg.send(this->async_channel);
  
    this->async_poll.revents=0;
    int ready=poll(&this->async_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
  
    int rc=resp.recv(this->async_channel,nsHiSLIP::AsyncMaximumMessageSizeResponse);
    if (rc!=0){
      //
    }
    //The 8-byte buffer size is sent in network order as a 64-bit integer.
    this->maximum_message_size=be64toh(*((u_int64_t *)(resp.payload)));
    this->maximum_payload_size = this->maximum_message_size - HEADER_SIZE;
    if (message_size < this->maximum_message_size){
      this->current_message_size=message_size;
      this->current_payload_size=message_size - HEADER_SIZE;
    }
    else{
      this->current_message_size = this->maximum_message_size;
      this->current_payload_size = this->maximum_payload_size;
    }
    return this->maximum_message_size;
  };

  int HiSLIP::device_clear(u_int8_t feature_request){
    Message resp(AnyMessages);
    u_int8_t feature_preference;
    int ready,rc;
  
    Message *msg=new Message(nsHiSLIP::AsyncDeviceClear,
			     0,
			     0,
			     0,NULL);

    msg->send(this->async_channel);

    this->async_poll.revents=0;
    ready=poll(&this->async_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
  
    rc=resp.recv(this->async_channel, nsHiSLIP::AsyncDeviceClearAcknowledge);
    if (rc !=0){
      // Error!
    }
    feature_preference=resp.control_code;
    feature_preference &= feature_request;

    msg=new Message(nsHiSLIP::DeviceClearComplete,
		    feature_preference,
		    0,
		    0, NULL);

    msg->send(this->sync_channel);
  
    ready=poll(&this->sync_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
    rc=resp.recv(this->sync_channel,nsHiSLIP::DeviceClearAcknowledge);
    if (rc !=0){
      // Error!
    }
    this->overlap_mode=resp.control_code;
    this->reset_message_id();
    this->rmt_delivered = false;
  
    return 0;
  
  };

  u_int8_t HiSLIP::status_query(){
    u_int8_t status;
    int ready,rc;
  
    Message resp(AnyMessages);
    Message msg((u_int8_t) nsHiSLIP::AsyncStatusQuery,
		(u_int8_t) this->rmt_delivered,
		message_parameter((u_int32_t) this->most_recent_message_id),
		0, NULL);
    if (this->message_id == 0xffffff00){
      msg.message_parameter=message_parameter((u_int32_t) 0xfffffefe);
    }
    msg.send(this->async_channel);
    this->rmt_delivered = false;

    this->async_poll.revents=0;
    ready=poll(&this->async_poll,  1, this->socket_timeout);
    if ( ready == 0){
      return -1;
    }
    rc=resp.recv(this->async_channel, nsHiSLIP::AsyncStatusResponse);
    if (rc !=0){
      //Error!
    }
    //resp.print();
    
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

    while(bytestosend){
      if (bytestosend < max_payload_size){
	Message msg(nsHiSLIP::DataEnd,
		    this->rmt_delivered,
		    nsHiSLIP::message_parameter(this->message_id),
		    bytestosend, (u_int8_t *) buffer);
	buffer += bytestosend;
	count=msg.send(this->sync_channel);
	this->rmt_delivered=false;
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
	this->rmt_delivered=false;
	count -= HEADER_SIZE;
	bytestosend -=count;
	delivered += count;
	buffer += max_payload_size;
      }
      this->increment_message_id();
    }
    return delivered;
  };
  
  long HiSLIP::read(size_t *received, u_int8_t **buffer, long timeout){
    bool eom=false;
    long rstatus=0;
    
    *received=0;
    this->rmt_delivered = false;
    if (*buffer == NULL){
      *buffer=(u_int8_t *) calloc(1,this->current_message_size);
    }
    if (*buffer == NULL){
      perror("buffer allocation error");
      return -1;
    }
    while(!eom) {
      int ready;
      Message resp(AnyMessages);
	
      ready=poll(&this->sync_poll,  1, this->socket_timeout);
      if ( ready == 0){
	return -1;
      }
      
      rstatus=resp.recv(this->sync_channel);
      if (rstatus < 0){
	//Error!!
	return -2;
      };
      
      // ::printf("HiSLIP read() received status:%ld pay_load Size:%llu \n",
      // 	       rstatus, resp.payload_length);
      
      // may not be a good idea to reallocate memory evertime.
      // 'message_parameter' should be checkd
      // aginst most_recent_message_id and overlap_mode
      //
      {	u_int8_t *newbuf;
	
	// newbuf=(u_int8_t *) reallocarray(*buffer, 1,
	// 			   *received+resp.payload_length);
	newbuf = (u_int8_t *) realloc( *buffer,
				       *received + resp.payload_length);

	if (newbuf == NULL){
	  *buffer=NULL;
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
  
  long HiSLIP::read(size_t *received,
		    u_int8_t *buffer, size_t bsize,
		    long timeout){
    bool eom=false;
    long rstatus;
    
    *received=0;
    this->rmt_delivered = false;
    
    if (buffer==NULL || bsize <= 0){
      return -1;
    }
    if (bsize < this->maximum_payload_size){
      ::printf("HiSLIP buffersize:%ld is smallerthan maximum_payload_size:%ld \n ",bsize,this->maximum_payload_size);
    }
    while(!eom) {
      int ready;
      Message resp(AnyMessages);

      ready=::poll(&this->sync_poll, 1, timeout);
      
      if (ready == 0){
	return -1;
      }
      
      rstatus=resp.recv(this->sync_channel);
      
      // ::printf("HiSLIP read() received status:%ld pay_load Size:%llu buffer size: %ld \n",
      // 	       rstatus, resp.payload_length, bsize);
      
      if (rstatus < 0){
	return -1;
      };
      if (( (*received) + resp.payload_length) > bsize){
	::memcpy( (buffer + *received), resp.payload, (bsize - *received));
	*received = bsize;
	return 0;
      }
      else{
	::memcpy( (buffer + *received), resp.payload, resp.payload_length);
      
	*received +=resp.payload_length;
      }
      
      if ( resp.message_type == nsHiSLIP::Data){
	continue;
      } else if (resp.message_type == nsHiSLIP::DataEnd){
	eom=true;
	this->rmt_delivered=true;
	return 0;
      } else{
	// resp.print();
	// error unexpected message type.
	return -1;
      }
    }
    return -1;
  };

  long  HiSLIP::ask(u_int8_t  *const data_str, size_t const dsize,
		     u_int8_t **rbuffer,
		     long wait_time){
    size_t rsize=-1;
    u_int8_t *buffer=NULL;
    int status;
    
  
    this->write(data_str, dsize);
    if(this->wait_for_answer(wait_time) == 0){
      // error
      buffer=NULL;
      return -1;
    };
    status=this->read(&rsize, &buffer, wait_time); // read will allocate memory area pointed by buffer.
    if (status !=0){
      rsize=-1;
    }
    if (rsize > 0){
      *rbuffer=buffer;
    }
    else {
      if (buffer != NULL) free(buffer);
      buffer=NULL;
    }
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
    int rc;
    Message msg(nsHiSLIP::AsyncRemoteLocalControl,
		request,
		message_parameter((u_int32_t) this->most_recent_message_id),
		0, NULL), resp(AnyMessages);
    msg.send(this->async_channel);
    rc=resp.recv(this->async_channel, nsHiSLIP::AsyncRemoteLocalResponse);
    if (rc !=0){
      return -1;
    }
    return 0;
  };
  
  long HiSLIP::request_lock(const char* lock_string){
    int rc;
    Message msg(nsHiSLIP::AsyncLock,
		1, // request
		this->lock_timeout,
		strnlen(lock_string, 256), (u_int8_t *) lock_string),
      resp(AnyMessages);
    
    msg.send(this->async_channel);
    
    rc=resp.recv(this->async_channel, nsHiSLIP::AsyncLockResponse);
    if (rc !=0){
      //error!
    }
    return resp.control_code;
  };
  
  long HiSLIP::release_lock(void){
    int rc=-1;
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
    
    rc=resp.recv(this->async_channel, nsHiSLIP::AsyncLockResponse);
    if(rc != 0){
      //Error
      return -1;
    }
    return resp.control_code;
  };
  
  long HiSLIP::request_srq_lock(void){
    // if (sem_wait(&(this->srq_lock)) == 0){
    //   return 0;
    // }
    // else{
    //   perror("request_srq_lock");
    //   return -1;
    // }
    if (pthread_mutex_lock(&(this->srq_lock)) == 0){
      return 0;
    }
    else{
      perror("request_srq_lock");
      return -1;
    }
  };
  
  long HiSLIP::release_srq_lock(void){
    // int sval=this->check_srq_lock();
    // if (sval != 0){
    //   return sval;
    // }
    // if (sem_post(&(this->srq_lock)) == 0){
    //   return 0;
    // }
    // else{
    //   perror("release_srq_lock");
    //   return -1;
    // }
    if (pthread_mutex_unlock(&(this->srq_lock)) == 0){
      return 0;
    }
    else{
      perror("release_srq_lock");
      return -1;
    }
  };

  int HiSLIP::check_srq_lock(void){
    int rc=pthread_mutex_trylock(&(this->srq_lock));
    if (rc == 0){
      pthread_mutex_unlock(&(this->srq_lock));
      return 1;
    }
    else if (rc == EBUSY){
      return 0;
    }
    perror("check_srq_lock");
    return -1;
  }
  
  int HiSLIP::check_and_lock_srq_lock(void){
    int rc=pthread_mutex_trylock(&(this->srq_lock));
    switch (rc){
    case 0:
      return 1;
    case EBUSY:
      return 0;
    default:
      perror("check_and_lock_srq_lock");
      return -1;
    }
  }
} // end of namespace HiSLIP

