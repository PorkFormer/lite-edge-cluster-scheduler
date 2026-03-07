#include "domain/enums/TaskType.h"

#include <algorithm>
#include <cctype>

TaskType StrToTaskType(const std::string &str) {
    std::string s = str;
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](unsigned char c) { return !std::isspace(c); }));
    s.erase(std::find_if(s.rbegin(), s.rend(), [](unsigned char c) { return !std::isspace(c); }).base(), s.end());

    if (s == "YoloV5") return YoloV5;
    if (s == "MobileNet") return MobileNet;
    if (s == "Bert") return Bert;
    if (s == "ResNet50") return ResNet50;
    if (s == "deeplabv3") return deeplabv3;
    if (s == "transcoding") return transcoding;
    if (s == "decoding") return decoding;
    if (s == "encoding") return encoding;
    return Unknown;
}
