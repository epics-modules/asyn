# Makefile for Asyn _NAME_ support
#
# Created by _USER_ on _DATE_
# Based on the Asyn _TYPE_ template

TOP = .
include $(TOP)/configure/CONFIG

DIRS += configure
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *Sup))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *sup))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *App))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard *app))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocBoot))
DIRS := $(DIRS) $(filter-out $(DIRS), $(wildcard iocboot))

include $(TOP)/configure/RULES_TOP
