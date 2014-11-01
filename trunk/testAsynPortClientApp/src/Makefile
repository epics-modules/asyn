TOP=../..

include $(TOP)/configure/CONFIG
#----------------------------------------
#  ADD MACRO DEFINITIONS AFTER THIS LINE
#=============================
PROD_HOST += testAsynIPPortClient
PROD_SRCS += testAsynIPPortClient.cpp

PROD_LIBS += asyn
ifeq ($(EPICS_LIBCOM_ONLY),YES)
  PROD_LIBS += Com
else
  PROD_LIBS += $(EPICS_BASE_IOC_LIBS)
endif

include $(TOP)/configure/RULES
#----------------------------------------
#  ADD RULES AFTER THIS LINE
