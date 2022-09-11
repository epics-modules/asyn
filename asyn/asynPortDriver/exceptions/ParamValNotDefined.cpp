/*
 * ParamValNotDefined.cpp
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#include "ParamValNotDefined.h"

ParamValNotDefined::ParamValNotDefined(const std::string& description):
    std::logic_error(description){
}

