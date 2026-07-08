#ifndef ANIMATION_MANAGER_H
#define ANIMATION_MANAGER_H

#include <string>
#include <unordered_map>
#include <functional>

#include "OLED.hpp"
#include "ServoManager.hpp"

class AnimationManager
{
public:
    static AnimationManager& instance();

    bool register_animation(const std::string& name, std::function<void()> func);
    bool remove_animation(const std::string& name);
    bool play_animation(const std::string& name);
    bool init(OLED& oled);

    void tick();
    void switch_to(const std::string& name);
    void boot_to_idle();
    void show_expression(Expression expr);

private:
    void expression_move_happy();
    void expression_move_sad();
    void expression_move_idle();
    void expression_move_sleep();
    void expression_move_no();
    void expression_move_yes();
    void expression_move_caidan();
    void showExpression(Expression expr);

    std::unordered_map<std::string, std::function<void()>> animation_list_;
    OLED* oled_ptr_ = nullptr;

    Expression current_expr_ = Expression::ASLEEP;
    std::string active_anim_ = "";

    struct HeadSwayState {
        uint8_t head_a;
        bool    head_up = true;
        uint8_t body_a;
        bool    body_left = true;
        uint16_t skip = 0;

    } servo_con_;

    uint8_t miaomiao_frame_ = 0;
    uint8_t caidan_tick_ = 0;

    /* 超时计数器（每 tick ≈ 6ms） */
    uint16_t expression_timeout_ = 0;
    uint16_t anim_timeout_ = 0;

    AnimationManager() = default;
    ~AnimationManager() = default;
    AnimationManager(const AnimationManager&) = delete;
    AnimationManager& operator=(const AnimationManager&) = delete;
};

#endif
