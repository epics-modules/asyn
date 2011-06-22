/*
 * Float32ArrayParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef FLOAT32ARRAYPARAM_H_
#define FLOAT32ARRAYPARAM_H_
#include <ParamVal.h>

class Float32ArrayParam: public ParamVal
{
public:
  Float32ArrayParam(const char *name, int index, paramList *parentList);
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsFloat32 *pArray;
  int numElements;
};


#endif /* FLOAT32ARRAYPARAM_H_ */
