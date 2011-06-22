/*
 * ParamFactory.h
 *
 *  Created on: Jun 20, 2011
 *      Author: hammonds
 */

#ifndef PARAMFACTORY_H_
#define PARAMFACTORY_H_
#include <asynParamTypes.h>

class ParamVal;
class paramList;

class ParamFactory
{
public:
  static ParamVal* createParam(const char *name, asynParamType type,
    int index, paramList *parentList);
};

#endif /* PARAMFACTORY_H_ */
