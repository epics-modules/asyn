TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += testGpibSerial.dbd
DBD += testGpibSerialVx.dbd
testGpibSerial_DBD += serialInclude.dbd

#=============================

PROD_IOC += testGpibSerial
PROD_vxWorks += testGpibSerialVx

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpibSerial_SRCS += testGpibSerial_registerRecordDeviceDriver.cpp
testGpibSerial_SRCS += testGpibSerialMain.cpp
testGpibSerial_SRCS += devTestGpibSerial.c
testGpibSerial_LIBS += testSupport asyn
testGpibSerial_LIBS += $(EPICS_BASE_IOC_LIBS)

testGpibSerialVx_SRCS += testGpibSerialVx_registerRecordDeviceDriver.cpp
testGpibSerialVx_SRCS += devTestGpibSerial.c
ifdef IPAC
testGpibSerialVx_DBD += serialVxInclude.dbd ipacInclude.dbd
testGpibSerialVx_LIBS += Ipac TyGSOctal
else
testGpibSerialVx_DBD += serialVxInclude.dbd
endif
testGpibSerialVx_LIBS += testSupport asyn
testGpibSerialVx_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
