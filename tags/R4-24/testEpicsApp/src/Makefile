TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += testEpics.dbd

LIBRARY_IOC += testEpicsSupport
testEpicsSupport_SRCS += int32Driver.c
testEpicsSupport_SRCS += uint32DigitalDriver.c
testEpicsSupport_LIBS += asyn
testEpicsSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_IOC += testEpics

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testEpics_SRCS += testEpics_registerRecordDeviceDriver.cpp
testEpics_SRCS += testEpicsMain.c

testEpics_LIBS += testEpicsSupport
testEpics_LIBS += testSupport
testEpics_LIBS += asyn
testEpics_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

