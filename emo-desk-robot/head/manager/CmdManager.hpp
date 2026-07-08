#ifndef _CMD_MANAGER_H_
#define _CMD_MANAGER_H_

#include <string>
#include <unordered_map>
#include <functional>

#include "ServoManager.hpp"
#include "AnimationManager.hpp"
#include "LED.hpp"

class CmdManager
{
    using Handler = std::function<bool(const std::string& cmd)>;
    using CmdMap  = std::unordered_map<std::string, Handler>;
public:
    bool init(LED& led_0);

    bool execute(const std::string& cmd);

private:
    bool handleServo(const std::string& cmd);
    void register_animation_commands();
    void register_expression_commands();

    CmdMap cmd_map_;
    LED* led_0_ = nullptr;

    CmdManager() = default;
    ~CmdManager() = default;
    CmdManager(const CmdManager&) = delete;
    CmdManager& operator=(const CmdManager&) = delete;

public:
    static CmdManager &instance()
    {
        static CmdManager manager;
        return manager;
    }
};

#endif
