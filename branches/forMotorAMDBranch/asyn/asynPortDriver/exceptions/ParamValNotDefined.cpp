/*
 * ParamValNotDefined.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#include "ParamValNotDefined.h"
#include <epicsString.h>

ParamValNotDefined::ParamValNotDefined(ParamVal *param) :
  ParamValException(param, "ParamValNotDefined", "this is a generic indicator")
{
  //Empty method
}
