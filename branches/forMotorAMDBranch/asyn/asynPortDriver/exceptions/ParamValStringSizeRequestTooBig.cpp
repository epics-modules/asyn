/*
 * ParamValStringSizeRequestTooBig.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#include "ParamValStringSizeRequestTooBig.h"

ParamValStringSizeRequestTooBig::ParamValStringSizeRequestTooBig(
    ParamVal *param):
    ParamValException(param, "ParamValStringSizeRequestTooBig",
        "the requested string size is larger than the requested string")
{
  // TODO Auto-generated constructor stub

}
