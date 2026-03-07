#ifndef STATIC_INFO_H
#define STATIC_INFO_H

#include <vector>
#include <string>
#include "domain/node/device.h"
#include <nlohmann/json.hpp>

struct StaticInfoItem {
    ImageInfo imageInfo;
    TaskOverhead taskOverhead;
    StaticInfoItem(){};
    StaticInfoItem(const nlohmann::json &device) {
        imageInfo.container_name = device["imageInfo"]["container_name"];
        imageInfo.image = device["imageInfo"]["image"];
        imageInfo.cmds = device["imageInfo"]["cmds"].get<std::vector<std::string> >();
        imageInfo.args = device["imageInfo"]["args"].get<std::vector<std::string> >();
        imageInfo.host_config_privileged = device["imageInfo"]["host_config_privileged"];
        imageInfo.env = device["imageInfo"]["env"].get<std::vector<std::string> >();
        imageInfo.host_config_binds = device["imageInfo"]["host_config_binds"].get<std::vector<std::string> >();
        imageInfo.devices = device["imageInfo"]["devices"].get<std::vector<std::string> >();
        imageInfo.host_ip = device["imageInfo"]["host_ip"];
        imageInfo.host_port = device["imageInfo"]["host_port"];
        imageInfo.container_port = device["imageInfo"]["container_port"];
        imageInfo.has_tty = device["imageInfo"]["has_tty"];

        taskOverhead.proc_time = device["taskOverhead"]["proc_time"];
        taskOverhead.mem_usage = device["taskOverhead"]["mem_usage"];
        taskOverhead.cpu_usage = device["taskOverhead"]["cpu_usage"];
        taskOverhead.xpu_usage = device["taskOverhead"]["xpu_usage"];
    }
};

#endif
