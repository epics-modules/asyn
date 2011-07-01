/*
 * ParamValException.cpp
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#include "ParamValException.h"
#include <APDException.h>
#include <ParamVal.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

ParamValException::ParamValException(ParamVal *param, char *name,
    char *description):
    APDException(name, description)
{
  this->param = param;
}

/** Return a message string that can be used by the catching code
 *  as an indication of what has gone wrong.
 */
char* ParamValException::getMessageString()
{
  char formatString[] = "%s: The parameter %s %s";
  char *retString;

  retString = new char[strlen(formatString) - 6 + strlen(name) + strlen(
      param->getName()) + strlen(description) + 1];

  sprintf(retString, "%s: The parameter %s %s", name, param->getName(),
      description);
  return retString;
}
