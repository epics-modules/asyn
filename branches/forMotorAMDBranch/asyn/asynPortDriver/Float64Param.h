/*
 * Float64Param.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef FLOAT64PARAM_H_
#define FLOAT64PARAM_H_
#include <ParamVal.h>

/**
 * This class implements the ParamVal interface and stores a
 * float value.  It overrides the get/setDouble methods,
 * as well as callCallback and reportDefined Value.
  */
class Float64Param: public ParamVal
{
public:
  Float64Param(const char *name, int index, paramList *parentList);
  virtual double getDouble();
  virtual void setDouble(double value);
  virtual asynStatus callCallback(int addr);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsFloat64 value;
};
#endif /* FLOAT64PARAM_H_ */
