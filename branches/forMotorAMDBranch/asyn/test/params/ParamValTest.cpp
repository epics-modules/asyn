/*************************************************************************\
* Copyright (c) 2011 UChicago Argonne LLC, as Operator of Argonne
 *     National Laboratory.
 * Copyright (c) 2002 The Regents of the University of California, as
 *     Operator of Los Alamos National Laboratory.
 * EPICS BASE is distributed subject to a Software License Agreement found
 * in file LICENSE that is included with this distribution.
 \*************************************************************************/
/*
 * ParamValTest.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */
#include <string.h>
#include <asynPortDriver.h>
#include <ParamVal.h>
#include <ParamValInvalidMethod.h>
#include "epicsUnitTest.h"
#include "testMain.h"

paramList *pList;
ParamVal *param;
const char paramName[] = "myParam";
const char getIntegerMessage[] =
    "ParamVal->getInteger() should throw ParamValInvalidMethod";
const char getDoubleMessage[] =
    "ParamVal->getDouble() should throw ParamValInvalidMethod";
const char getStringMessage[] =
    "ParamVal->getString() should throw ParamValInvalidMethod";
const char getUInt32Message[] =
    "ParamVal->getUInt32() should throw ParamValInvalidMethod";
const char getUInt32InterruptMessage[] =
    "ParamVal->getUInt32Interrupt() should throw ParamValInvalidMethod";
const char setUInt32InterruptMessage[] =
    "ParamVal->setUInt32Interrupt() should throw ParamValInvalidMethod";
const char clearUInt32InterruptMessage[] =
    "ParamVal->clearUInt32Interrupt() should throw ParamValInvalidMethod";
const char setUInt32Message[] =
    "ParamVal->setUInt32() should throw ParamValInvalidMethod";
void setup()
{
  asynStandardInterfaces asynStdInterfaces;
  pList = new paramList(4, &asynStdInterfaces);
  param = new ParamVal(paramName, 4, pList);
}

MAIN(ParamValTest)
{
  int totalTests = 11;
  setup();
  testPlan(totalTests);
  testOk((strcmp(paramName, param->getName()) == 0),
      " Comparing parameter name [expected]:[retrieved] [%s]:[%s]", paramName,
      param->getName());
  testOk((strcmp(ParamVal::typeNames[0], param->getTypeName()) == 0),
      " Comparing parameter Type name [expected]:[retrieved] [%s]:[%s]",
      ParamVal::typeNames[0], param->getTypeName());
  testOk(asynParamTypeUndefined == param->getType(),
      " Comparing parameter Type [expected]:[retrieved] [%d]:[%d]",
      asynParamTypeUndefined, param->getType());
  try
  {
    int temp;
    temp = param->getInteger();
    testFail(getIntegerMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(getIntegerMessage);
  }
  try
  {
    double temp;
    temp = param->getDouble();
    testFail(getDoubleMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(getDoubleMessage);
  }
  try
  {
    epicsUInt32 temp;
    temp = param->getUInt32(0);
    testFail(getUInt32Message);
  } catch (ParamValInvalidMethod&)
  {
    testPass(getUInt32Message);
  }
  try
  {
    char* temp;
    temp = param->getString(57, temp);
    testFail(getStringMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(getStringMessage);
  }
  try
  {
    epicsUInt32 mask;
    mask = param->getUInt32Interrupt(interruptOnZeroToOne);
    testFail(getUInt32InterruptMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(getUInt32InterruptMessage);
  }
  try
  {

    param->setUInt32Interrupt(0,interruptOnZeroToOne);
    testFail(setUInt32InterruptMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(setUInt32InterruptMessage);
  }
  try
  {
    param->clearUInt32Interrupt(0);
    testFail(clearUInt32InterruptMessage);
  } catch (ParamValInvalidMethod&)
  {
    testPass(clearUInt32InterruptMessage);
  }
  try
  {
    param->setUInt32(0, 0);
    testFail(setUInt32Message);
  } catch (ParamValInvalidMethod&)
  {
    testPass(setUInt32Message);
  }


  testDone();
}
