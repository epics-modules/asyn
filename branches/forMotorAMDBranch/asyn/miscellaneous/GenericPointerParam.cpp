/*
 * GenericPointerParam.cpp
 *
 *  Created on: Jun 16, 2011
 *      Author: hammonds
 */

#include <GenericPointerParam.h>

GenericPointerParam::GenericPointerParam(const char *name, int index, paramList *parentList)
: ParamVal(name, index, parentList),
                      pValue(0)
{
  type = asynParamGenericPointer;
}

void GenericPointerParam::report(FILE *fp, int details)
{
  fprintf(fp, "Parameter %d type=%s, name=%s, value=%p\n",
      getIndex(), getTypeName(), getName(), pValue);
}
