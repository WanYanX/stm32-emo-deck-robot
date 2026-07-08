#ifndef __OLED_DATA_HPP__
#define __OLED_DATA_HPP__

#include <array>
#include <vector>

// 图像数据（示例，保留原 kunkun 数组）
extern const uint8_t miaomiao[][1024];

// ASCII 8×16 字模（95个字符，从空格开始）
extern const std::array<std::array<uint8_t, 16>, 95> OLED_F8x16;

#endif