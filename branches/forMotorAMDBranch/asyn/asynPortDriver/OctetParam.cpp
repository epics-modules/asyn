/*
 * OctetParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */
#include <stdlib.h>
#include <string.h>
#include <epicsString.h>
#include <asynPortDriver.h>
#include <OctetParam.h>
#include <ParamValStringSizeRequestTooBig.h>
#include <ParamValNotDefined.h>
#include <ParamListCallbackError.h>
#include <OctetCallback.h>

OctetParam::OctetParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      sValue(0)
{
  type = asynParamOctet;
}

bool OctetParam::requestedSizeOK(unsigned int & maxChars, char *value)
{
    return (maxChars > 0);
}


char* OctetParam::getString(unsigned int maxChars, char* value){
  if (!isValueDefined())
  {
    throw ParamValNotDefined(this);
  }
  if (!requestedSizeOK(maxChars, value))
  {
    throw ParamValStringSizeRequestTooBig(this);
  }

//  value = new char[maxChars];
  strncpy(value, sValue, maxChars-1);

  return value;
}

void OctetParam::setString(const char *value){
  if (!isValueDefined() || strcmp(sValue, value) ){
    free(sValue);
    sValue = epicsStrDup(value);
    markValueIsDefined();
    try
    {
      notifyList() ;
    }
    catch (ParamListCallbackError ex)
    {
      throw ex;
    }
  }
}

void OctetParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%s\n", getIndex(),
      getTypeName(), getName(), sValue);
}

asynStatus OctetParam::callCallback(int addr)
{
  asynStatus retStat = asynSuccess;
  if(isValueDefined())
  {
    //retStat = parentList->octetCallback(getIndex(), addr, sValue);
		OctetCallback<char*, asynOctetInterrupt> callback(getIndex(), addr,
				(void *)parentList->standardInterfaces()->octetInterruptPvt, sValue);
		callback.doCallback();

  }
  return retStat;
}
