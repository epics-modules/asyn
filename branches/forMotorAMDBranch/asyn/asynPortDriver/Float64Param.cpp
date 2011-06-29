/*
 * Float64Param.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <Float64Param.h>
#include <asynPortDriver.h>

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
asynStatus Float64Param::get(double *value)
{
  asynStatus retStat = asynSuccess;
  if (isValueDefined())
  {
    *value = this->value;
    retStat = asynSuccess;
  }
  else
  {
    retStat = asynParamUndefined;
  }
  return retStat;
}

/** sets the double value.  Also marks the value as defined.
 * \param[in] value The new value to store in the parameter
 * \return asynSuccess
 *
 */
asynStatus Float64Param::set(double value)
{
  this->value = value;
  markValueIsDefined();
  notifyList();
  return asynSuccess;
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

