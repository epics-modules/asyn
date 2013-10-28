/*
 * ParamValNotDefined.h
 *
 *  Created on: Dec 19, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALNOTDEFINED_H_
#define PARAMVALNOTDEFINED_H_

#include <string>
#include <stdexcept>

using std::logic_error;
class ParamValNotDefined: public logic_error {
public:
	ParamValNotDefined(const std::string& description);
};

#endif /* PARAMVALNOTDEFINED_H_ */
