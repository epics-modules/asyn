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
#include "paramVal.h"
#include "paramValWrongType.h"
#include "epicsUnitTest.h"
#include "testMain.h"

//paramList *pList;
ParamVal *intParam, *doubleParam, *uInt32DigitalParam, *stringParam;
const char intParamName[] = "myIntParam";
const char doubleParamName[] = "myDoubleParam";
const char uInt32DigitalParamName[] = "myUInt32DigitalParam";
const char stringParamName[] = "myStringParam";
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
  intParam = new ParamVal(intParamName, asynParamInt32);
  doubleParam = new ParamVal(doubleParamName, asynParamFloat64);
  uInt32DigitalParam = new ParamVal(uInt32DigitalParamName, asynParamUInt32Digital);
  stringParam = new ParamVal(stringParamName, asynParamOctet);
}

void catchWrongTypeTestsInitialPass(ParamVal *param, const char* fcnName, ParamValWrongType *e ){
	  testPass("Should not be able to %s on %s\n   %s",
			  fcnName, param->getTypeName(), e->what());
	  testOk(!param->isDefined(), "%s value should be undefined after initial %s",
			  param->getName(), fcnName);
	  testOk(!param->hasValueChanged(), "%s value should report value "
			  "not changed after initial %s", param->getName(), fcnName);
}

void catchWrongTypeTestsSecondPass(ParamVal *param, const char* fcnName, ParamValWrongType *e ){
	  testPass("Should not be able to %s on %s\n   %s",
			  fcnName, param->getTypeName(), e->what());
	  testOk(param->isDefined(), "%s value should be defined after second %s",
			 param->getName(), fcnName);
	  testOk(!param->hasValueChanged(), "%s value should report value "
			  "not Changed after second %s",
			  param->getName(), fcnName);
}

void testFailingSetDouble(ParamVal *param, int pass){
    try {
  	  param->setDouble(0.0);
  	  testFail("Should not be able to setDouble on %s", param->getTypeName());
    }
    catch (ParamValWrongType& e){
    	if (pass == 0) {
    		catchWrongTypeTestsInitialPass(param, "setDouble", &e );
    	}
    	else {
    		catchWrongTypeTestsSecondPass(param, "setDouble", &e );
    	}
    }

}

void testFailingSetUint32(ParamVal *param, int pass){
    try {
  	  param->setUInt32(0,0,0);
  	  testFail("Should not be able to setUInt32 on asynParamInt32");
    }
    catch (ParamValWrongType& e){
    	if (pass == 0){
    		catchWrongTypeTestsInitialPass(param, "setUInt32", &e );
    	}
    	else {
    		catchWrongTypeTestsSecondPass(param, "setUInt32", &e );
    	}
    }
}

void testFailingSetInteger(ParamVal *param, int pass){
    try {
  	  param->setInteger(0);
  	  testFail("Should not be able to setInteger on asynParamFloat64");
    }
    catch (ParamValWrongType& e){
    	if (pass == 0){
    		catchWrongTypeTestsInitialPass(param, "setInteger", &e);
    	}
    	else {
    		catchWrongTypeTestsSecondPass(param, "setInteger", &e);
    	}
    }

}

void testFailingSetString(ParamVal *param, int pass){
    try {
  	  param->setString("hello");
  	  testFail("Should not be able to setString on asynParamInt32");
    }
    catch (ParamValWrongType& e){
    	if (pass == 0){
    		catchWrongTypeTestsInitialPass(param, "setString", &e );
    	}
    	else{
    		catchWrongTypeTestsSecondPass(param, "setString", &e );
    	}
    }
}

MAIN(ParamValTest)
{
  int totalTests = 132;
  setup();
  testPlan(totalTests);


/**
 * Make sure that the test classes got set up correctly
 */
  testOk((strcmp(intParamName, intParam->getName()) == 0),
      " Comparing parameter name [expected]:[retrieved] [%s]:[%s]", intParamName,
      intParam->getName());
  testOk( intParam->type == asynParamInt32, "Check intParam correct type");
  testOk((strcmp(doubleParamName, doubleParam->getName()) == 0),
      " Comparing parameter name [expected]:[retrieved] [%s]:[%s]", doubleParamName,
      doubleParam->getName());
  testOk( doubleParam->type == asynParamFloat64, "Check doubleParam correct type");
  testOk((strcmp(uInt32DigitalParamName, uInt32DigitalParam->getName()) == 0),
      " Comparing parameter name [expected]:[retrieved] [%s]:[%s]", uInt32DigitalParamName,
      uInt32DigitalParam->getName());
  testOk(uInt32DigitalParam->type == asynParamUInt32Digital, "Check uInt32DigitalParam correct type");
  testOk((strcmp(stringParamName, stringParam->getName()) == 0),
      " Comparing parameter name [expected]:[retrieved] [%s]:[%s]", stringParamName,
      stringParam->getName());
  testOk( stringParam->type == asynParamOctet, "Check stringParam correct type");

  /** Exercise intParam
   *
   */
    testOk(!(intParam->isDefined()),
  		  "intParam should be undefined to start");
    testOk(!intParam->hasValueChanged(),
  		  "intParam should start unchanged");

    // Try to set each type only Integer should pass.  We will do this last
    // so that the this stays undefined and unchanged until then.
    testFailingSetDouble(intParam, 0);
    testFailingSetUint32(intParam, 0);
    testFailingSetString(intParam, 0);
    try {
  	  intParam->setInteger(1);
  	  testPass("Should be able to setInteger on asynParamInt32");
  	  testOk(intParam->isDefined(), "intParam value should be defined after setInteger");
  	  testOk(intParam->hasValueChanged(), "intParam value should report valueChanged after setInteger");
  	  intParam->resetValueChanged();
  	  testOk(!intParam->hasValueChanged(), "intParam value should report value not Changed after resetValueChange");
  	  testOk(intParam->isDefined(), "intParam value should still be defined after resetValueChanged");
    }
    catch (ParamValWrongType& e){
  	  testFail("Should be able to setInteger on asynParamInt32\n   %s", e.what());
    }

    // Repeat each type.  The fact that our parameter is now defined should
    // not change the fact that only set integer should work.  This time
    // around isDefined should remain true.  hasValueChanged should only
    // be true when setting a new integer value.
    testFailingSetDouble(intParam, 1);
    testFailingSetUint32(intParam, 1);
    testFailingSetString(intParam, 1);
    try {
  	  intParam->setInteger(1);
  	  testPass("Should be able to setInteger on asynParamInt32");
  	  testOk(intParam->isDefined(), "intParam value should be defined after "
  			  "setInteger");
  	  testOk(!intParam->hasValueChanged(), "intParam value should report "
  			  "value not changed after setInteger to same value");
  	  intParam->setInteger(34);
  	  testPass("Should be able to setInteger on asynParamInt32");
  	  testOk(intParam->hasValueChanged(), "intParam value should report "
  			  "valueChanged after setInteger to new value");
  	  intParam->resetValueChanged();
  	  testOk(!intParam->hasValueChanged(), "intParam value should report value not Changed after resetValueChange");
    }
    catch (ParamValWrongType& e){
  	  testFail("Should be able to setInteger on asynParamInt32\n   %s", e.what());
    }

    /** Exercise doubleParam
     *
     */
      testOk(!(doubleParam->isDefined()),
    		  "doubleParam should be undefined to start");
      testOk(!intParam->hasValueChanged(),
    		  "doubleParam should start unchanged");

      // Try to set each type only setDouble should pass.  We will do this last
      // so that the this stays undefined and unchanged until then.
      testFailingSetInteger(doubleParam, 0);
      testFailingSetUint32(doubleParam, 0);
      testFailingSetString(doubleParam, 0);
      try {
    	  doubleParam->setDouble(1.0);
    	  testPass("Should be able to setDouble on asynParamFloat64");
    	  testOk(doubleParam->isDefined(), "doubleParam value should be defined after setDouble");
    	  testOk(doubleParam->hasValueChanged(), "doubleParam value should report valueChanged after setDouble");
    	  doubleParam->resetValueChanged();
    	  testOk(!doubleParam->hasValueChanged(), "doubleParam value should report value not Changed after resetValueChange");
    	  testOk(doubleParam->isDefined(), "doubleParam value should still be defined after resetValueChanged");
      }
      catch (ParamValWrongType& e){
    	  testFail("Should be able to setInteger on asynParamFloat64\n   %s", e.what());
      }

      // Repeat each type.  The fact that our parameter is now defined should
      // not change the fact that only set integer should work.  This time
      // around isDefined should remain true.  hasValueChanged should only
      // be true when setting a new integer value.
      testFailingSetInteger(doubleParam, 1);
      testFailingSetUint32(doubleParam, 1);
      testFailingSetString(doubleParam, 1);
      try {
    	  doubleParam->setDouble(1.0);
    	  testPass("Should be able to setDouble on asynParamFloat64");
    	  testOk(doubleParam->isDefined(), "doubleParam value should be defined after "
    			  "setDouble");
    	  testOk(!doubleParam->hasValueChanged(), "doubleParam value should report "
    			  "value not changed after setDouble to same value");
    	  doubleParam->setDouble(34.0);
    	  testPass("Should be able to setDouble on asynParamFloat64");
    	  testOk(doubleParam->hasValueChanged(), "floatParam value should report "
    			  "valueChanged after setDouble to new value");
    	  doubleParam->resetValueChanged();
    	  testOk(!doubleParam->hasValueChanged(), "doubleParam value should report "
    			  "value not Changed after resetValueChange");
      }
      catch (ParamValWrongType& e){
    	  testFail("Should be able to setDouble on asynParamFloat64\n   %s", e.what());
      }

      /** Exercise stringParam
       *
       */
        testOk(!(stringParam->isDefined()),
      		  "stringParam should be undefined to start");
        testOk(!intParam->hasValueChanged(),
      		  "stringParam should start unchanged");

        // Try to set each type only setDouble should pass.  We will do this last
        // so that the this stays undefined and unchanged until then.
        testFailingSetInteger(stringParam, 0);
        testFailingSetUint32(stringParam, 0);
        testFailingSetDouble(stringParam, 0);
        try {
      	  stringParam->setString("hello");
      	  testPass("Should be able to setString on asynParamOctet");
      	  testOk(stringParam->isDefined(), "stringParam value should be defined after setString");
      	  testOk(stringParam->hasValueChanged(), "stringParam value should report valueChanged after setString");
      	  stringParam->resetValueChanged();
      	  testOk(!stringParam->hasValueChanged(), "stringParam value should report value not Changed after resetValueChange");
      	  testOk(stringParam->isDefined(), "stringParam value should still be defined after resetValueChanged");
        }
        catch (ParamValWrongType& e){
      	  testFail("Should be able to setString on asynParamOctet\n   %s", e.what());
        }

        // Repeat each type.  The fact that our parameter is now defined should
        // not change the fact that only set integer should work.  This time
        // around isDefined should remain true.  hasValueChanged should only
        // be true when setting a new integer value.
        testFailingSetInteger(stringParam, 1);
        testFailingSetUint32(stringParam, 1);
        testFailingSetDouble(stringParam, 1);
        try {
      	  stringParam->setString("hello");
      	  testPass("Should be able to setString on asynParamOctet");
      	  testOk(stringParam->isDefined(), "stringParam value should be defined after "
      			  "setString");
      	  testOk(!stringParam->hasValueChanged(), "stringParam value should report "
      			  "value not changed after setString to same value");
      	  stringParam->setString("goodbye");
      	  testPass("Should be able to setString on asynParamOctet");
      	  testOk(stringParam->hasValueChanged(), "stringParam value should report "
      			  "valueChanged after setString to new value");
      	  stringParam->resetValueChanged();
      	  testOk(!stringParam->hasValueChanged(), "stringParam value should report "
      			  "value not Changed after resetValueChange");
        }
        catch (ParamValWrongType& e){
      	  testFail("Should be able to setString on asynParamOctet\n   %s", e.what());
        }

        /** Exercise uInt32DigitalParam
         *
         */
          testOk(!(uInt32DigitalParam->isDefined()),
        		  "uInt32DigitalParam should be undefined to start");
          testOk(!intParam->hasValueChanged(),
        		  "uInt32DigitalParam should start unchanged");

          // Try to set each type only setDouble should pass.  We will do this last
          // so that the this stays undefined and unchanged until then.
          testFailingSetInteger(uInt32DigitalParam, 0);
          testFailingSetDouble(uInt32DigitalParam, 0);
          try {
        	  uInt32DigitalParam->setString("hello");
        	  testFail("Should not be able to setString on asynParamFloat64");
          }
          catch (ParamValWrongType& e){
        	  catchWrongTypeTestsInitialPass(uInt32DigitalParam, "setString", &e);
          }
          try {
        	  uInt32DigitalParam->setUInt32(1,0xffff,0);
        	  testPass("Should be able to setUInt32 on asynParamUInt32Digital");
        	  testOk(uInt32DigitalParam->isDefined(), "uInt32DigitalParam value should be defined after setUInt32");
        	  testOk(uInt32DigitalParam->hasValueChanged(), "uInt32DigitalParam value should report valueChanged after setUInt32");
        	  uInt32DigitalParam->resetValueChanged();
        	  testOk(!uInt32DigitalParam->hasValueChanged(), "uInt32DigitalParam value should report value not Changed after resetValueChange");
        	  testOk(uInt32DigitalParam->isDefined(), "uInt32DigitalParam value should still be defined after resetValueChanged");
          }
          catch (ParamValWrongType& e){
        	  testFail("Should be able to setInteger on asynParamFloat64\n   %s", e.what());
          }

          // Repeat each type.  The fact that our parameter is now defined should
          // not change the fact that only set integer should work.  This time
          // around isDefined should remain true.  hasValueChanged should only
          // be true when setting a new integer value.
          testFailingSetInteger(uInt32DigitalParam, 1);
          testFailingSetDouble(uInt32DigitalParam, 1);
          try {
        	  uInt32DigitalParam->setString("hello");
        	  testFail("Should not be able to setString on asynParamFloat64");
          }
          catch (ParamValWrongType& e){
              catchWrongTypeTestsSecondPass(uInt32DigitalParam, "setString", &e );
          }
          try {
        	  uInt32DigitalParam->setUInt32(1, 0, 0);
        	  testPass("Should be able to setUInt32 on asynParamUInt32Digital");
        	  testOk(uInt32DigitalParam->isDefined(), "uInt32DigitalParam value should be defined after "
        			  "setUInt32");
        	  testOk(!uInt32DigitalParam->hasValueChanged(), "uInt32DigitalParam value should report "
        			  "value not changed after setUInt32 to same value");
        	  uInt32DigitalParam->setUInt32(5, 0xffff, 0);
        	  testPass("Should be able to setUInt32 on asynParamUInt32Digital");
        	  testOk(uInt32DigitalParam->hasValueChanged(), "floatParam value should report "
        			  "valueChanged after setUInt32 to new value");
        	  uInt32DigitalParam->resetValueChanged();
        	  testOk(!uInt32DigitalParam->hasValueChanged(), "uInt32DigitalParam value should report "
        			  "value not Changed after resetValueChange");
          }
          catch (ParamValWrongType& e){
        	  testFail("Should be able to setDouble on asynParamFloat64\n   %s", e.what());
          }



  testDone();
}
