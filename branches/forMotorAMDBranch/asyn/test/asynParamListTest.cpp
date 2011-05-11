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
#include <string.h>

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

void testCreateParam(const char *paramName, paramList *pList,
    asynParamType pType, asynStatus expResult, int expIndex)
{
  int index = -999;
  asynStatus status;
  status = pList->createParam(paramName, pType, &index);

  bool result = (status == expResult);
  testOk(result, "Create param %s", paramName);
  if (result)
    testOk(index == expIndex, " Parameter index %d expected %d", index,
        expIndex);
}

void testCreateParams(paramList *pList)
{
  testCreateParam(PARAM1, pList, asynParamInt32, asynSuccess, 0);
  testCreateParam(PARAM1, pList, asynParamInt32, asynParamAlreadyExists, 0);
  testCreateParam(PARAM2, pList, asynParamUInt32Digital, asynSuccess, 1);
  testCreateParam(PARAM3, pList, asynParamFloat64, asynSuccess, 2);
  testCreateParam(PARAM4, pList, asynParamOctet, asynSuccess, 3);
  testCreateParam(PARAM5, pList, asynParamInt8Array, asynSuccess, 4);
  testCreateParam(PARAM6, pList, asynParamInt16Array, asynSuccess, 5);
  testCreateParam(PARAM7, pList, asynParamInt32Array, asynSuccess, 6);
  testCreateParam(PARAM8, pList, asynParamFloat32Array, asynSuccess, 7);
  testCreateParam(PARAM9, pList, asynParamFloat64Array, asynSuccess, 8);
  testCreateParam(PARAM10, pList, asynParamGenericPointer, asynSuccess, 9);
  testCreateParam(PARAM11, pList, asynParamGenericPointer, asynParamBadIndex,
      10);
  testCreateParam(PARAM11, pList, asynParamGenericPointer, asynParamBadIndex,
      10);
}

void tesstFindParams(const char *paramName, paramList *pList,
    asynStatus expStatus, int expIndex)
{
  asynStatus status;
  int index = -999;
  status = pList->findParam(paramName, &index);
  testOk(status == expStatus, "Trying to find parameter %s", paramName);
  if (status == expStatus)
    testOk(index == expIndex, "Comparing index and expected index %d, %d",
        index, expIndex);

}

void testFindParams(paramList *pList)
{
  tesstFindParams(PARAM1, pList, asynSuccess, 0);
  tesstFindParams(PARAM2, pList, asynSuccess, 1);
  tesstFindParams(PARAM3, pList, asynSuccess, 2);
  tesstFindParams(PARAM4, pList, asynSuccess, 3);
  tesstFindParams(PARAM5, pList, asynSuccess, 4);
  tesstFindParams(PARAM6, pList, asynSuccess, 5);
  tesstFindParams(PARAM7, pList, asynSuccess, 6);
  tesstFindParams(PARAM8, pList, asynSuccess, 7);
  tesstFindParams(PARAM9, pList, asynSuccess, 8);
  tesstFindParams(PARAM10, pList, asynSuccess, 9);
  tesstFindParams(PARAM11, pList, asynParamNotFound, 0);
}

void testGetName(int index, paramList *pList, asynStatus expStatus,
    const char *expName)
{
  asynStatus status;
  const char *value;
  status = pList->getName(index, &value);
  testOk(status == expStatus, "Testing getName for index %d", index);
  if (status == asynSuccess)
    testOk((strcmp(value, expName) == 0),
        "  Comparing retrieved:expected %s:%s", value, expName);

}

void testGetNames(paramList *pList)
{
  testGetName(0, pList, asynSuccess, PARAM1);
  testGetName(1, pList, asynSuccess, PARAM2);
  testGetName(2, pList, asynSuccess, PARAM3);
  testGetName(3, pList, asynSuccess, PARAM4);
  testGetName(4, pList, asynSuccess, PARAM5);
  testGetName(5, pList, asynSuccess, PARAM6);
  testGetName(6, pList, asynSuccess, PARAM7);
  testGetName(7, pList, asynSuccess, PARAM8);
  testGetName(8, pList, asynSuccess, PARAM9);
  testGetName(9, pList, asynSuccess, PARAM10);
  testGetName(10, pList, asynParamBadIndex, PARAM1);
}

void testSetInteger(int index, int value, paramList *pList,
    asynStatus expStatus)
{
  asynStatus status;
  status = pList->setInteger(index, value);
  testOk(status == expStatus, "Test setInteger for param %d", index);
  if (expStatus == asynSuccess && status == expStatus)
  {
    int retVal;
    status == pList->getInteger(index, &retVal);
    testOk(status == asynSuccess, "Check status on getInteger for param %d",
        index);
    if (status == asynSuccess)
    {
      testOk(retVal == value,
          "Checking returned:expected Value for param %d, %d:%d", index,
          retVal, value);
    }
  }
}

void testSetIntegers(paramList *pList)
{
  testSetInteger(0, 5, pList, asynSuccess);
  testSetInteger(0, -5, pList, asynSuccess);
  testSetInteger(0, 0, pList, asynSuccess);
  testSetInteger(0, -2147483648, pList, asynSuccess);
  testSetInteger(0, 2147483647, pList, asynSuccess);
  testSetInteger(1, 5, pList, asynParamWrongType);
  testSetInteger(2, 5, pList, asynParamWrongType);
  testSetInteger(3, 5, pList, asynParamWrongType);
  testSetInteger(4, 5, pList, asynParamWrongType);
  testSetInteger(5, 5, pList, asynParamWrongType);
  testSetInteger(6, 5, pList, asynParamWrongType);
  testSetInteger(7, 5, pList, asynParamWrongType);
  testSetInteger(8, 5, pList, asynParamWrongType);
  testSetInteger(9, 5, pList, asynParamWrongType);
  testSetInteger(10, 5, pList, asynParamBadIndex);
}

MAIN(asynParamListTest)
{
  int numCreateTests = 22;
  int numFindTests = 22;
  int numGetNameTests = 22;
  int numSetIntegerTests = 25;

  int totalTests = 0;
  totalTests += numCreateTests;
  totalTests += numFindTests;
  totalTests += numGetNameTests;
  totalTests += numSetIntegerTests;

  testPlan( totalTests);
  asynStandardInterfaces asynStdInterfaces;
  int nVals = 10;
  paramList pList(nVals, &asynStdInterfaces);
  testCreateParams(&pList);
  testFindParams(&pList);
  testGetNames(&pList);
  testSetIntegers(&pList);
  testDone();
}
