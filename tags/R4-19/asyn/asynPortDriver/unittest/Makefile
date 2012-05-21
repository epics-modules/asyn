#*************************************************************************
# Copyright (c) 2006 The University of Chicago, as Operator of Argonne
#     National Laboratory.
# Copyright (c) 2002 The Regents of the University of California, as
#     Operator of Los Alamos National Laboratory.
# EPICS BASE is distributed subject to a Software License Agreement found
# in file LICENSE that is included with this distribution.
#*************************************************************************
TOP=../../..

include $(TOP)/configure/CONFIG

PROD_LIBS += Com
PROD_LIBS += asyn
ASYNTEST = $(TOP)/asyn/test

#Tests for the parameters
TESTPROD_HOST += ParamValTest
ParamValTest_SRCS += ParamValTest.cpp
TESTS += ParamValTest

#tests for the paramList
#TESTPROD_HOST += ParamListTest
#asynParamListTest_SRCS += ParamListTest.cpp
#TESTS += ParamListTest

#tests for asynPortDriver
#TESTPROD_HOST += asynPortDriverTest
#asynPortDriverTest_SRCS += asynPortDriverTest.cpp
#TESTS += asynPortDriverTest

TESTSCRIPTS_HOST += $(TESTS:%=%.t)

include $(TOP)/configure/RULES
