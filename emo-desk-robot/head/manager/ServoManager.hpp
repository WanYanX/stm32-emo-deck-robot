#ifndef SERVO_MANAGER_H
#define SERVO_MANAGER_H

#include <unordered_map>
#include <string>
#include "Servo.hpp"

class ServoManager
{
public:
    //注册舵机
    bool register_servo(std::string servo_name,const Servo& servo);
    //设置舵机角度
    bool set_angle(std::string servo_name,uint32_t angle);
    bool set_angle(std::string cmd_str);
private:
    std::string get_servo_name(std::string cmd_str);
    uint32_t get_servo_angle(std::string cmd_str);

private:
    std::unordered_map<std::string, Servo> servo_list_;

public:
    static ServoManager &instance()
    {
        static ServoManager manager;
        return manager;
    }

private:
    ServoManager()                                = default;
    ~ServoManager()                               = default;
    ServoManager(const ServoManager &)            = delete;
    ServoManager &operator=(const ServoManager &) = delete;
};

#endif