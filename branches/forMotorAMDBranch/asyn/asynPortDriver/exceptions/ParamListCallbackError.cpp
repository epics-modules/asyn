/*
 * ParamListCallbackError.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#include "ParamListCallbackError.h"
#include "asynPortDriver.h"

ParamListCallbackError::ParamListCallbackError(paramList *pList):
  ParamListException(pList, "ParamListCallbackError", "error calling back")
{
  this->pList = pList;

}
