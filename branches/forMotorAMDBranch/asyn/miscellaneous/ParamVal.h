/*
 * asynParameter.h
 *
 *  Created on: Jun 15, 2011
 *      Author: hammonds
 */

#ifndef ASYNPARAMETER_H_
#define ASYNPARAMETER_H_
#include <asynStandardInterfaces.h>
#include <asynParamTypes.h>
#include <asynStandardInterfaces.h>
class paramList;

class ParamVal
{
public:
  ParamVal(const char *name, int index, paramList *parent);
  ParamVal();
  virtual ~ParamVal();
  char* getName();
  asynParamType getType();
  virtual asynStatus get(double *value);
  virtual asynStatus get(int *value);
  virtual asynStatus get(unsigned int maxChars, char *value);
  virtual asynStatus get(epicsUInt32 *value, epicsUInt32 mask);
  virtual asynStatus getUInt32Interrupt(epicsUInt32 *mask, interruptReason reason);
  virtual asynStatus set(double value);
  virtual asynStatus set(int value);
  virtual asynStatus set(const char *string);
  virtual asynStatus set(epicsUInt32 value, epicsUInt32 mask);
  virtual asynStatus setUInt32Interrupt(epicsUInt32 mask, interruptReason reason);
  virtual asynStatus clearUInt32Interrupt(epicsUInt32 mask);
  void report(FILE *fp, int details);
  virtual const char* getTypeName();
  virtual asynStatus callCallback(int addr);
  static const char *typeNames[];

protected:
  asynParamType type;
  bool isValueDefined();
  void markValueIsDefined();
  void notifyList();
  int getIndex();
  virtual void reportDefinedValue(FILE *fp, int details);
  paramList *parentList;

private:

  char *name;
  int index;
  bool valueDefined;
};

#endif /* ASYNPARAMETER_H_ */
