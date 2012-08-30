/*
 * ParamValNotDefined.h
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALNOTDEFINED_H_
#define PARAMVALNOTDEFINED_H_

#include <stdexcept>

class ParamValNotDefined: public std::logic_error {
public:
	ParamValNotDefined(const std::string& description);
};

#endif /* PARAMVALNOTDEFINED_H_ */
