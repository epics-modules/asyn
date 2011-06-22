/*
 * Float64ArrayParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef FLOAT64ARRAYPARAM_H_
#define FLOAT64ARRAYPARAM_H_
#include <ParamVal.h>

class Float64ArrayParam: public ParamVal
{
public:
  Float64ArrayParam(const char *name, int index, paramList *parentList);
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsFloat64 *pArray;
  int numElements;
};

#endif /* FLOAT64ARRAYPARAM_H_ */
