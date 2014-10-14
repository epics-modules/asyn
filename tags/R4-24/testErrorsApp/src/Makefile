TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testErrors.dbd

LIBRARY_IOC += testErrorsSupport
testErrorsSupport_SRCS += testErrors.cpp
testErrorsSupport_SRCS += myTimeStampSource.cpp
testErrorsSupport_LIBS += asyn
testErrorsSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testErrors

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testErrors_SRCS_DEFAULT += testErrors_registerRecordDeviceDriver.cpp testErrorsMain.cpp
testErrorsVx_SRCS_vxWorks  += testErrors_registerRecordDeviceDriver.cpp
testErrors_LIBS += testErrorsSupport asyn
testErrors_LIBS += $(EPICS_BASE_IOC_LIBS)

testErrors_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
