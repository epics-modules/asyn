/*
 * ParamVal.cpp
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#include "epicsString.h"
#include "paramVal.h"


ParamVal::ParamVal(const char *name):
	type(asynParamUndefined), valueDefined(false){
	name = epicsStrDup(name);
}
