/*
 * ParamValNotDefined.h
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALNOTDEFINED_H_
#define PARAMVALNOTDEFINED_H_
#include <ParamVal.h>
#include <ParamValException.h>

/** This is an Exception that should be thrown when a
 * parameter method is trying to return a value when
 * the value has not been defined.
 */
class ParamValNotDefined: public ParamValException
{
public:
  ParamValNotDefined(ParamVal *param);
};

#endif /* PARAMVALNOTDEFINED_H_ */
