/*
 * Float64Param.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef FLOAT64PARAM_H_
#define FLOAT64PARAM_H_
#include <ParamVal.h>

class Float64Param: public ParamVal
{
public:
  Float64Param(const char *name, int index, paramList *parentList);
  virtual asynStatus get(double *value);
  virtual asynStatus set(double value);
  virtual void report(FILE *fp, int details);
  virtual asynStatus callCallback(int addr);

private:
  epicsFloat64 value;
};
#endif /* FLOAT64PARAM_H_ */
