TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

LIBRARY_IOC += testManagerSupport
testManagerSupport_SRCS += testManagerDriver.c
testManagerSupport_SRCS += testManager.c
testManagerSupport_LIBS += asyn
testManagerSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

DBD += testManager.dbd

PROD_IOC += testManager

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testManager_SRCS += testManager_registerRecordDeviceDriver.cpp
testManager_SRCS += testManagerMain.c

testManager_LIBS += testManagerSupport
testManager_LIBS += asyn
testManager_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

