TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

USR_CFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DUSE_TYPED_DRVET

ifeq ($(OS_CLASS),vxWorks)
DBD += testGpibSerialVx.dbd
testGpibSerialVx_DBD += testGpibSerialSupport.dbd
endif

DBD += testGpibSerial.dbd
testGpibSerial_DBD += testGpibSerialSupport.dbd

LIBRARY_IOC = testGpibSerialSupport
testGpibSerialSupport_SRCS += devTestGpibSerial.c
testGpibSerialSupport_LIBS += testSupport asyn
testGpibSerialSupport_LIBS += $(EPICS_BASE_IOC_LIBS)


#=============================

PROD_IOC_DEFAULT += testGpibSerial
PROD_IOC_vxWorks += testGpibSerialVx

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpibSerial_SRCS_DEFAULT += testGpibSerial_registerRecordDeviceDriver.cpp testGpibSerialMain.cpp
testGpibSerialVx_SRCS_vxWorks  += testGpibSerialVx_registerRecordDeviceDriver.cpp
testGpibSerial_LIBS += testSupport
testGpibSerial_LIBS += testGpibSerialSupport
testGpibSerial_LIBS += asyn
testGpibSerial_LIBS += $(EPICS_BASE_IOC_LIBS)

testGpibSerialVx_LIBS += testSupport
testGpibSerialVx_LIBS += testGpibSerialSupport
testGpibSerialVx_LIBS += asyn
testGpibSerialVx_LIBS += $(EPICS_BASE_IOC_LIBS)
ifdef IPAC
testGpibSerialVx_DBD += ipacInclude.dbd
testGpibSerialVx_LIBS_vxWorks += Ipac TyGSOctal
endif
testGpibSerialVx_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
