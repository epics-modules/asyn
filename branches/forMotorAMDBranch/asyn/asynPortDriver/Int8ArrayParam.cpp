/*
 * Int8ArrayParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <Int8ArrayParam.h>

Int8ArrayParam::Int8ArrayParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      pArray(0), numElements(0)
{
  type = asynParamInt8Array;
}

void Int8ArrayParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n", getIndex(),
      getTypeName(), getName(), pArray);
}
