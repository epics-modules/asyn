/*
 * Float64ArrayParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <Float64ArrayParam.h>

Float64ArrayParam::Float64ArrayParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      pArray(0), numElements(0)
{
  type=asynParamFloat64Array;
}

void Float64ArrayParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n",
      getIndex(), getTypeName(), getName(), pArray);
}
