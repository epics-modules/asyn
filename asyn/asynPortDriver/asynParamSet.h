#ifndef AsynParamSet_H
#define AsynParamSet_H

#include <vector>

#include "asynParamType.h"

struct asynParam {
    const char* name;
    asynParamType type;
    int* index;
};

class asynParamSet {
public:
    std::vector<asynParam> getParamDefinitions() {
        return paramDefinitions;
    }

protected:
    void add(const char* name, asynParamType type, int* index) {
        asynParam param;
        param.name = name;
        param.type = type;
        param.index = index;

        this->paramDefinitions.push_back(param);
    }

private:
    std::vector<asynParam> paramDefinitions;
};

#endif  // AsynParamSet_H
