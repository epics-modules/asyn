TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

ifeq ($(OS_CLASS),vxWorks)
DBD += testGpibSerialVx.dbd
testGpibSerialVx_DBD += commonInclude.dbd
endif

ifeq ($(OS_CLASS),WIN32)
DBD += testGpibSerialWin32.dbd
testGpibSerialWin32_DBD += win32Include.dbd
else
DBD += testGpibSerial.dbd
testGpibSerial_DBD += commonInclude.dbd
endif

#=============================

PROD_IOC_DEFAULT += testGpibSerial
PROD_IOC_vxWorks += testGpibSerialVx

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpibSerial_SRCS_DEFAULT += testGpibSerial_registerRecordDeviceDriver.cpp testGpibSerialMain.cpp
testGpibSerial_SRCS_WIN32   += testGpibSerialWin32_registerRecordDeviceDriver.cpp testGpibSerialMain.cpp
testGpibSerialVx_SRCS_vxWorks  += testGpibSerialVx_registerRecordDeviceDriver.cpp
testGpibSerial_SRCS += devTestGpibSerial.c
testGpibSerial_LIBS += testSupport asyn
testGpibSerial_LIBS += $(EPICS_BASE_IOC_LIBS)

testGpibSerialVx_SRCS += devTestGpibSerial.c
testGpibSerialVx_LIBS += testSupport asyn
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
