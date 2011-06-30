/*
 * ParamValInvalidMethod.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#include "ParamValInvalidMethod.h"

ParamValInvalidMethod::ParamValInvalidMethod(ParamVal *param) :
      ParamValException(param, "ParamValInvalidMethod",
          " This method is inappropriate for the selected type/")
{
  // Empty method
}
