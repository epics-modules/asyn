TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testOutputReadback.dbd

LIBRARY_IOC += testOutputReadbackSupport
testOutputReadbackSupport_SRCS += testOutputReadback.cpp
testOutputReadbackSupport_LIBS += asyn
testOutputReadbackSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testOutputReadback

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testOutputReadback_SRCS_DEFAULT += testOutputReadback_registerRecordDeviceDriver.cpp testOutputReadbackMain.cpp
testOutputReadbackVx_SRCS_vxWorks  += testOutputReadback_registerRecordDeviceDriver.cpp
testOutputReadback_LIBS += testOutputReadbackSupport asyn
testOutputReadback_LIBS += $(EPICS_BASE_IOC_LIBS)

testOutputReadback_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
