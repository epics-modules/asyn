#!cython
#-*- coding:utf-8 -*-

# distutils: sources = HiSLIPMessage.cpp

from cython.operator cimport dereference as deref, preincrement as inc, address as addressof

from cPyHiSLIP cimport cHiSLIP
#from cPyHiSLIP cimport HiSLIPPort
#from cPyHiSLIP cimport Default_device_name
import logging
from logging import info,debug,warn,log,fatal
#logging.root.setLevel(logging.DEBUG)

cdef class HiSLIP:
  cdef cHiSLIP *thisobj

  def __cinit__(self,host=None):
      self.thisobj=new cHiSLIP()
      if host:
          self.thisobj.connect(host, Default_device_name, HiSLIPPort)

  def __dealloc__(self):
      del self.thisobj
      pass
  
  def connect(self, host, device=Default_device_name, port=HiSLIPPort):
      debug("connect to {} {} {}".format(host,device, port))
      self.thisobj.connect(host,device,port)

  def write(self, msg, timeout=3000):
      self.thisobj.write(msg, len(msg), timeout)

  cdef _cread(self,timeout=3000):
       cdef unsigned char *buffer=NULL
       cdef size_t recieved=0
       cdef int rt
       debug("calling read in c++")
       rt=self.thisobj.read(addressof(recieved), addressof(buffer), timeout)
       if rt == -1:
           raise RuntimeError("Timeout")
       elif rt < 0:
           raise RuntimeError
       elif (buffer == NULL):
           raise RuntimeError("NULL pointer")
       if recieved == 0:
           return (recieved, None)
       else:
           return (recieved, buffer)
   
  def read(self,timeout=3000):
      recieved, buffer=self._cread(timeout)
      debug("cread result {} {}".format(recieved,len(buffer)))
      return (recieved, buffer[:recieved])

  def ask(self, msg, wait_time=3000):
      rsize, result=self._ask(msg, wait_time)
      return result[:rsize]
  
  cdef _ask(self, u_int8_t *msg, wait_time):
      cdef unsigned char *buffer=NULL
      cdef size_t recieved=0
      cdef int rt
      recieved=self.thisobj.ask(msg, len(msg),
                                addressof(buffer),
                                wait_time)
      return (recieved, buffer)
          
  def set_timeout(self,timeout):
      self.thisobj.set_timeout(timeout)
      return
  
  def get_timeout(self):
      cdef long to
      to=self.thisobj.get_timeout()
      return to

  def set_lock_timeout(self, to):
      to=self.thisobj.set_lock_timeout(to)

  def get_lock_timeout(self):
      cdef long to
      to=self.thisobj.get_lock_timeout()
      return to

  def set_max_message_size(self, message_size):
      cdef long ms
      ms=self.thisobj.set_max_size(message_size)
      return ms
  
  def get_max_message_size(self):
      cdef long ms
      ms=self.thisobj.maximum_message_size
      return ms

  def get_max_payload_size(self):
      cdef long ms
      ms=self.thisobj.maximum_payload_size
      return ms

  def device_clear(self):
      cdef long rc
      rc=self.thisobj.device_clear()
      return rc

  def status_query(self):
      cdef u_int8_t rc
      rc=self.thisobj.status_query()
      return rc
  
  def trigger_message(self):
      cdef long rc
      rc=self.thisobj.trigger_message()
      return rc

  def remote_local(self, request):
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
      cdef u_int8_t _request=request
      
      rc=self.thisobj.remote_local(_request)

      return rc

  def request_lock(self, lock_string):
      """
      response to request:
      0 - Failure
      1 - Success
      3 - Error
      """
      cdef long rc
      cdef char *_lock_string=lock_string
      
      rc=self.thisobj.request_lock(_lock_string)
      
      return rc

  def release_lock(self):
      """
      response to release:
      1 - Success exclusive
      2 - Success shared
      3 - Error
      """
      cdef long rc
      rc=self.thisobj.release_lock()
      return rc
  
  def request_srq_lock(self):
      """
      Not implemented in .cpp yet.
      """
      cdef long rc=-1
      rc=self.thisobj.request_srq_lock()
      return rc
  
  def release_srq_lock(self):
      """
      Not implemented in .cpp yet.
      """
      cdef long rc=-1
      rc=self.thisobj.release_srq_lock()
      return rc

  def check_srq_lock(self):
      """
      1: released
      0: locked
      -1: 
      """
      cdef int rc=-1
      rc=self.thisobj.check_srq_lock()
      return rc

  def check_and_lock_srq_lock(self):
      """
      1: released and now locked.
      0: already locked
      """
      cdef int rc
      rc=self.thisobj.check_and_lock_srq_lock()
      return rc
  
  def get_Service_Request(self):
      cdef u_int8_t rc=0
      rc=self.thisobj.get_Service_Request()
      return rc
      
  def wait_for_SRQ(self, wait_time):
      cdef int rc=-1
      rc=self.thisobj.wait_for_SRQ(wait_time)
      return rc
                   
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
         InitializeResPonce=1
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


      