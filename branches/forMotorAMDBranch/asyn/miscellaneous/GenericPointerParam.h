/*
 * GenericPointerParam.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef GENERICPOINTERPARAM_H_
#define GENERICPOINTERPARAM_H_

#include <ParamVal.h>

class GenericPointerParam: public ParamVal
{
public:
  GenericPointerParam(const char *name, int index, paramList *parentList);
  virtual void report(FILE *fp, int details);
private:
  void *pValue;
};

#endif /* GENERICPOINTERPARAM_H_ */
