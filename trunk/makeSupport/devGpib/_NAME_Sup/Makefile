TOP=..

include $(TOP)/configure/CONFIG

#==================================================
# build a support library

LIBRARY_IOC += dev_NAME_
dev_NAME__SRCS += dev_NAME_

dev_NAME__LIBS += asyn
dev_NAME__LIBS += $(EPICS_BASE_IOC_LIBS)

DBD += dev_NAME_.dbd
DB_INSTALLS += dev_NAME_.db

include $(TOP)/configure/RULES
#----------------------------------------
