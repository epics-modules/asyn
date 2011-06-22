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

OctetParam::OctetParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      sValue(0)
{
  type = asynParamInt32Array;
}

asynStatus OctetParam::get(unsigned int maxChars, char *value){
  asynStatus retVal = asynParamUndefined;
  if (isValueDefined())
  {
    if ((maxChars >0) && (maxChars <= strlen(sValue)))
    {
      strncpy(value, sValue, maxChars-1);
      retVal = asynSuccess;
    }
    else
    {
      retVal = asynError;
    }
  }
  return retVal;
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
