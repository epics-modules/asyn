/*
 * Int32Array.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef INT32ARRAYPARAM_H_
#define INT32ARRAYPARAM_H_
#include <ParamVal.h>

class Int32ArrayParam: public ParamVal
{
public:
  Int32ArrayParam(const char *name, int index, paramList *parentList);
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsInt32 *pArray;
  int numElements;
};
#endif /* INT32ARRAYPARAM_H_ */
