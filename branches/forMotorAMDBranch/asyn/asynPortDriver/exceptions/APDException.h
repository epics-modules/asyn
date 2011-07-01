/*
 * APDException.h
 *
 *  Created on: Jun 30, 2011
 *      Author: hammonds
 */

#ifndef APDEXCEPTION_H_
#define APDEXCEPTION_H_

class APDException
{
public:
  APDException(char *name, char *description);
  virtual ~APDException();
  virtual char* getMessageString();

protected:
  char *description;
  char *name;

};

#endif /* APDEXCEPTION_H_ */
