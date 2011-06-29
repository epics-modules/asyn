/*
 * Int8ArrayParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef INT8ARRAYPARAM_H_
#define INT8ARRAYPARAM_H_
#include <ParamVal.h>

class Int8ArrayParam: public ParamVal
{
public:
  Int8ArrayParam(const char *name, int index, paramList *parentList);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsInt32 *pArray;
  int numElements;
};
#endif /* INT8ARRAYPARAM_H_ */
