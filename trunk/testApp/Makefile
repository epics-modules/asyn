TOP=..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

ASYN = $(TOP)
ASYN_LIB = $(TOP)/lib/$(T_A)
ASYN_BIN = $(TOP)/bin/$(T_A)
asyn_DIR = $(ASYN_LIB)

DBD += test.dbd

#=============================

PROD_IOC += test

# <name>_registerRecordDeviceDriver.cpp will be created from <name>.dbd
test_SRCS += test_registerRecordDeviceDriver.cpp
test_SRCS += testMain.c
test_SRCS += echoDriver.c
test_SRCS += testEcho.c

test_LIBS += asyn
test_LIBS += $(EPICS_BASE_IOC_LIBS)

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE

