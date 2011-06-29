/*
 * Int32ArrayParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <Int32ArrayParam.h>

Int32ArrayParam::Int32ArrayParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      pArray(0), numElements(0)
{
  type = asynParamInt32Array;
}

void Int32ArrayParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n", getIndex(),
      getTypeName(), getName(), pArray);
}

