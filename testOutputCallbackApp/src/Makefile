TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testOutputCallback.dbd

LIBRARY_IOC += testOutputCallbackSupport
testOutputCallbackSupport_SRCS += testOutputCallback.cpp
testOutputCallbackSupport_LIBS += asyn
testOutputCallbackSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testOutputCallback

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testOutputCallback_SRCS_DEFAULT += testOutputCallback_registerRecordDeviceDriver.cpp testOutputCallbackMain.cpp
testOutputCallbackVx_SRCS_vxWorks  += testOutputCallback_registerRecordDeviceDriver.cpp
testOutputCallback_LIBS += testOutputCallbackSupport asyn
testOutputCallback_LIBS += $(EPICS_BASE_IOC_LIBS)

testOutputCallback_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
