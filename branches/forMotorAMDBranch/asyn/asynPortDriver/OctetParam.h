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
  virtual char* getString(unsigned int maxChars, char *value);
  virtual void setString(const char *value);
  virtual asynStatus callCallback(int addr);

protected:
  bool requestedSizeOK(unsigned int & maxChars);
  virtual void reportDefinedValue(FILE *fp, int details);

private:
  char *sValue;
};
#endif /* OCTETPARAM_H_ */
