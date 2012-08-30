/*
 * ParamValValueNotChanged.h
 *
 *  Created on: Dec 20, 2011
 *      Author: hammonds
 */

#ifndef PARAMVALVALUENOTCHANGED_H_
#define PARAMVALVALUENOTCHANGED_H_

#include <stdexcept>

class ParamValValueNotChanged: public std::logic_error {
public:
	ParamValValueNotChanged(const std::string& description);
};

#endif /* PARAMVALVALUENOTCHANGED_H_ */
