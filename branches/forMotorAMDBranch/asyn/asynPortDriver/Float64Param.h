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
  virtual double getDouble();
  virtual void setDouble(double value);
  virtual asynStatus callCallback(int addr);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsFloat64 value;
};
#endif /* FLOAT64PARAM_H_ */
