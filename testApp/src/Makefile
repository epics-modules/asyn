TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)

USR_CFLAGS += -DUSE_TYPED_RSET -DUSE_TYPED_DSET -DUSE_TYPED_DRVET

DBD += devTestBlock.dbd
DBD += test.dbd

LIBRARY_IOC += testSupport
testSupport_SRCS += echoDriver.c
testSupport_SRCS += addrChangeDriver.c
testSupport_SRCS += devTestBlock.c
testSupport_SRCS += interposeInterface.c
testSupport_SRCS += asynExample.c
testSupport_LIBS += asyn
testSupport_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_IOC += test

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
test_SRCS += test_registerRecordDeviceDriver.cpp
test_SRCS += testMain.c

test_LIBS += testSupport
test_LIBS += asyn
test_LIBS += $(EPICS_BASE_IOC_LIBS)

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

