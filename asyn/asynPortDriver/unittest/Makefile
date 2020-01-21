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

PROD_LIBS += asyn
PROD_LIBS += Com

#Tests for the parameters
#TESTPROD_HOST += ParamValTest
#ParamValTest_SRCS += ParamValTest.cpp
#TESTS += ParamValTest

#tests for the paramList
#TESTPROD_HOST += ParamListTest
#asynParamListTest_SRCS += ParamListTest.cpp
#TESTS += ParamListTest

#tests for asynPortDriver
TESTPROD_HOST += asynPortDriverTest
asynPortDriverTest_SRCS += asynPortDriverTest.cpp
testHarness_SRCS += asynPortDriverTest.cpp
TESTS += asynPortDriverTest


# The testHarness runs all the test programs in a known working order.
testHarness_SRCS += asynRunPortDriverTests.c

asynPortDriverTestHarness_SRCS += $(testHarness_SRCS)
asynPortDriverTestHarness_SRCS_RTEMS += rtemsTestHarness.c

PROD_vxWorks = asynPortDriverTestHarness
PROD_RTEMS += asynPortDriverTestHarness

TESTSPEC_vxWorks = asynPortDriverTestHarness.munch; asynRunPortDriverTests
TESTSPEC_RTEMS = asynPortDriverTestHarness.boot; asynRunPortDriverTests


TESTSCRIPTS_HOST += $(TESTS:%=%.t)
ifneq ($(filter $(T_A),$(CROSS_COMPILER_RUNTEST_ARCHS)),)
TESTPROD = $(TESTPROD_HOST)
TESTSCRIPTS += $(TESTS:%=%.t)
endif

include $(TOP)/configure/RULES
