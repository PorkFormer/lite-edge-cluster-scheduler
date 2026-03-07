#ifndef NODE_MANAGER_H
#define NODE_MANAGER_H

#include <map>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <vector>

#include "domain/node/device.h"
#include "core/node_manager/static_info.h"

class NodeManager {
public:
    int RegisterNode(const Device &device);
    void RemoveDevice(DeviceID global_id);
    void DisplayDevices();
    void DisplayDeviceInfo();
    void LoadStaticInfo(const std::string &filepath);
    std::map<TaskType, std::map<DeviceType, StaticInfoItem>> GetStaticInfo() const;
    void StartDeviceInfoCollection();
    bool DisconnectDevice(const Device &device);

    std::shared_mutex &Mutex() { return devs_mutex_; }
    const std::shared_mutex &Mutex() const { return devs_mutex_; }
    std::map<DeviceID, Device> &DeviceStaticInfo() { return device_static_info_; }
    const std::map<DeviceID, Device> &DeviceStaticInfo() const { return device_static_info_; }
    std::map<DeviceID, DeviceStatus> &DeviceStatusMap() { return device_status_; }
    const std::map<DeviceID, DeviceStatus> &DeviceStatusMap() const { return device_status_; }
    std::map<DeviceID, std::vector<TaskType>> &ActiveServices() { return device_active_services_; }
    const std::map<DeviceID, std::vector<TaskType>> &ActiveServices() const { return device_active_services_; }
    std::map<TaskType, std::map<DeviceType, StaticInfoItem>> &StaticInfoMap() { return static_info_; }
    const std::map<TaskType, std::map<DeviceType, StaticInfoItem>> &StaticInfoMap() const { return static_info_; }

private:
    std::map<TaskType, std::map<DeviceType, StaticInfoItem>> static_info_;
    std::shared_mutex devs_mutex_;
    std::map<DeviceID, Device> device_static_info_;
    std::map<DeviceID, DeviceStatus> device_status_;
    std::map<DeviceID, std::vector<TaskType>> device_active_services_;
};

#endif
