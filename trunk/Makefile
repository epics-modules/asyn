#Makefile at top of application tree
TOP = .
include $(TOP)/configure/CONFIG
DIRS += configure
DIRS += makeSupport
DIRS += asyn
DIRS += testApp
DIRS += testGpibApp
DIRS += testGpibSerialApp
DIRS += testEpicsApp
DIRS += testManagerApp
ifdef SNCSEQ
DIRS += testIPServerApp
endif
DIRS += iocBoot
include $(TOP)/configure/RULES_TOP
