#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS += configure
DIRS += makeSupport

DIRS += asyn
asyn_DEPEND_DIRS = configure
DIRS += testApp
testApp_DEPEND_DIRS = asyn
DIRS += testGpibApp
testGpibApp_DEPEND_DIRS = testApp
DIRS += testGpibSerialApp
testGpibSerialApp_DEPEND_DIRS = testApp
DIRS += testEpicsApp
testEpicsApp_DEPEND_DIRS = testApp
DIRS += testManagerApp
testManagerApp_DEPEND_DIRS = asyn

ifdef SNCSEQ
DIRS += testIPServerApp
testIPServerApp_DEPEND_DIRS = asyn
endif

DIRS += iocBoot
iocBoot_DEPEND_DIRS = testApp testGpibApp testGpibSerialApp testEpicsApp testManagerApp
ifdef SNCSEQ
iocBoot_DEPEND_DIRS += testIPServerApp
endif

include $(TOP)/configure/RULES_TOP
