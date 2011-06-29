/*
 * Float32ArrayParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */
#include <Float32ArrayParam.h>

Float32ArrayParam::Float32ArrayParam(const char *name, int index, paramList *parentList) :
ParamVal(name, index, parentList),
                     pArray(0), numElements(0)
{
  type = asynParamFloat32Array;
}

void Float32ArrayParam::reportDefinedValue(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n",
      getIndex(), getTypeName(), getName(), pArray);
}
