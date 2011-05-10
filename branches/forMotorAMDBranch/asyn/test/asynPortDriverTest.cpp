/*************************************************************************\
* Copyright (c) 2009 UChicago Argonne LLC, as Operator of Argonne
*     National Laboratory.
* Copyright (c) 2002 The Regents of the University of California, as
*     Operator of Los Alamos National Laboratory.
* EPICS BASE is distributed subject to a Software License Agreement found
* in file LICENSE that is included with this distribution. 
\*************************************************************************/

#include <stdio.h>
#include <stdlib.h>

#include "asynPortDriver.h"
#include "epicsUnitTest.h"
#include "testMain.h"

MAIN(asynPortDriverTest)
{
  testPlan(1);
  testOk(true, "DummyTest");
  testDone();
}
