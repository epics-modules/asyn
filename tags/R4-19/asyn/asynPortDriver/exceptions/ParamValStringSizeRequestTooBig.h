/*
 * ParamValStringSizeRequestTooBig.h
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALSTRINGSIZEREQUESTTOOBIG_H_
#define PARAMVALSTRINGSIZEREQUESTTOOBIG_H_

#include <stdexcept>

class ParamValStringSizeRequestTooBig: public std::logic_error {
public:
	ParamValStringSizeRequestTooBig(const std::string& description);
};

#endif /* PARAMVALSTRINGSIZEREQUESTTOOBIG_H_ */
