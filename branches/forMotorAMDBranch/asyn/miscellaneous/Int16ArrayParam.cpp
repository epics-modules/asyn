/*
 * Int16ArrayParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */
#include <Int16ArrayParam.h>

Int16ArrayParam::Int16ArrayParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      pArray(0), numElements(0)
{
  type = asynParamInt16Array;
}

void Int16ArrayParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n", getIndex(),
      getTypeName(), getName(), pArray);
}

