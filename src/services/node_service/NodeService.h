#pragma once

#include "domain/node/device.h"

class NodeService {
public:
    int RegisterNode(const Device &device) const;
    bool DisconnectNode(const Device &device) const;
};
