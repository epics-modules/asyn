#!cython
# -*- coding:utf-8 -*-
# distutils: language=c++

cdef int SCPIRawSocketPort=5025 # both udp/tcp
cdef int SCPITelnetPort=5024    # both udp/tcp


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
  cdef char *   Default_device_name "nsHiSLIP::Default_device_name"
  cdef int HiSLIPPort "nsHiSLIP::Default_Port"       # not in /etc/services

  cdef cppclass message_parameter "nsHiSLIP::message_parameter"
  cdef cppclass Header  "nsHiSLIP::Header"
  cdef cppclass Message "nsHiSLIP::Message"
  
  cdef cppclass cHiSLIP "nsHiSLIP::HiSLIP":
    unsigned long maximum_message_size
    unsigned long maximum_payload_size
    int overlap_mode;
    int session_id;
    int server_protocol_version;
    unsigned int server_vendorID;

    int rmt_delivered;
    u_int32_t message_id;
    u_int32_t most_recent_message_id;

    cHiSLIP() except+
    void connect(char * hostname,
                 char * dev_name,
                 int    port)
    void set_timeout(long)
    long get_timeout()
    void  set_lock_timeout(long)
    long get_lock_timeout()
    
    long set_max_size(long)
    int device_clear()
    u_int8_t status_query()
    long write(u_int8_t *,  size_t, long)
    long read(size_t *, u_int8_t **, long)
    long ask(u_int8_t *, size_t, u_int8_t **, long)
    long trigger_message()
    long remote_local(u_int8_t)
    long request_lock(char *)
    long release_lock()
    long request_srq_lock()
    long release_srq_lock()
    int check_srq_lock()
    int check_and_lock_srq_lock()
    u_int8_t get_Service_Request()
    int  wait_for_SRQ(int)
    void disconnect()
    
