TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testQueueOverflow.dbd

LIBRARY_IOC += testQueueOverflowSupport
testQueueOverflowSupport_SRCS += testQueueOverflow.cpp
testQueueOverflowSupport_LIBS += asyn
testQueueOverflowSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testQueueOverflow

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testQueueOverflow_SRCS_DEFAULT += testQueueOverflow_registerRecordDeviceDriver.cpp testQueueOverflowMain.cpp
testQueueOverflow_SRCS_vxWorks  += testQueueOverflow_registerRecordDeviceDriver.cpp
testQueueOverflow_LIBS += testQueueOverflowSupport asyn
testQueueOverflow_LIBS += $(EPICS_BASE_IOC_LIBS)

testQueueOverflow_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
