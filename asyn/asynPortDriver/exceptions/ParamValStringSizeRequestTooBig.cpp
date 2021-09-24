/*
 * ParamValStringSizeRequestTooBig.cpp
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#include "ParamValStringSizeRequestTooBig.h"

ParamValStringSizeRequestTooBig::ParamValStringSizeRequestTooBig(const std::string& description):
    std::logic_error(description){
}

