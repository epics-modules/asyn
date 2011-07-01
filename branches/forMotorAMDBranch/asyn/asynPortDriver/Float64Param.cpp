/*
 * Float64Param.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <Float64Param.h>
#include <asynPortDriver.h>
#include <ParamValNotDefined.h>
#include <ParamListCallbackError.h>

Float64Param::Float64Param(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                         value(0.0)
{
  type = asynParamFloat64;
}

/** Get the value of the parameter as an double
 * \param[out] the value of the parameter
 * \return Returns asynParamUndefined if the value of the parameter has
 * never been set, otherwise returns asynSuccess.
 */
double Float64Param::getDouble()
{
  if (!isValueDefined())
  {
    throw ParamValNotDefined(this);
  }
  return this->value;
}

/** sets the double value.  Also marks the value as defined.
 * \param[in] value The new value to store in the parameter
 * \return asynSuccess
 *
 */
void Float64Param::setDouble(double value)
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

void Float64Param::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%f\n", getIndex(),
      getTypeName(), getName(), value);
}

asynStatus Float64Param::callCallback(int addr)
{
  asynStatus retStat = asynSuccess;
  if(isValueDefined())
    retStat = parentList->float64Callback(getIndex(), addr, value);
  return retStat;
}

