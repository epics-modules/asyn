#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS += configure
DIRS += makeSupport

DIRS += asyn
asyn_DEPEND_DIRS = configure

ifneq ($(EPICS_LIBCOM_ONLY),YES)
  DIRS += testApp
  testApp_DEPEND_DIRS = asyn
  DIRS += testGpibApp
  testGpibApp_DEPEND_DIRS = testApp
  DIRS += testGpibSerialApp
  testGpibSerialApp_DEPEND_DIRS = testApp testGpibApp
  DIRS += testEpicsApp
  testEpicsApp_DEPEND_DIRS = testApp
  DIRS += testManagerApp
  testManagerApp_DEPEND_DIRS = asyn
  DIRS += testAsynPortDriverApp
  testAsynPortDriverApp_DEPEND_DIRS = asyn
  DIRS += testErrorsApp
  testErrorsApp_DEPEND_DIRS = asyn
  DIRS += testUsbtmcApp
  testUsbtmcApp_DEPEND_DIRS = asyn
  DIRS += testArrayRingBufferApp
  testArrayRingBufferApp_DEPEND_DIRS = asyn
  ifdef SNCSEQ
    DIRS += testIPServerApp
    testIPServerApp_DEPEND_DIRS = asyn
  endif
  DIRS += iocBoot
  iocBoot_DEPEND_DIRS = testApp testGpibApp testGpibSerialApp testEpicsApp testManagerApp
  ifdef SNCSEQ
    iocBoot_DEPEND_DIRS += testIPServerApp
  endif
endif

DIRS += testAsynPortClientApp
testAsynPortClientApp_DEPEND_DIRS = asyn

include $(TOP)/configure/RULES_TOP
