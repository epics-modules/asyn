/*
 * APDException.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#include "APDException.h"

#include <epicsString.h>
#include <string.h>
#include <stdlib.h>

APDException::APDException(char *name, char *description)
{
  this->name = epicsStrDup(name);
  this->description = epicsStrDup(description);
}

APDException::~APDException()
{
  free(name);
  free(description);
}

/** Return a message string that can be used by the catching code
 *  as an indication of what has gone wrong.
 */
char* APDException::getMessageString()
{
  char formatString[] = "%s %s";
  char *retString;

  retString = new char[strlen(formatString) - 6 + strlen(name) +
      strlen(description) + 1];

  sprintf(retString, formatString, name,
      description);

  return retString;
}
