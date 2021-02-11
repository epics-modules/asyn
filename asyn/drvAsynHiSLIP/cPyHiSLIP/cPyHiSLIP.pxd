#!cython
# -*- coding:utf-8 -*-
# distutils: language=c++

cdef int SCPIRawSocketPort=5025 # both udp/tcp
cdef int SCPITelnetPort=5024    # both udp/tcp

cdef extern from "stdlib.h":
   cdef void free (void *)

cdef extern from "HiSLIPMessage.h":
  # you don’t need to match the type exactly,
  # just use something of the right general kind (int, float, etc).
  
  ctypedef unsigned long  u_long
  ctypedef unsigned int   u_int
  ctypedef unsigned short u_short
  ctypedef unsigned char  u_char
  ctypedef int            bool_t

  ctypedef signed char int8_t
  ctypedef signed short int int16_t
  ctypedef signed       int int32_t
  ctypedef signed long  int int64_t
  
  ctypedef unsigned char      u_int8_t
  ctypedef unsigned short int u_int16_t
  ctypedef unsigned       int u_int32_t
  ctypedef unsigned long  int u_int64_t

cdef extern from "HiSLIPMessage.h" namespace "nsHiSLIP":
  # exceptions
  cdef cppclass HiSLIP_Error "nsHiSLIP:HiSLIP_Error"
  cdef cppclass HiSLIP_FatalError "nsHiSLIP:HiSLIP_FatalError"
  cdef cppclass HiSLIP_Interrupted "nsHiSLIP:HiSLIP_Interrupted"
  cdef cppclass HiSLIP_TimeoutError "nsHiSLIP:HiSLIP_TimeoutError"
  cdef cppclass HiSLIP_RutimeError "nsHiSLIP:HiSLIP_RuntimeError"
  # static variables
  cdef const long  MAXIMUM_MESSAGE_SIZE "nsHiSLIP::MAXIMUM_MESSAGE_SIZE"
  cdef char *   Default_device_name "nsHiSLIP::Default_device_name"
  cdef int HiSLIPPort "nsHiSLIP::Default_Port"       # not in /etc/services
  # classes
  cdef cppclass message_parameter "nsHiSLIP::message_parameter"
  cdef cppclass Header  "nsHiSLIP::Header"
  cdef cppclass Message "nsHiSLIP::Message"
  
  cdef cppclass cHiSLIP "nsHiSLIP::HiSLIP":
    # members
    unsigned long maximum_message_size
    unsigned long maximum_payload_size
    unsigned long current_message_size
    unsigned long current_payload_size
    
    int overlap_mode;
    u_int8_t feature_setting
    u_int8_t feature_preference
    int session_id;
    int server_protocol_version;
    unsigned int server_vendorID;

    int rmt_delivered;
    u_int32_t message_id;
    u_int32_t most_recent_message_id;
    u_int32_t most_recent_received_message_id;
    # methods
    cHiSLIP() except+
    
    void connect(char * hostname,
                 char * dev_name,
                 int    port) nogil except+

    void start_async_receiver_thread() nogil except+ 
    void stop_async_receiver_thread() nogil except+
    void restart_async_receiver_thread() nogil except+
    
    void set_timeout(long) nogil except+
    long get_timeout() nogil except+
    void set_lock_timeout(long) nogil except+
    long get_lock_timeout() nogil except+
    
    long set_max_size(long) nogil except+ 
    int device_clear(u_int8_t) nogil except+
    u_int8_t status_query() nogil except+ 
    long write(u_int8_t *,  size_t, long) nogil except+
    long read(size_t *, u_int8_t **, long) nogil except+
    long ask(u_int8_t *, size_t, u_int8_t **, long) nogil except+
    long trigger_message() nogil except+
    long remote_local(u_int8_t) nogil except+
    long request_lock(char *) nogil except+
    long release_lock() nogil except+
    long lock_info() nogil except+

    long request_srq_lock() nogil except+
    long release_srq_lock() nogil except+
    int check_srq_lock() nogil except+
    int check_and_lock_srq_lock() nogil except+
    int get_Service_Request() nogil except+
    int wait_Service_Request(int wait_time) nogil except+
    void clear_srq_stacks() nogil except+
    # int wait_for_Async(int) nogil except+
    void disconnect() nogil except+
    void report_Fatal_Error(const u_int8_t erc, const char *errmsg) nogil except+
    void report_Error(const u_int8_t erc, const char *errmsg)  nogil except+

