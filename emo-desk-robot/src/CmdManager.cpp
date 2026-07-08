#include "CmdManager.hpp"
#include "OledExpression.hpp"

bool CmdManager::init(LED& led_0)
{
    led_0_ = &led_0;

    /* LED 指令 */
    cmd_map_["led-on"] = [this](const std::string&) {
        led_0_->turn_on();
        return true;
    };
    cmd_map_["led-off"] = [this](const std::string&) {
        led_0_->turn_off();
        return true;
    };

    register_animation_commands();
    register_expression_commands();

    return true;
}

void CmdManager::register_expression_commands()
{
    struct {
        const char* name;
        Expression expr;
    } table[] = {
        {"sadness",      Expression::SADNESS},
        {"surprise",     Expression::SURPRISE},
        {"horrified",    Expression::HORRIFIED},
        {"furious",      Expression::FURIOUS},
        {"anger",        Expression::ANGER},
        {"disgust",      Expression::DISGUST},
        {"skeptical",    Expression::SKEPTICAL},
        {"suspicious",   Expression::SUSPICIOUS},
        {"happiness",    Expression::HAPPINESS},
        {"fear",         Expression::FEAR},
        {"annoyed",      Expression::ANNOYED},
        {"dejected",     Expression::DEJECTED},
        {"pleading",     Expression::PLEADING},
        {"guilty",       Expression::GUILTY},
        {"confused",     Expression::CONFUSED},
        {"bored",        Expression::BORED},
        {"vulnerable",   Expression::VULNERABLE},
        {"disappionted", Expression::DISAPPIONTED},
        {"amazed",       Expression::AMAZED},
        {"tired",        Expression::TIRED},
        {"despair",      Expression::DESPAIR},
        {"embarrassed",  Expression::EMBARRASSED},
        {"excited",      Expression::EXCITED},
        {"asleep",       Expression::ASLEEP},
    };
    for (auto& e : table) {
        Expression expr = e.expr;
        cmd_map_[e.name] = [expr](const std::string&) {
            AnimationManager::instance().show_expression(expr);
            return true;
        };
    }
}

void CmdManager::register_animation_commands()
{
    static const char* anim_names[] = {
        "happy", "sad", "sleep", "idle", "no", "yes", "caidan", nullptr
    };
    for (const char** p = anim_names; *p != nullptr; ++p) {
        std::string name(*p);
        cmd_map_[name] = [name](const std::string&) {
            AnimationManager::instance().switch_to(name);
            return true;
        };
    }
}

bool CmdManager::execute(const std::string& raw)
{
    /* 格式：@<命令># 或 [stm32][命令] */
    std::string cmd;
    if (raw.size() >= 2 && raw.front() == '@') {
        size_t end = raw.find('#');
        if (end == std::string::npos) return false;
        cmd = raw.substr(1, end - 1);
    } else if (raw.size() > 9 && raw.substr(0, 7) == "[stm32]") {
        size_t rb = raw.find(']', 7);
        if (rb == std::string::npos) return false;
        cmd = raw.substr(8, rb - 8);
    } else {
        cmd = raw;
    }

    if (cmd.empty()) return false;

    /* 精确匹配动画 / LED 指令 */
    auto it = cmd_map_.find(cmd);
    if (it != cmd_map_.end()) {
        return it->second(cmd);
    }

    /* 舵机格式 "名称-数字"，如 head-90 */
    if (cmd.find('-') != std::string::npos) {
        return handleServo(cmd);
    }

    return false;
}

bool CmdManager::handleServo(const std::string& cmd)
{
    return ServoManager::instance().set_angle(cmd);
}
