/*
 * Int32Param.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef INT32PARAM_H_
#define INT32PARAM_H_
#include <ParamVal.h>

class Int32Param: public ParamVal
{
public:
  Int32Param(const char *name, int index, paramList *parentList);
  virtual asynStatus get(int *value);
  virtual asynStatus set(int value);
  virtual void reportDefinedValue(FILE *fp, int details);
  virtual asynStatus callCallback(int addr);

private:
  epicsInt32 value;
};
#endif /* INT32PARAM_H_ */
