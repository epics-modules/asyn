#!cython
#-*- coding:utf-8 -*-
# distutils: sources = HiSLIPMessage.cpp
# distutils: language=c++

from cython.operator cimport dereference as deref, preincrement as inc, address as addressof

#
import sys

from cPyHiSLIP cimport cHiSLIP
#from cPyHiSLIP cimport HiSLIPPort
#from cPyHiSLIP cimport Default_device_name
import logging
from logging import info,debug,warn,log,fatal,warning
#logging.root.setLevel(logging.DEBUG)

cdef class HiSLIP:
  cdef cHiSLIP *thisobj

  def __cinit__(self, char *host=NULL):
      self.thisobj=new cHiSLIP()
      
  def __init__(self, char *host=NULL):
      cdef char *chost=host
      if host:
          self.connect(host)

  def __dealloc__(self):
      del self.thisobj
      debug("dealloced cHiSLIP object")
      pass
  
  def connect(self, host, device=Default_device_name, port=HiSLIPPort):
      cdef char *chost=host
      cdef char *cdevice =device
      cdef int cport =port
      debug("connect to {} {} {}".format(host, device, port))

      with nogil:
          self.thisobj.connect(chost,cdevice,cport)
          
      # debug("start async_receiver  to {} {} {}".format(host, device, port))
      # with nogil:          
      #     self.thisobj.start_async_receiver_thread()

      # debug("setmaxsize to {} {} {}".format(host, device, port))
      # with nogil:          
      #     self.thisobj.set_max_size(MAXIMUM_MESSAGE_SIZE)
          
          
  @property
  def session_id(self):
      """
      a getter function for the property session_id.
      """
      return self.thisobj.session_id

  @property
  def message_id(self):
      """
      """
      return self.thisobj.message_id

  @property
  def most_recent_message_id(self):
      """
      """
      return self.thisobj.most_recent_message_id

  @property
  def most_recent_received_message_id(self):
      """
      """
      return self.thisobj.most_recent_received_message_id

  @property
  def overlap_mode(self):
      """
      """
      return self.thisobj.overlap_mode

  def write(self, msg, long timeout=3000):
      cdef u_int8_t *cmsg=msg
      cdef size_t ssize=len(msg)
      with nogil:
          self.thisobj.write(cmsg, ssize, timeout)

  cdef _cread(self,long timeout=3000) except+:
       cdef unsigned char *buffer=NULL
       cdef unsigned char **pbuffer=&buffer
       cdef size_t recieved=0,
       cdef size_t *precieved=&recieved
       cdef int rt
       # debug("calling read in c++")
       try:
           with nogil:
               rt=self.thisobj.read(precieved, pbuffer, timeout)
       except RuntimeError as e:
           debug("catch c++ exception error: {}".format(sys.exc_info()))
           #print (e)
           raise 
       except:
           debug("Unexpected error: {}".format(sys.exc_info()[0]))
           raise
       
       if rt == -1:
           if (buffer != NULL):
               free(buffer)
               buffer=NULL
           raise RuntimeError("Timeout")
       elif rt < 0:
           if (buffer != NULL):
               free(buffer)
               buffer=NULL
           raise RuntimeError
       if recieved == 0:
           if (buffer != NULL):
               free(buffer)
               buffer=NULL
           return (recieved, None)
       else:
           if (buffer == NULL):
               raise RuntimeError("NULL pointer")
           rval=(recieved, <bytes> buffer[:recieved]) # to avoid truncated string by \x00 in rawdata
           free(buffer)
           buffer=NULL
           return rval
   
  def read(self, long timeout=3000):
      recieved, buffer=self._cread(timeout)
      debug("cread result {} {} {}".format(recieved,len(buffer),
                                           buffer[:recieved]
                                           )
            )
      return (recieved, buffer[:recieved])

  def ask(self, msg, long wait_time=3000):
      rsize, result=self._ask(msg, wait_time)
      # debug("ask res size: {}".format(rsize))
      if result == None:
          return None
      elif (rsize > 0):
          return result[:rsize]
      else:
          return None
  
  cdef _ask(self, u_int8_t *msg, long wait_time):
      cdef unsigned char *buffer=NULL
      cdef unsigned char **pbuffer=&buffer;
      cdef size_t recieved=0
      cdef int rt
      cdef size_t msgsz=len(msg)
      
      with nogil:
          recieved=self.thisobj.ask(msg,  msgsz,
                                    pbuffer,
                                    wait_time)
      debug("_ask recieved: {}".format(hex(recieved)))
      if (recieved & 0x8000000000000000L):
          if (buffer != NULL):
              free(buffer)
              buffer=NULL
          return (-1,None)
      elif (recieved > 0 and (buffer != NULL)):
          rval=(recieved, buffer[:recieved])
          free(buffer)
          buffer=NULL
          return rval
      else:
          return (recieved, None)
          
  def set_timeout(self,long timeout):
      with nogil:
          self.thisobj.set_timeout(timeout)
      return
  
  def get_timeout(self):
      cdef long to
      with nogil:
          to=self.thisobj.get_timeout()
      return to

  def set_lock_timeout(self, long to):
      with nogil:
          self.thisobj.set_lock_timeout(to)
      return
  
  def get_lock_timeout(self):
      cdef long to
      with nogil:
          to=self.thisobj.get_lock_timeout()
      return to

  def set_max_message_size(self, size_t message_size):
      cdef long ms
      with nogil:
          ms=self.thisobj.set_max_size(message_size)
      return ms
  
  def get_max_message_size(self):
      cdef size_t ms
      ms=self.thisobj.maximum_message_size
      return ms

  def get_max_payload_size(self):
      cdef long ms
      ms=self.thisobj.maximum_payload_size
      return ms

  def get_message_size(self):
      cdef size_t ms
      ms=self.thisobj.current_message_size
      return ms

  def get_payload_size(self):
      cdef long ms
      ms=self.thisobj.current_payload_size
      return ms

  def device_clear(self, u_int8_t request=0):
      cdef long rc
      with nogil:
          rc=self.thisobj.device_clear(request)
      return rc

  def status_query(self):
      """
      bit/weight/name
      0	1 Reserved
      1	2 Reserved
      2	4 Error/Event Queue
      3	8 Questionable Status Register (QUES)
      4	16 Message Available (MAV)
      5	32 Standard Event Status Bit Summary (ESB)
      6	64 Request Service (RQS)/Master Status Summary (MSS)
      7	128 Operation Status Register (OPER)
      
      SCPI/IEEE4882/Tektronix:
        -/RQS(64)/ESB(32)/MAV(16)/----
      STB for Agilent/Keysight: 
        OPER(128)/RQS(64)/ESB(32)/MAV(16)/-/MSG(4)/USR(2)/TRG(1)
      STB for Kikusui PWR01
        OPER(128)/RQS(64)/ESB(32)/MAV(16)/QUES(8)/ERR(4)/-/-
      """
      
      cdef u_int8_t rc

      # if self.thisobj.wait_for_Async(1):
      #     self.thisobj.get_Service_Request()
      with nogil:
          rc=self.thisobj.status_query()
      return rc
  
  def trigger_device(self):
      cdef long rc
      with nogil:
          rc=self.thisobj.trigger_message()
      return rc

  def remote_local(self, u_int8_t request):
      """
      Table 18: Remote Local Control Transactions
      0 - Disable remote
      1 - Enable remote
      2 - Disable remote and go to LOCAL
      3 - Enable remote and go to REMOTE
      4 - Enable remote and Lock Out LOCAL
      5 - Enable remote, go to REMOTE, and set local LOCKOUT
      6 - go to local without changing state of remote enable.
      """
      cdef long rc
      #cdef u_int8_t _request=request
      with nogil:
          rc=self.thisobj.remote_local(request)
      return rc

  def request_lock(self, char *lock_string=b""):
      """
      response to request:
      0 - Failure
      1 - Success
      3 - Error
      """
      cdef long rc
      cdef char *c_lock_string
      if lock_string != b"":
          c_lock_string=lock_string
          with nogil:
              rc=self.thisobj.request_lock(c_lock_string)
      else:
          with nogil:
              rc=self.thisobj.request_lock(lock_string)
      return rc

  def release_lock(self):
      """
      response to release:
      1 - Success exclusive
      2 - Success shared
      3 - Error
      """
      cdef long rc
      with nogil:
          rc=self.thisobj.release_lock()
      return rc
  
  def lock_info(self):
      """
      return (lock_exclusiv, lock_shared) pair.
      where:
      lock_exclusive: 1 if exclusively locked
      lock_shared: number of clients has sahred lock.
      """
      cdef long rc=0
      cdef long lock_shared=0
      cdef u_int8_t lock_exclusive=0
      
      with nogil:
          rc=self.thisobj.lock_info()
      #debug ("rc: {}".format(rc))
      if rc >= 0:
          lock_exclusive= rc & 0xff
          lock_shared = (rc >> 8) & 0xffffffff
          #debug ("rc: {} {}".format(lock_exclusive, lock_shared))
      return (lock_exclusive, lock_shared)
  
  
  def request_srq_lock(self):
      """
      implemented in HiSLIPMessage.cpp.
      rc :0  success
         :others error
      """
      cdef long rc=-1
      with nogil:
          rc=self.thisobj.request_srq_lock()
      return rc
  
  def release_srq_lock(self):
      """
      Not implemented in .cpp yet.
      0: success
      other: error
      """
      cdef long rc=-1
      with nogil:
          rc=self.thisobj.release_srq_lock()
      return rc

  def check_srq_lock(self):
      """
      1: released
      0: locked
      -1: 
      """
      cdef int rc=-1
      with nogil:
          rc=self.thisobj.check_srq_lock()
      return rc

  def check_and_lock_srq_lock(self):
      """
      1: released and now locked.
      0: already locked
      """
      cdef int rc
      with nogil:
          rc=self.thisobj.check_and_lock_srq_lock()
      return rc
  
  def get_overlap_mode(self):
      cdef int ovm=self.thisobj.overlap_mode
      return ovm
  
  def get_feature_setting(self):
      cdef int fs=self.thisobj.feature_setting
      return fs
  
  def get_feature_preference(self):
      cdef int fp=self.thisobj.feature_preference
      return fp
  
  def get_protocol_version(self):
      cdef int spv=self.thisobj.server_protocol_version
      major=(spv&0xff00)>>8
      minor=(spv & 0x00ff)
      return (major, minor)
  
  def get_Service_Request(self):
      cdef u_int8_t rc=0
      with nogil:
          rc=self.thisobj.get_Service_Request()
      return rc

  # def check_SRQ(self):
  #     cdef u_int8_t rc=0
  #     with nogil:
  #         rc=self.thisobj.wait_for_Async(0)
  #         if rc:
  #             rc=self.thisobj.get_Service_Request()
  #     return rc
  
  def wait_Service_Request(self, int wait_time):
      cdef int8_t rc=0
      
      with nogil:
          rc=self.thisobj.wait_Service_Request(wait_time)
      return rc

  def clear_srq_stacks(self):
      with nogil:
          self.thisobj.clear_srq_stacks()
  
  # def wait_for_Async(self, long wait_time=1000):
  #     """ 
  #     return 1 if async port is ready to read
  #     """
  #     cdef int rc=-1
  #     with nogil:
  #         rc=self.thisobj.wait_for_Async(wait_time)
  #     return rc

  # def run_svc(self,int wait=1000):
  #     cdef int rc=-1
  #     cdef u_int8_t st
  #     cdef int cwait=wait
  #     with nogil:
  #         while 1:
  #             rc=self.thisobj.wait_for_Async(cwait) # msec , wait_for_SRQ does not release GIL so we use shorter time
  #             if rc:
  #                 st=self.thisobj.get_Service_Request() # will release lock
  #             if not self.thisobj.session_id:
  #                 break
  #     return

  def report_FatalError(self, u_int8_t erc, char * errmsg):
      self.thisobj.report_Fatal_Error(erc, errmsg)

  def report_Error(self, u_int8_t erc, char * errmsg):
      self.thisobj.report_Error(erc, errmsg)
      
cdef class enumType:
   @classmethod
   def getKey(cls, v):
      for k,kv in cls.__dict__.iteritems():
          if kv == v:
              return k
      return None

   @classmethod
   def getValue(cls, k):
      return cls.__dict__.get(k,None)
   
cdef class HiSLIPMessageType(enumType):
         Initialize=0
         InitializeResPonse=1
         FatalError=2
         Error=3
         AsynLock=4
         AsynLockResponse=5,
         Data=6
         DataEnd=7
         DeviceClearComplete=8
         DeviceCLearAcknowledge=9
         AsyncRemoteLocalContro=10 
         AsyncRemoteLocalResponcse=11
         Trigger=12
         Interrupted=13
         AsyncInterrupted=14 
         AsyncMaximumMessageSize=15 
         AsyncMaximumMessageSizeResponse=16
         AsyncInitilalize=17
         AsyncIntializeResponse=18
         AsyncDeviceClear=19
         AsyncServiceRequest=20
         AsynStatusQuery=21
         AsyncStatusResponse=22
         AsyncDeviceClearAcknowledge=23
         AsynLockInfo=24 
         AsynLockInfoResponse=25
         # VendorSpecific 128-255 inclusive

cdef class FatalErrorCodes(enumType): # Defined Fatal Error codes. Table-7
        UndefinedFatalError=0
        PoorlyFormedMessage=1
        UnEstablishedConnection=2
        InvalidInitializationSequence=3
        ServerRefued=4
        # 5-127 : reserved
        FirstDeviceDefinedError=128
        # 128-255 : Device Defined Error

cdef class ErrorCode(enumType):  # defined Error codes(non-fatal). Table-9
        UndefinedError=0
        UnrecognizedMessageType=1
        UnrecognizedControlCode=2
        UnrecognizedVendorDefinedMessage=3
        MessageTooLarge=4
        # 5-127 : Reserved
        FirstDviceDefinedError=128
        #128-255:Device Defined Error

cdef class  LockControlCode(enumType):
        release=0
        request=1

cdef class LockResponseControlCode(enumType):
        fail=0
        success=1
        successSharedLock=2
        error=3

cdef class RemoteLocalControlCode(enumType):
        disableRemote=0
        enableRemote=1
        disableAndGTL=2
        enableAndGotoRemote=3
        enableAndLockoutLocal=4
        enableAndGTRLLO=5
        justGTL=6


      
