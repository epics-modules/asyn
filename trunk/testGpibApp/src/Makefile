TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

DBD += testGpib.dbd
DBD += testGpibVx.dbd

#=============================

PROD_IOC_DEFAULT += testGpib
PROD_IOC_vxWorks += testGpibVx


# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
testGpib_SRCS += testGpib_registerRecordDeviceDriver.cpp
testGpib_SRCS += testGpibMain.c
testGpib_SRCS += devTestGpib.c

testGpib_LIBS += testSupport asyn
testGpib_LIBS += $(EPICS_BASE_IOC_LIBS)

testGpibVx_SRCS += testGpibVx_registerRecordDeviceDriver.cpp
testGpibVx_OBJS_vxWorks += $(EPICS_BASE_BIN)/vxComLibrary
vxComLibrary_DIR = $(EPICS_BASE_BIN)
testGpibVx_SRCS += devTestGpib.c

testGpibVx_LIBS += testSupport asyn
testGpibVx_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

