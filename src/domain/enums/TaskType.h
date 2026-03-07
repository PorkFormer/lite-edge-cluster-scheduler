#pragma once

#include <string>

#include <boost/uuid/uuid.hpp>
#include <nlohmann/json.hpp>
#include <spdlog/fmt/fmt.h>

enum TaskType {
    YoloV5, MobileNet, Bert, ResNet50, deeplabv3, transcoding, decoding, encoding, Unknown
};

template <> struct fmt::formatter<TaskType> : formatter<string_view> {
    template <typename FormatContext>
    auto format(TaskType c, FormatContext &ctx) const {
        string_view name = "unknown";
        switch (c) {
            case YoloV5: name = "YoloV5"; break;
            case MobileNet: name = "MobileNet"; break;
            case Bert: name = "Bert"; break;
            case ResNet50: name = "ResNet50"; break;
            case deeplabv3: name = "deeplabv3"; break;
            case transcoding: name = "transcoding"; break;
            case decoding: name = "decoding"; break;
            case encoding: name = "encoding"; break;
            case Unknown: name = "Unknown"; break;
        }
        return formatter<string_view>::format(name, ctx);
    }
};

TaskType StrToTaskType(const std::string &str);

NLOHMANN_JSON_SERIALIZE_ENUM(TaskType, {
    {TaskType::YoloV5, "YoloV5"},
    {TaskType::MobileNet, "MobileNet"},
    {TaskType::Bert, "Bert"},
    {TaskType::ResNet50, "ResNet50"},
    {TaskType::deeplabv3, "deeplabv3"},
    {TaskType::transcoding, "transcoding"},
    {TaskType::decoding, "decoding"},
    {TaskType::encoding, "encoding"},
    {TaskType::Unknown, "Unknown"}
})
