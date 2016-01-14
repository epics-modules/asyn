TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================

PROD_IOC += testBroadcastAsyn
testBroadcastAsyn_SRCS += testBroadcastAsyn
testBroadcastAsyn_LIBS += asyn
testBroadcastAsyn_LIBS += $(EPICS_BASE_IOC_LIBS)

# This is not portable, only builds on Linux
PROD_IOC_Linux += testBroadcastNoAsyn
testBroadcastNoAsyn_SRCS += testBroadcastNoAsyn
testBroadcastNoAsyn_LIBS += $(EPICS_BASE_IOC_LIBS)

PROD_IOC_Linux += testBroadcastBurst
testBroadcastBurst_SRCS += testBroadcastBurst

#===========================

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
