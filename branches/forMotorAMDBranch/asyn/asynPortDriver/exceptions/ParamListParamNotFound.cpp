/*
 * ParamListParamNotFound.cpp
 *
 *  Created on: Jul 7, 2011
 *      Author: hammonds
 */

#include <epicsString.h>
#include <stdlib.h>

#include "ParamListParamNotFound.h"

ParamListParamNotFound::ParamListParamNotFound(paramList *pList, char *name):
ParamListException(pList, "ParamListParamNotFound",
    "Parameter name was not found in the list")
{
    this->requestedName = epicsStrDup(description);
}

ParamListParamNotFound::~ParamListParamNotFound()
{
    free(requestedName);
}
