/*
 * ParamValException.h
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALEXCEPTION_H_
#define PARAMVALEXCEPTION_H_
#include <ParamVal.h>

class ParamValException
{
public:
  ParamValException(ParamVal *param, char *name, char *description);
  virtual ~ParamValException();

  virtual char* getMessageString();

protected:
  ParamVal *param;
  char *description;
  char *name;
};

#endif /* PARAMVALEXCEPTION_H_ */
