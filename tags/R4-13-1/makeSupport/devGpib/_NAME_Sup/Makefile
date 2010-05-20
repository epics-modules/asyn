TOP=..
include $(TOP)/configure/CONFIG
#=======================================

# Build dev_NAME_ as a library for an IOC:
LIBRARY_IOC += dev_NAME_

# Library Source files
dev_NAME__SRCS += dev_NAME_.c

# Link with the asyn and base libraries
dev_NAME__LIBS += asyn
dev_NAME__LIBS += $(EPICS_BASE_IOC_LIBS)

# Install .dbd and .db files
DBD += dev_NAME_.dbd
DB_INSTALLS += dev_NAME_.db

#=======================================
include $(TOP)/configure/RULES
