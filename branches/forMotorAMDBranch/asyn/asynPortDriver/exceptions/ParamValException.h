/*
 * ParamValException.h
 *
 *  Created on: Jun 29, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALEXCEPTION_H_
#define PARAMVALEXCEPTION_H_
#include <APDException.h>
class ParamVal;

class ParamValException: public APDException
{
public:
  ParamValException(ParamVal *param, char *name, char *description);

  virtual char* getMessageString();

protected:
  ParamVal *param;
};

#endif /* PARAMVALEXCEPTION_H_ */
