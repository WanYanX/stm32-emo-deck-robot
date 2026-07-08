#include "manager/AnimationManager.hpp"
#include "OledExpression.hpp"
#include "OLED_Data.hpp"
#include "ResourceManager.hpp"
#include "Delay.hpp"
#include <cstdlib>
#include "AnimationManager.hpp"

static constexpr uint8_t HEAD_SWAY_LOW  = 80;
static constexpr uint8_t HEAD_SWAY_HIGH = 120;
static constexpr uint8_t BODY_SWAY_LOW  = 80;
static constexpr uint8_t BODY_SWAY_HIGH = 120;
static constexpr uint16_t SWAY_SKIP     = 2;

/* 每 tick ≈ 6ms */

/* 加权随机：数字越大选中概率越小（min-of-two 策略） */
static uint16_t weighted_seconds(uint16_t max_sec)
{
    uint16_t a = (uint16_t)(rand() % max_sec) + 1;
    uint16_t b = (uint16_t)(rand() % max_sec) + 1;
    uint16_t sec = (a < b) ? a : b;
    return sec * 167;
}

AnimationManager &AnimationManager::instance()
{
    static AnimationManager manager;
    return manager;
}

void AnimationManager::boot_to_idle()
{
    showExpression(Expression::SURPRISE);
    ServoManager::instance().set_angle("head", 100);
    Delay_ms(20);
    ServoManager::instance().set_angle("body", 95);
    Delay_ms(20);
    current_expr_ = static_cast<Expression>(0xFF);
    active_anim_  = "";
    anim_timeout_ = 0;
    expression_timeout_ = 0;
}

bool AnimationManager::register_animation(const std::string &name, std::function<void()> func)
{
    if (name.empty()) return false;

    auto ret = animation_list_.emplace(name, std::move(func));
    return ret.second;
}

bool AnimationManager::remove_animation(const std::string &name)
{
    if (name.empty()) return false;
    return animation_list_.erase(name) != 0;
}

bool AnimationManager::play_animation(const std::string &name)
{
    auto it = animation_list_.find(name);
    if (it == animation_list_.end()) return false;

    it->second();
    return true;
}

void AnimationManager::tick()
{
    if (!active_anim_.empty()) {
        play_animation(active_anim_);
    }

    /* 动画超时 → 回到 idle（彩蛋无超时） */
    if (anim_timeout_ > 0) {
        anim_timeout_--;
        if (anim_timeout_ == 0) {
            switch_to("idle");
            return;
        }
    }

    /* 纯表情超时 → 回到 idle */
    if (expression_timeout_ > 0) {
        expression_timeout_--;
        if (expression_timeout_ == 0) {
            switch_to("idle");
        }
    }
}

void AnimationManager::switch_to(const std::string& name)
{
    if (animation_list_.find(name) == animation_list_.end()) return;
    active_anim_ = name;

    current_expr_ = static_cast<Expression>(0xFF);

    auto& rm = ResourceManager::instance();
    servo_con_.head_a = rm.head_angle();
    servo_con_.body_a = rm.body_angle();
    servo_con_.skip    = 0;
    servo_con_.head_up = true;
    servo_con_.body_left = true;

    if (name == "caidan") {
        ServoManager::instance().set_angle("head", 100);
        ServoManager::instance().set_angle("body", 95);
    }

    miaomiao_frame_ = 0;
    caidan_tick_    = 0;

    /* 动画超时：caidan 和 sleep 永不超时，其他 1~5 秒 */
    expression_timeout_ = 0;
    if (name == "caidan" || name == "sleep") {
        anim_timeout_ = 0;
    } else {
        anim_timeout_ = weighted_seconds(5);
    }
}

void AnimationManager::show_expression(Expression expr)
{
    if (expr == current_expr_) return;
    current_expr_ = expr;
    oled_ptr_->showImage(0, 0, 128, 64, oled_expression[(int)expr]);
    /* 纯表情 1~10 秒后自动熄灭 */
    expression_timeout_ = weighted_seconds(10);
}

void AnimationManager::showExpression(Expression expr)
{
    if (expr == current_expr_) return;
    current_expr_ = expr;
    oled_ptr_->showImage(0, 0, 128, 64, oled_expression[(int)expr]);
}

void AnimationManager::expression_move_happy()
{
    servo_con_.skip++;
    if (servo_con_.skip < SWAY_SKIP) return;
    servo_con_.skip = 0;

    if (servo_con_.head_up) {
        servo_con_.head_a++;
        if (servo_con_.head_a >= HEAD_SWAY_HIGH) servo_con_.head_up = false;
    } else {
        servo_con_.head_a--;
        if (servo_con_.head_a <= HEAD_SWAY_LOW) servo_con_.head_up = true;
    }

    if (servo_con_.body_left) {
        servo_con_.body_a++;
        if (servo_con_.body_a >= BODY_SWAY_HIGH) servo_con_.body_left = false;
    } else {
        servo_con_.body_a--;
        if (servo_con_.body_a <= BODY_SWAY_LOW) servo_con_.body_left = true;
    }
    ServoManager::instance().set_angle("head", servo_con_.head_a);
    ServoManager::instance().set_angle("body", servo_con_.body_a);
}

void AnimationManager::expression_move_sad()
{
    if (servo_con_.body_a == 95 && servo_con_.head_a == 125) return;

    if (servo_con_.body_a != 95) {
        servo_con_.body_left = servo_con_.body_a < 95;
        if (servo_con_.body_left) {
            ++servo_con_.body_a;
        } else {
            --servo_con_.body_a;
        }
    }

    if (servo_con_.head_a != 125) {
        servo_con_.head_up = servo_con_.head_a < 125;
        if (servo_con_.head_up) {
            ++servo_con_.head_a;
        } else {
            --servo_con_.head_a;
        }
    }

    ServoManager::instance().set_angle("head", servo_con_.head_a);
    ServoManager::instance().set_angle("body", servo_con_.body_a);
}

void AnimationManager::expression_move_idle()
{
    if (servo_con_.body_a == 95 && servo_con_.head_a == 100) return;

    if (servo_con_.body_a != 95) {
        servo_con_.body_left = servo_con_.body_a < 95;
        if (servo_con_.body_left) {
            ++servo_con_.body_a;
        } else {
            --servo_con_.body_a;
        }
    }

    if (servo_con_.head_a != 100) {
        servo_con_.head_up = servo_con_.head_a < 100;
        if (servo_con_.head_up) {
            ++servo_con_.head_a;
        } else {
            --servo_con_.head_a;
        }
    }

    ServoManager::instance().set_angle("head", servo_con_.head_a);
    ServoManager::instance().set_angle("body", servo_con_.body_a);
}

void AnimationManager::expression_move_sleep()
{
    if (servo_con_.head_a == 110) return;

    if (servo_con_.head_a != 110) {
        servo_con_.head_up = servo_con_.head_a < 110;
        if (servo_con_.head_up) {
            ++servo_con_.head_a;
        } else {
            --servo_con_.head_a;
        }
    }

    ServoManager::instance().set_angle("head", servo_con_.head_a);
}

void AnimationManager::expression_move_no()
{
    ++servo_con_.skip;
    if (servo_con_.skip < SWAY_SKIP) return;
    servo_con_.skip = 0;

    if (servo_con_.body_left) {
        servo_con_.body_a++;
        if (servo_con_.body_a >= 115) servo_con_.body_left = false;
    } else {
        servo_con_.body_a--;
        if (servo_con_.body_a <= 75) servo_con_.body_left = true;
    }

    ServoManager::instance().set_angle("body", servo_con_.body_a);
    ServoManager::instance().set_angle("head", 100);
}

void AnimationManager::expression_move_yes()
{
    ++servo_con_.skip;
    if (servo_con_.skip < SWAY_SKIP) return;
    servo_con_.skip = 0;

    if (servo_con_.head_up) {
        servo_con_.head_a++;
        if (servo_con_.head_a >= 115) servo_con_.head_up = false;
    } else {
        servo_con_.head_a--;
        if (servo_con_.head_a <= 85) servo_con_.head_up = true;
    }

    ServoManager::instance().set_angle("body", 95);
    ServoManager::instance().set_angle("head", servo_con_.head_a);
}

void AnimationManager::expression_move_caidan()
{
    oled_ptr_->showImage(0, 0, 128, 64, miaomiao[miaomiao_frame_]);
    ++miaomiao_frame_;
    if (miaomiao_frame_ >= 30) {
        miaomiao_frame_ = 0;
    }
}

bool AnimationManager::init(OLED &oled)
{
    oled_ptr_ = &oled;

    srand(1);

    register_animation("init", [this]() {
        ServoManager::instance().set_angle("head", 100);
        Delay_ms(20);
        ServoManager::instance().set_angle("body", 95);
        Delay_ms(20);
        showExpression(Expression::SURPRISE);
        showExpression(Expression::ASLEEP);
        showExpression(Expression::SURPRISE);
        oled_ptr_->update();
    });

    register_animation("happy", [this]() {
        showExpression(Expression::EXCITED);
        expression_move_happy();
    });

    register_animation("sad", [this]() {
        showExpression(Expression::SADNESS);
        expression_move_sad();
    });

    register_animation("sleep", [this]() {
        showExpression(Expression::ASLEEP);
        expression_move_sleep();
    });

    register_animation("idle", [this]() {
        showExpression(Expression::SURPRISE);

        expression_move_idle();
    });

    register_animation("no", [this]() {
        showExpression(Expression::FURIOUS);
        expression_move_no();
    });

    register_animation("yes", [this]() {
        showExpression(Expression::HAPPINESS);
        expression_move_yes();
    });

    register_animation("caidan", [this]() {
        expression_move_caidan();
    });

    return true;
}
