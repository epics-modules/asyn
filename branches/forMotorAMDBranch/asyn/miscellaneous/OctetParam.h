/*
 * OctetParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef OCTETPARAM_H_
#define OCTETPARAM_H_
#include <ParamVal.h>

class OctetParam: public ParamVal
{
public:
  OctetParam(const char *name, int index, paramList *parentList);
  virtual asynStatus get(unsigned int maxChars, char *value);
  virtual asynStatus set(const char *value);
  virtual asynStatus callCallback(int addr);

protected:
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  char *sValue;
};
#endif /* OCTETPARAM_H_ */
