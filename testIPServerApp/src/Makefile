TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += testIPServer.dbd

#=============================

PROD_IOC += testIPServer

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testIPServer_SRCS_DEFAULT += testIPServer_registerRecordDeviceDriver.cpp testIPServerMain.cpp
testIPServerVx_SRCS_vxWorks  += testIPServer_registerRecordDeviceDriver.cpp
testIPServer_SRCS += testIPServer.c
testIPServer_LIBS += testSupport asyn
testIPServer_LIBS += $(EPICS_BASE_IOC_LIBS)

testIPServer_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
