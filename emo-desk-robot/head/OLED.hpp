#ifndef __OLED_HPP__
#define __OLED_HPP__

#include <string>

#include "stm32f4xx.h"
#include "OLED_Data.hpp"
#include "OledExpression.hpp"

class OLED {
public:
    OLED() = default;
    ~OLED() = default;

    OLED(const OLED&) = delete;
    OLED& operator=(const OLED&) = delete;

    void init();
    void clear();
    void update();
    bool needsUpdate() const { return pending_refresh_; }

    void areaClear(int16_t x, int16_t y, int16_t x1, int16_t y1);
    void areaReversal(int16_t x, int16_t y, int16_t x1, int16_t y1);
    void areaRefresh(int16_t x, int16_t y, int16_t x1, int16_t y1);

    void showChar(int16_t x, int16_t y, uint32_t charIndex);
    void showString(int16_t x, int16_t y, const std::string& str);
    void out(int16_t x, int16_t y, const std::string& str);

    void showImage(uint8_t x, uint8_t y, uint8_t width, uint8_t height, const uint8_t* imageData);

private:
    void i2cInit();
    void writeCommand(uint8_t cmd);
    void writeData(uint8_t data);
    void setCursor(uint8_t x, uint8_t page);

    std::array<std::array<uint8_t, 128>, 8> displayBuf_;

    volatile bool pending_refresh_ = false;
    volatile bool i2c_busy_ = false;
};

#endif
