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
#include <string>

#include "asynPortDriver.h"
#include "epicsUnitTest.h"
#include "testMain.h"

const char PARAM1[] = "PARAM1";
const char PARAM2[] = "PARAM2";
const char PARAM3[] = "PARAM3";
const char PARAM4[] = "PARAM4";
const char PARAM5[] = "PARAM5";
const char PARAM6[] = "PARAM6";
const char PARAM7[] = "PARAM7";
const char PARAM8[] = "PARAM8";
const char PARAM9[] = "PARAM9";
const char PARAM10[] = "PARAM10";
const char PARAM11[] = "PARAM11";
const char PORT_NAME[] = "testDriver";

void testCreateParam1(asynPortDriver *portDriver, const char *paramName,
    asynParamType pType, int expIndex, asynStatus expResult)
{
  int index = -999;
  asynStatus status;
  status = portDriver->createParam(paramName, pType, &index);

  bool result = (status == expResult);
  testOk(result, "Create param %s, exp:actual %d:%d", paramName, expResult,
      status);
  if (result)
    testOk(index == expIndex, " Parameter index %d expected %d", index,
        expIndex);

}

void testFindParam1(asynPortDriver *portDriver, const char *paramName,
    asynStatus expStatus, int expValue)
{
  asynStatus status;
  int initialIndex = -999;
  int index = initialIndex;
  status = portDriver->findParam(paramName, &index);
  testOk(status == expStatus,
      "Checking found %s, status expected:actual %d:%d", paramName, expStatus,
      status);
  if (expStatus == asynSuccess && status == expStatus)
  {
    testOk(expValue == index, "Checking correct index expected:actual %d,%d",
        expValue, index);
  }
  else
  {
    testOk(initialIndex == index,
        "Checking returned index same as initial: exp:acutal %d,%d",
        initialIndex, index);
  }
}

void testFindParam2(asynPortDriver *portDriver, const char *paramName,
    int portNum, asynStatus expStatus, int expValue)
{
  asynStatus status;
  int initialIndex = -999;
  int index = initialIndex;
  status = portDriver->findParam(portNum, paramName, &index);
  testOk(status == expStatus,
      "Checking found %s:%d, status expected:actual %d:%d", paramName, portNum,
      expStatus, status);
  if (expStatus == asynSuccess && status == expStatus)
  {
    testOk(expValue == index, "Checking correct index expected:actual %d,%d",
        expValue, index);
  }
  else
  {
    testOk(initialIndex == index,
        "Checking returned index same as initial: exp:acutal %d,%d",
        initialIndex, index);
  }
}

void testFindName1(asynPortDriver *portDriver, int index, asynStatus expStatus,
    const char *expName)
{
  asynStatus status;
  const char *paramName = NULL;

  status = portDriver->getParamName(index, &paramName);
  testOk(status == expStatus,
      "Checking getParamName for parameter %d, status exp:act, %d:%d", index,
      expStatus, status);
  if (status == asynSuccess)
  {
    testOk(strcmp(expName, paramName) == 0,
        "Checking name returned by getParamName for param %d exp:act %s:%s",
        index, expName, paramName);
  }
  else
  {
    testOk(paramName == NULL,
        "Make sure paramName did not get reassigned if name not found");
  }
}

void testFindName2(asynPortDriver *portDriver, int index, int addr,
    asynStatus expStatus, const char *expName)
{
  asynStatus status;
  const char *paramName = NULL;

  status = portDriver->getParamName(addr, index, &paramName);
  testOk(
      status == expStatus,
      "Checking getParamName for parameter %d for addr %d, status exp:act, %d:%d",
      index, addr, expStatus, status);
  if (status == asynSuccess)
  {
    testOk(
        strcmp(expName, paramName) == 0,
        "Checking name returned by getParamName for param %d for addr %d exp:act %s:%s",
        index, addr, expName, paramName);
  }
  else
  {
    testOk(paramName == NULL,
        "Make sure paramName did not get reassigned if name not found");
  }
}

void testOldConstructor1()
{
  int numDrivers = 3;
  int numParams = 10;
  asynPortDriver portDriver(PORT_NAME, numDrivers, numParams, 0, 0, 0,
      ASYN_TRACE_ERROR, 0, 0);
  testCreateParam1(&portDriver, PARAM1, asynParamInt32, 0, asynSuccess);
  testCreateParam1(&portDriver, PARAM1, asynParamInt32, 0, asynError);
  testCreateParam1(&portDriver, PARAM2, asynParamUInt32Digital, 1, asynSuccess);
  testCreateParam1(&portDriver, PARAM3, asynParamFloat64, 2, asynSuccess);
  testCreateParam1(&portDriver, PARAM4, asynParamOctet, 3, asynSuccess);
  testCreateParam1(&portDriver, PARAM5, asynParamInt8Array, 4, asynSuccess);
  testCreateParam1(&portDriver, PARAM6, asynParamInt16Array, 5, asynSuccess);
  testCreateParam1(&portDriver, PARAM7, asynParamInt32Array, 6, asynSuccess);
  testCreateParam1(&portDriver, PARAM8, asynParamFloat32Array, 7, asynSuccess);
  testCreateParam1(&portDriver, PARAM9, asynParamFloat64Array, 8, asynSuccess);
  testCreateParam1(&portDriver, PARAM10, asynParamGenericPointer, 9,
      asynSuccess);
  testCreateParam1(&portDriver, PARAM11, asynParamGenericPointer, -999,
      asynError);

  testFindParam1(&portDriver, PARAM1, asynSuccess, 0);
  testFindParam1(&portDriver, PARAM2, asynSuccess, 1);
  testFindParam1(&portDriver, PARAM3, asynSuccess, 2);
  testFindParam1(&portDriver, PARAM4, asynSuccess, 3);
  testFindParam1(&portDriver, PARAM5, asynSuccess, 4);
  testFindParam1(&portDriver, PARAM6, asynSuccess, 5);
  testFindParam1(&portDriver, PARAM7, asynSuccess, 6);
  testFindParam1(&portDriver, PARAM8, asynSuccess, 7);
  testFindParam1(&portDriver, PARAM9, asynSuccess, 8);
  testFindParam1(&portDriver, PARAM10, asynSuccess, 9);
  testFindParam1(&portDriver, PARAM11, asynParamNotFound, 0);

  //port 2
  testFindParam2(&portDriver, PARAM1, 1, asynSuccess, 0);
  testFindParam2(&portDriver, PARAM2, 1, asynSuccess, 1);
  testFindParam2(&portDriver, PARAM3, 1, asynSuccess, 2);
  testFindParam2(&portDriver, PARAM4, 1, asynSuccess, 3);
  testFindParam2(&portDriver, PARAM5, 1, asynSuccess, 4);
  testFindParam2(&portDriver, PARAM6, 1, asynSuccess, 5);
  testFindParam2(&portDriver, PARAM7, 1, asynSuccess, 6);
  testFindParam2(&portDriver, PARAM8, 1, asynSuccess, 7);
  testFindParam2(&portDriver, PARAM9, 1, asynSuccess, 8);
  testFindParam2(&portDriver, PARAM10, 1, asynSuccess, 9);
  testFindParam2(&portDriver, PARAM11, 1, asynParamNotFound, 0);

  //port 3
  testFindParam2(&portDriver, PARAM1, 2, asynSuccess, 0);
  testFindParam2(&portDriver, PARAM2, 2, asynSuccess, 1);
  testFindParam2(&portDriver, PARAM3, 2, asynSuccess, 2);
  testFindParam2(&portDriver, PARAM4, 2, asynSuccess, 3);
  testFindParam2(&portDriver, PARAM5, 2, asynSuccess, 4);
  testFindParam2(&portDriver, PARAM6, 2, asynSuccess, 5);
  testFindParam2(&portDriver, PARAM7, 2, asynSuccess, 6);
  testFindParam2(&portDriver, PARAM8, 2, asynSuccess, 7);
  testFindParam2(&portDriver, PARAM9, 2, asynSuccess, 8);
  testFindParam2(&portDriver, PARAM10, 2, asynSuccess, 9);
  testFindParam2(&portDriver, PARAM11, 2, asynParamNotFound, 0);

  testFindParam2(&portDriver, PARAM1, 3, asynError, 0);
  testFindParam2(&portDriver, PARAM10, 3, asynError, 0);

  testFindName1(&portDriver, 0, asynSuccess, PARAM1);
  testFindName1(&portDriver, 9, asynSuccess, PARAM10);
  testFindName1(&portDriver, 10, asynParamBadIndex, PARAM11);

  //addr 1
  testFindName2(&portDriver, 0, 1, asynSuccess, PARAM1);
  testFindName2(&portDriver, 9, 1, asynSuccess, PARAM10);
  testFindName2(&portDriver, 10, 1, asynParamBadIndex, PARAM11);

  //addr 2
  testFindName2(&portDriver, 0, 2, asynSuccess, PARAM1);
  testFindName2(&portDriver, 9, 2, asynSuccess, PARAM10);
  testFindName2(&portDriver, 10, 2, asynParamBadIndex, PARAM11);

  //addr 3 should not exist.
  testFindName2(&portDriver, 0, 3, asynError, PARAM1);
}

MAIN(asynPortDriverTest)
{
  int numOldConstructorTests = 94;

  int totalTests = 0;
  totalTests += numOldConstructorTests;

  testPlan(totalTests);
  testOldConstructor1();
  testDone();
}
