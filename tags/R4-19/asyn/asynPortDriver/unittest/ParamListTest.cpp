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
#include "ParamVal.h"
#include "ParamListInvalidIndex.h"
#include "ParamValInvalidMethod.h"
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
const char paramListPassMessage[] =
    "ParamList::%s for index %d should have passed";
const char paramListFailMessage[] =
    "ParamList::%s for index %d should have failed";
const char paramListFailWrongReasonMessage[] =
    "ParamList::%s for index %d should have failed, "
      "but failed for the wrong reason";

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

void testGetParam(paramList *pList, int index, bool expectToPass)
{
  const char getParamString[] = "getParam";
  if (expectToPass)
  {
    try
    {
      pList->getParam(index);
      testPass(paramListPassMessage, getParamString, index);
    } catch (ParamListInvalidIndex&)
    {
      testFail(paramListPassMessage, getParamString, index);
    }
  }
  else
  {
    try
    {
      pList->getParam(index);
      testFail(paramListFailMessage, getParamString, index);
    } catch (ParamListInvalidIndex&)
    {
      testPass(paramListFailMessage, getParamString, index);
    }
  }
}

void testGetParams(paramList *pList)
{
  testGetParam(pList, 0, true);
  testGetParam(pList, 1, true);
  testGetParam(pList, 2, true);
  testGetParam(pList, 3, true);
  testGetParam(pList, 4, true);
  testGetParam(pList, 5, true);
  testGetParam(pList, 6, true);
  testGetParam(pList, 7, true);
  testGetParam(pList, 8, true);
  testGetParam(pList, 9, true);
  testGetParam(pList, 10, false);
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

void testTryFailureMode(bool expectToPass, int expectedReason, int reason,
    const char *method, int index)
{
  if (!expectToPass && (reason == expectedReason))
  {
    testPass(paramListFailMessage, method, index);
  }
  else
  {
    if (reason == expectedReason)
    {
      testFail(paramListFailMessage, method, index);
    }
    else
    {
      testFail(paramListFailWrongReasonMessage, method, index);
    }

  }
}
void testSetInteger(int index, int value, paramList *pList, bool expectToPass,
    int expectedReason)
{
  asynStatus status;
  const char setIntegerString[] = "(ParamVal->setInteger)";
  const char getIntegerString[] = "(ParamVal->getInteger)";
  bool passedSet = false;
  try
  {
    pList->getParam(index)->setInteger(value);
    if (expectToPass)
    {
      testPass(paramListPassMessage, setIntegerString, index);
      passedSet = true;
    }
    else
    {
      testFail(paramListPassMessage, setIntegerString, index);
    }
  }
  catch (ParamListInvalidIndex&)
  {
    int reason = 2;
    testTryFailureMode(expectToPass, expectedReason, reason, setIntegerString,
        index);
  }
  catch (ParamValInvalidMethod&)
  {
    int reason = 1;
    testTryFailureMode(expectToPass, expectedReason, reason, setIntegerString,
        index);
  }

  if (expectToPass && passedSet)
  {
    int retVal = 32;
    try
    {
      retVal = pList->getParam(index)->getInteger();
      testPass(paramListPassMessage, getIntegerString, index);
      testOk(retVal == value,
          "Checking returned:expected Value for param %d, %d:%d",
          index, retVal, value);
    }
    catch (ParamListInvalidIndex&)
    {
      int reason = 2;
      testTryFailureMode(expectToPass, expectedReason, reason,
          setIntegerString, index);
    }
    catch (ParamValInvalidMethod&)
    {
      int reason = 1;
      testTryFailureMode(expectToPass, expectedReason, reason,
          setIntegerString, index);
    }
  }
}

void testSetIntegers(paramList *pList)
{
  testSetInteger(0, 5, pList, true, 0);
  testSetInteger(0, -5, pList, true, 0);
  testSetInteger(0, 0, pList, true, 0);
  testSetInteger(0, -2147483648, pList, true, 0);
  testSetInteger(0, 2147483647, pList, true, 0);
  testSetInteger(1, 5, pList, false, 1);
  testSetInteger(2, 5, pList, false, 1);
  testSetInteger(3, 5, pList, false, 1);
  testSetInteger(4, 5, pList, false, 1);
  testSetInteger(5, 5, pList, false, 1);
  testSetInteger(6, 5, pList, false, 1);
  testSetInteger(7, 5, pList, false, 1);
  testSetInteger(8, 5, pList, false, 1);
  testSetInteger(9, 5, pList, false, 1);
  testSetInteger(10, 5, pList, false, 2);
}

MAIN(asynParamListTest)
{
  int numCreateTests = 26;
  int numGetParamsTests = 11;
  int numFindTests = 22;
  int numGetNameTests = 21;
  int numSetIntegerTests = 25;

  int totalTests = 0;
  totalTests += numCreateTests;
  totalTests += numGetParamsTests;
  totalTests += numFindTests;
  totalTests += numGetNameTests;
  totalTests += numSetIntegerTests;

  asynStandardInterfaces asynStdInterfaces;
  int nVals = 10;
  paramList pList(nVals, &asynStdInterfaces);

  testPlan(totalTests);
  testCreateParams(&pList);
  testGetParams(&pList);
  testFindParams(&pList);
  testGetNames(&pList);
  testSetIntegers(&pList);
  testDone();
}
