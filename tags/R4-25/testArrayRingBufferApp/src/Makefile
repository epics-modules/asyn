TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

DBD += testArrayRingBuffer.dbd

LIBRARY_IOC += testArrayRingBufferSupport
testArrayRingBufferSupport_SRCS += testArrayRingBuffer.cpp
testArrayRingBufferSupport_LIBS += asyn
testArrayRingBufferSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

#=============================

PROD_IOC += testArrayRingBuffer

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testArrayRingBuffer_SRCS_DEFAULT += testArrayRingBuffer_registerRecordDeviceDriver.cpp testArrayRingBufferMain.cpp
testArrayRingBufferVx_SRCS_vxWorks  += testArrayRingBuffer_registerRecordDeviceDriver.cpp
testArrayRingBuffer_LIBS += testArrayRingBufferSupport asyn
testArrayRingBuffer_LIBS += $(EPICS_BASE_IOC_LIBS)

testArrayRingBuffer_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
