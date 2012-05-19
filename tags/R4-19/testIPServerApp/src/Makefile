TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += testIPServer.dbd

LIBRARY_IOC += testIPServerSupport
testIPServerSupport_SRCS += ipEchoServer.c
testIPServerSupport_SRCS += ipEchoServer2.c
testIPServerSupport_SRCS += ipSNCServer.st
testIPServerSupport_SRCS += asynPortTest.cpp
testIPServerSupport_LIBS += asyn
testIPServerSupport_LIBS += seq pv
testIPServerSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testIPServer

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testIPServer_SRCS_DEFAULT += testIPServer_registerRecordDeviceDriver.cpp testIPServerMain.cpp
testIPServerVx_SRCS_vxWorks  += testIPServer_registerRecordDeviceDriver.cpp
testIPServer_LIBS += testIPServerSupport asyn
testIPServer_LIBS += seq pv
testIPServer_LIBS += $(EPICS_BASE_IOC_LIBS)

testIPServer_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
