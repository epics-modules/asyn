/*
 * Int32Param.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */
#include <Int32Param.h>
#include <asynPortDriver.h>
#include <ParamValNotDefined.h>
#include <ParamListCallbackError.h>

Int32Param::Int32Param(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                         value(0)
{
  type = asynParamInt32;
}

/** Get the value of the parameter as an integer
 * \param[out] the value of the parameter
 * \return Returns asynParamUndefined if the value of the parameter has
 * never been set, otherwise returns asynSuccess.
 */
int Int32Param::getInteger()
{
  if (!isValueDefined())
  {
    throw ParamValNotDefined(this);
  }
  return value;
}

/** sets the integer value.  Also marks the value as defined.
 * \param[in] value The new value to store in the parameter
 * \return asynSuccess
 *
 */
void Int32Param::setInteger(int value)
{
 this->value = value;
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

void Int32Param::reportDefinedValue(FILE *fp, int details)
{
    fprintf(fp, "Parameter %d type=%s, name=%s, value=%d\n", getIndex(),
        getTypeName(), getName(), value);
}

asynStatus Int32Param::callCallback(int addr)
{
  asynStatus retStat = asynSuccess;
  if ( isValueDefined())
    retStat = parentList->int32Callback(getIndex(), addr, value);
  return retStat;
}
