//
// Created by lxsa1 on 22/10/2024.
//
#include "domain/node/device.h"
std::string GetDockerVersion(const Device& dev) {
    std::string docker_version;

    if(dev.type==DeviceType::ATLAS_H){
        docker_version = "v1.47";
    }else if(dev.type==DeviceType::RK3588){
        docker_version = "v1.45";
    }else if(dev.type==DeviceType::ATLAS_L){
        docker_version = "v1.39";
    }else{
        docker_version = "v1.39";
    }
    return docker_version;
}


