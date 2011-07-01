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

OctetParam::OctetParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      sValue(0)
{
  type = asynParamOctet;
}

bool OctetParam::requestedSizeOK(unsigned int & maxChars)
{
    return (maxChars > 0) && (maxChars <= strlen(sValue));
}

char* OctetParam::getString(unsigned int maxChars, char* value){
  if (isValueDefined())
  {
    throw ParamValNotDefined(this);
  }
  if (!requestedSizeOK(maxChars))
  {
    throw ParamValStringSizeRequestTooBig(this);
  }

  value = new char[maxChars];
  strncpy(value, sValue, maxChars-1);

  return value;
}

asynStatus OctetParam::set(const char *value){
  if (!isValueDefined() || strcmp(sValue, value) ){
    free(sValue);
    sValue = epicsStrDup(value);
    markValueIsDefined();
    notifyList();
  }
  return asynSuccess;
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
    retStat = parentList->octetCallback(getIndex(), addr, sValue);
  return retStat;
}
