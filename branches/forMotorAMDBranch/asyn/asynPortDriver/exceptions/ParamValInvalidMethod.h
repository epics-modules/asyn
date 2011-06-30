/*
 * ParamValDummyParam.h
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALINVALIDMETHOD_H_
#define PARAMVALINVALIDMETHOD_H_

#include "ParamValException.h"

/** Exception to throw if the parameter does not handle this type of
 * call.  i.e.  Float64Param does not support get/setInteger.
 */
class ParamValInvalidMethod: public ParamValException
{
public:
  ParamValInvalidMethod(ParamVal *param);
};

#endif /* PARAMVALINVALIDMETHOD_H_ */
