/*
 * ParamListException.cpp
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#include "ParamListException.h"

ParamListException::ParamListException(paramList *param, char *name,
    char *description):
    APDException(name, description)
{
  // TODO Auto-generated constructor stub

}

/** Return a message string that can be used by the catching code
 *  as an indication of what has gone wrong.
 */
char* ParamListException::getMessageString()
{
  char formatString[] = "%s: The paramList %s";
  char *retString;

  retString = new char[strlen(formatString) - 6 + strlen(name) + 5
                       + strlen(description) + 1];

  sprintf(retString, formatString, name, description);
  return retString;
}

paramList* ParamListException::getParamList()
{
    return pList;
}
