/*
 * Int16ArrayParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef INT16ARRAYPARAM_H_
#define INT16ARRAYPARAM_H_
#include <ParamVal.h>

class Int16ArrayParam: public ParamVal
{
public:
  Int16ArrayParam(const char *name, int index, paramList *parentList);
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsInt16 *pArray;
  int numElements;
};
#endif /* INT16ARRAYPARAM_H_ */
