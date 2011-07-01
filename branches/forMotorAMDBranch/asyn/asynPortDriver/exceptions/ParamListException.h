/*
 * ParamListException.h
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#ifndef PARAMLISTEXCEPTION_H_
#define PARAMLISTEXCEPTION_H_

#include <APDException.h>
#include <asynPortDriver.h>
#include <string.h>
#include <stdio.h>
class paramList;

class ParamListException: public APDException
{
public:
  ParamListException(paramList *param, char *name, char *description);
  virtual char* getMessageString();

protected:
  paramList *pList;

};

#endif /* PARAMLISTEXCEPTION_H_ */
