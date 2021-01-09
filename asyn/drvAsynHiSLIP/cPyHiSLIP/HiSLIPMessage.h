//-*- coding:utf-8 -*-
#define NDEBUG 1
#define DEBUG 1

#include <assert.h>
#include <errno.h>
//#define _GNU_SOURCE         /* See feature_test_macros(7) */
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h> //memcpy, strlen, etc
#include <sys/types.h>
#include <sys/param.h> /* for MAXHOSTNAMELEN */
#include <unistd.h> /* close() and others */

#include <poll.h>
#include <sys/socket.h>
#include <netinet/tcp.h>
#include <netdb.h>
#include <pthread.h>  // for MacOS
#include <stdexcept>  //for std::exception
#include <mutex>

#include <chrono>
using namespace std::chrono;

// for async
#include <future>

//#include <semaphore.h>

#ifdef __linux__
#   include <endian.h> // network endian is "be".
#else // i.e. macOSX
inline u_int64_t htobe64(u_int64_t q) {
  return (htonl(q>>32) +((u_int64_t)htonl(q & 0x00000000ffffffff)<<32)) ;
};

inline u_int64_t be64toh(u_int64_t q) {
  return (ntohl(q>>32) +((u_int64_t)ntohl(q & 0x00000000ffffffff)<<32)) ;
};
#endif

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
  typedef class HiSLIP_FatalError:std::exception {
    char *msg;
  public:
    HiSLIP_FatalError(char * const msg){
      this->msg=msg;
    }
  } FatalError_t;
  
  typedef class HiSLIP_Error:std::exception {
    const char *msg;
  public:
    HiSLIP_Error(const char * const msg){
      this->msg=msg;
    }
  } Error_t;
  
  typedef class HiSLIP_Interrupted:std::exception {
    const char *msg;
  public:
    HiSLIP_Interrupted(const char * const msg){
      this->msg=msg;
    }
  } Interrupted_t;
  
  typedef class HiSLIP_SRQ:std::exception {
    const char *msg;
  public:
    HiSLIP_SRQ(const char * const msg){
      this->msg=msg;
    }
  } SRQ_t;
  
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

  typedef enum CC_LockRequestResponse{
			       fail=0,         //Lock was requested but not granted
			       success=1,      //release of exclusive lock
			       locK_error=3 // Invalid
  } CC_LockRequestResponse_t;
  
  typedef enum CC_LockReleaseResponse{
			       success_exclusive=1,      //release of exclusive lock
			       success_shared=2, //release of shared lock
			       release_error=3 // Invalid
  } CC_LockReleaseResponse_t;

  static const long  PROTOCOL_VERSION_MAX = 0x7f7f ; // # <major><minor> = <1><1> that is 257
  static const long  INITIAL_MESSAGE_ID = 0xffffff00 ;
  static const long  INITIAL_LAST_MESSAGE_ID = 0xfffffefe; //i.e. 0xffffff00-2
  static const long  UNKNOWN_MESSAGE_ID  = 0xffffffff;
  
  //Following VISA 256 bytes + header length 16 bytes
  static const long  MAXIMUM_MESSAGE_SIZE_VISA = 272;
  static const long  MAXIMUM_MESSAGE_SIZE= 8192;//R&S accept more than this
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
			     // for HiSLIP ver2
			     GetDescriptors =26,
			     GetDescriptorsResponse =27,
			     StartTLS = 28,
			     AsyncStartTLS = 29,
			     AsyncStartTLSResponse = 30,
			     EndTLS = 31,
			     AsyncEndTLS = 32,
			     AsyncEndTLSResponse = 33,
			     GetSaslMechanismList = 34,
			     GetSaslMechanismListResponse = 35,
			     AuthenticationStart = 36,
			     AuthenticationExchange = 37,
			     AuthenticationResult = 38,
			     // 39-127 are reserved for future use.
			     // I don't watn to use negative value to
			     // represent ANY message.
			     // So I picked 127 from reserved values
			     // for this purpose.
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
    };
    message_parameter(u_int16_t proto, u_int16_t sid){
      this->word = (proto << 16) + sid;

    };
    u_int16_t getServerProtocolVersion(){
      return (u_int16_t) (((this->word) & 0xffff0000)>>16);
    }
    u_int16_t getSessionId(){
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
    //
    Header(Message_Types_t message_type):control_code(0),message_parameter(0),payload_length(0){
      this->message_type=message_type;
    }
    //
    Header(const void *buffer):message_parameter(0){
      assert(memcmp(this->prologue, buffer,2) == 0);
      this->message_type=*((u_int8_t *) ((char *) buffer+2));
      this->control_code=*((u_int8_t *) ((char *) buffer+3));
      this->message_parameter.word = ntohl(*(u_int32_t *) ((char *) buffer+4));
      this->payload_length = be64toh(*(u_int64_t *) ((char *) buffer+8));
    }
    //
    Header(const u_int8_t  type,
	   const u_int8_t  cce,
	   const message_parameter_t param,
	   const u_int64_t length):message_parameter(param.word){
      this->message_type=type;
      this->control_code=cce;
      this->payload_length=length;
    }
    //
    void print(const char *msg=NULL){
      if (msg != NULL){
	::printf("Header dump for: %s\n", msg);
      }
      ::printf("message type:%d\n",this->message_type);
      ::printf("control_code:%d\n",this->control_code);
      ::printf("message_parameter: 0x%0x\n",this->message_parameter.word);
      ::printf("payload length: %llu\n",this->payload_length);
    }
    //
    ssize_t send(int socket){
      char hbuf[HEADER_SIZE];
      ssize_t ssize;
      
      this->toRawData(hbuf);
      
      //ssize=::send(socket, hbuf, sizeof(hbuf), 0);
      {
	auto fut=std::async(std::launch::async,
			    ::send,
			    socket,hbuf,sizeof(hbuf),0);
	ssize= fut.get();
      }
      return ssize;
    }
    //
    ssize_t recv(int socket){
      unsigned char buffer[HEADER_SIZE];
      return this->recv(socket, buffer);
    }
    //
    ssize_t recv(int socket, void *buffer);

    int fromRawData(void *buffer){ //DeSerialize
      if (memcmp(this->prologue, buffer, 2) != 0){
	//error
	return -1;
      }
      this->message_type=*((u_int8_t *) ((char *) buffer+2));
      this->control_code=*((u_int8_t *) ((char *) buffer+3));
      this->message_parameter.word = ntohl(*(u_int32_t *) ((char *) buffer+4));
      this->payload_length = be64toh(*(u_int64_t *) ((char *) buffer+8));
      return 0;
    }
    int toRawData(void *buffer){ //Serialize this as bytes data in buffer.
      memcpy( buffer, this->prologue, 2);
      *((char *) buffer + 2) = this->message_type;
      *((char *) buffer + 3) = this->control_code;
      //*((u_int32_t *)((char *) buffer+4))=htobe32(this->message_parameter.word);
      *((u_int32_t *)((char *) buffer+4))=htonl(this->message_parameter.word);
      *((u_int64_t *)((char *) buffer+8))=htobe64(this->payload_length);
      return 0;
    }
  };
  
  typedef class Message:public Header{
  public:
    void   *payload;
    int    clean_on_exit;
    
    ~Message(){
      if ((this->payload != NULL) && (this->clean_on_exit==1)){
	::free(this->payload);
	this->payload=NULL;
	this->clean_on_exit=0;
      }
    };
    
    Message(Message_Types_t message_type):Header(message_type){
      this->payload=NULL;
      this->clean_on_exit=0;
    };
    Message(void *raw_header):Header(raw_header){
      this->payload=NULL;
      this->clean_on_exit=0;
      //this->fromRawData(raw_header);
    };
    Message(void *raw_header, void *payload):Message(raw_header){
      this->payload =  calloc(this->payload_length, 1);
      this->clean_on_exit=1;
      if (this->payload != NULL){
	memcpy(this->payload, payload, this->payload_length);
      };
    }
    Message(u_int8_t  type,
	    u_int8_t  cce,
	    message_parameter_t param,
	    u_int64_t length,
	    u_int8_t *payload):Header(type,cce,param,length),payload(payload) {
      this->clean_on_exit=0;
    }
    //
    size_t send(int socket){
      size_t ssize;
      // ssize=this->Header::send(socket);
      // if (ssize < HEADER_SIZE){
      // 	return -1;
      // }
      // return (ssize + ::send(socket, this->payload, this->payload_length,0));
      u_int8_t *buffer = (u_int8_t *) calloc(sizeof(Header)+this->payload_length, 1);
      this->toRawData(buffer);
      memcpy(buffer+sizeof(Header),this->payload, this->payload_length);
      ssize= ::send(socket, buffer, sizeof(Header)+this->payload_length,0);
      free(buffer);
      return ssize;
    }
    
    ssize_t recv(int socket, Message_Types_t expected_message_type = AnyMessages);
    
  } Message_t;
    
  typedef class HiSLIP {
  public:
    unsigned long maximum_message_size; // max message size server accept
    unsigned long maximum_payload_size;
    unsigned long current_message_size; // current max message size setting
    unsigned long current_payload_size; 
    long socket_timeout;
    long lock_timeout;
    int sync_channel;
    int async_channel;
    pthread_mutex_t async_lock=PTHREAD_MUTEX_INITIALIZER;;
    struct pollfd sync_poll;
    struct pollfd async_poll;
    u_int8_t feature_setting;
    u_int8_t feature_preference;
    int session_id;
    int server_protocol_version;
    unsigned int server_vendorID;

    bool overlap_mode; // false for synchronized mode, true for overlapped mode
    bool interrupted;
    bool async_interrupted;
    bool rmt_delivered;
    u_int32_t message_id;
    u_int32_t most_recent_message_id;
    u_int32_t most_recent_received_message_id;
    //sem_t srq_lock;
    pthread_mutex_t srq_lock=PTHREAD_MUTEX_INITIALIZER;
    HiSLIP(){
      this->maximum_message_size=this->current_message_size= MAXIMUM_MESSAGE_SIZE;
      this->maximum_payload_size=this->current_payload_size= MAXIMUM_MESSAGE_SIZE - HEADER_SIZE;
      this->socket_timeout=SOCKET_TIMEOUT;
      this->lock_timeout=LOCK_TIMEOUT;
      this->feature_preference=0;
      this->feature_setting=0;
      this->overlap_mode=false; //i.e. synchronized mode. can be delived from feature_setting.
      this->interrupted=false;
      this->async_interrupted=false;
      this->rmt_delivered=false;
      this->sync_channel=0;
      this->async_channel=0;
      //this->async_lock=PTHREAD_MUTEX_INITIALIZER;
      //
      //this->srq_lock=PTHREAD_MUTEX_INITIALIZER;
      // if (sem_init(&(this->srq_lock), 0, 1) !=0){
      // 	perror(" HiSLIP srq_lock");
      // }
    };
    void connect(char const* hostname){
      this->connect(hostname,
		    Default_device_name,
		    Default_Port);
    };
    
    void connect(char const* hostname,
		 char const* dev_name,
		 int  port); 

    void set_timeout( long timeout){
      this->socket_timeout=timeout;
    };
    
    long get_timeout(void){
      return this->socket_timeout;
    };
    
    void set_lock_timeout( long timeout){
      this->lock_timeout=timeout;
    };
    
    long get_lock_timeout(void){
      return this->lock_timeout;
    };

    long set_max_size(unsigned long message_size);
    int  device_clear(u_int8_t);
    int  status_query(void);
    //long write(u_int8_t *data_str, long timeout=LOCK_TIMEOUT);
    long write(const u_int8_t *data_str, const size_t size,
	       long timeout=LOCK_TIMEOUT);
    long ask(u_int8_t *data_str, size_t size, u_int8_t **rbuffer,
	     long wait_time=LOCK_TIMEOUT);
    long read(size_t *received, long timeout=LOCK_TIMEOUT );
    long read(size_t *received, u_int8_t **buffer,
	      long timeout=LOCK_TIMEOUT);
    long read(size_t *received, u_int8_t *buffer, size_t bsize,
	      long timeout=LOCK_TIMEOUT);
    
    long async_ask(u_int8_t *data_str, size_t size, long wait_time=LOCK_TIMEOUT);
    long async_read( size_t *received, u_int8_t *buffer, size_t bsize,
		     long timeout=LOCK_TIMEOUT);
    
    long trigger_message(void);
    long remote_local(u_int8_t request);
    long request_lock(const char* lock_string = NULL);
    long release_lock(void);
    long lock_info(void);
    long handle_error_msg(void);
    //
    long request_srq_lock(void);
    long release_srq_lock(void);
    int  check_srq_lock(void);
    int  check_and_lock_srq_lock(void);
    //
    int  get_Service_Request(void);
    int  wait_Service_Request(int wait_time);
    //
    int wait_for_Async(int wait_time){
      this->async_poll.revents=0;
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

    void Fatal_Error_handler(Message *msg);
    void Error_handler(Message *msg);
    void report_Fatal_Error(const u_int8_t erc, const char *errmsg);
    void report_Error(const u_int8_t erc, const char *errmsg);
    //
    
  private:
    int wait_for_answer(int wait_time){
      return ::poll(&this->sync_poll,  1,   wait_time);
    }

    void reset_message_id(void){
      this->most_recent_message_id=INITIAL_LAST_MESSAGE_ID;
      this->most_recent_received_message_id=INITIAL_MESSAGE_ID;
      this->message_id = INITIAL_MESSAGE_ID;
    }

    u_int32_t increment_message_id(void){
      this->most_recent_message_id=this->message_id;
      this->message_id = (this->message_id +2) & 0xffffffff;
      return this->message_id;
    };
  } HiSLIP_t;
}; // namespace
