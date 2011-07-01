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
  virtual int getInteger();
  virtual void setInteger(int value);
  virtual asynStatus callCallback(int addr);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  epicsInt32 value;
};
#endif /* INT32PARAM_H_ */
