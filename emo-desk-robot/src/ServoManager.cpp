#include "ServoManager.hpp"
#include "ResourceManager.hpp"

static constexpr uint32_t ANGLE_PARSE_FAILED = 0xFFFFFFFF;

bool ServoManager::register_servo(std::string servo_name, const Servo &servo)
{
    if (servo_list_.empty()) {
        servo_list_.emplace(servo_name, servo);
        return true;
    }
    const auto &servo_tmp = servo_list_.find(servo_name);
    if (servo_tmp == servo_list_.end()) {
        servo_list_.emplace(servo_name, servo);
        return true;
    }
    return false;
}

bool ServoManager::set_angle(std::string servo_name, uint32_t angle)
{
    if (servo_name == "head" && angle > 130)return false;

    if (servo_list_.empty()) return false;

    const auto &servo_tmp = servo_list_.find(servo_name);
    if (servo_tmp != servo_list_.end()) {
        servo_list_[servo_name].set_angle(angle);
        if (servo_name == "head") {
            ResourceManager::instance().set_head_angle(static_cast<uint8_t>(angle));
        } else if (servo_name == "body") {
            ResourceManager::instance().set_body_angle(static_cast<uint8_t>(angle));
        }
        return true;
    }

    return false;
}

bool ServoManager::set_angle(std::string cmd_str)
{
    if (servo_list_.empty()) return false;
    std::string name      = get_servo_name(cmd_str);
    if (name == "")return false;
    const auto &servo_tmp = servo_list_.find(name);
    if (servo_tmp != servo_list_.end()) {
        uint32_t angle = get_servo_angle(cmd_str);
        if (angle == ANGLE_PARSE_FAILED) return false;
        servo_list_[name].set_angle(angle);
        if (name == "head") {
            ResourceManager::instance().set_head_angle(static_cast<uint8_t>(angle));
        } else if (name == "body") {
            ResourceManager::instance().set_body_angle(static_cast<uint8_t>(angle));
        }
        return true;
    }
    return false;
}

uint32_t ServoManager::get_servo_angle(std::string cmd_str)
{
    size_t start_pos = cmd_str.find_last_of('-');
    if (start_pos == std::string::npos || start_pos + 1 >= cmd_str.size())
        return ANGLE_PARSE_FAILED;
    std::string angle = cmd_str.substr(start_pos + 1);
    try {
        return static_cast<uint32_t>(std::stoi(angle));
    } catch (const std::invalid_argument&) {
        return ANGLE_PARSE_FAILED;
    } catch (const std::out_of_range&) {
        return ANGLE_PARSE_FAILED;
    }
}

std::string ServoManager::get_servo_name(std::string cmd_str)
{
    size_t name_len = cmd_str.find_last_of('-');
    if (name_len == std::string::npos) return "";
    return cmd_str.substr(0, name_len);
}
