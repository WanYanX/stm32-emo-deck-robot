#include "OLED.hpp"
#include "Delay.hpp"

void OLED::i2cInit() {
    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_GPIOB, ENABLE);
    RCC_APB1PeriphClockCmd(RCC_APB1Periph_I2C1, ENABLE);

    GPIO_InitTypeDef gpio;
    gpio.GPIO_Mode   = GPIO_Mode_AF;
    gpio.GPIO_PuPd   = GPIO_PuPd_NOPULL;
    gpio.GPIO_OType  = GPIO_OType_OD;
    gpio.GPIO_Pin    = GPIO_Pin_6 | GPIO_Pin_7;
    gpio.GPIO_Speed  = GPIO_Speed_50MHz;
    GPIO_Init(GPIOB, &gpio);

    GPIO_PinAFConfig(GPIOB, GPIO_PinSource6, GPIO_AF_I2C1);
    GPIO_PinAFConfig(GPIOB, GPIO_PinSource7, GPIO_AF_I2C1);

    I2C_InitTypeDef i2c;
    i2c.I2C_Ack                = I2C_Ack_Enable;
    i2c.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    i2c.I2C_ClockSpeed         = 400000;
    i2c.I2C_DutyCycle          = I2C_DutyCycle_2;
    i2c.I2C_Mode               = I2C_Mode_I2C;
    i2c.I2C_OwnAddress1        = 0x00;
    I2C_Init(I2C1, &i2c);
    I2C_Cmd(I2C1, ENABLE);

    RCC_AHB1PeriphClockCmd(RCC_AHB1Periph_DMA1, ENABLE);
}

void OLED::writeCommand(uint8_t cmd)
{
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET);
    I2C_GenerateSTART(I2C1, ENABLE);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT) != SUCCESS);
    I2C_Send7bitAddress(I2C1, 0x78, I2C_Direction_Transmitter);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != SUCCESS);
    I2C_SendData(I2C1, 0x00);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);
    I2C_SendData(I2C1, cmd);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);
    I2C_GenerateSTOP(I2C1, ENABLE);
}

void OLED::writeData(uint8_t data)
{
    while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET);
    I2C_GenerateSTART(I2C1, ENABLE);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT) != SUCCESS);
    I2C_Send7bitAddress(I2C1, 0x78, I2C_Direction_Transmitter);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != SUCCESS);
    I2C_SendData(I2C1, 0x40);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);
    I2C_SendData(I2C1, data);
    while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);
    I2C_GenerateSTOP(I2C1, ENABLE);
}

void OLED::setCursor(uint8_t x, uint8_t page)
{
    uint8_t real_x = x + 2;
    writeCommand(0x0F & real_x);
    writeCommand(0x10 | (0x0F & (real_x >> 4)));
    writeCommand(0xB0 | (page & 0x07));
}

void OLED::init()
{
    for (auto &row : displayBuf_) {
        row.fill(0);
    }
    i2cInit();
    Delay_ms(100);
    writeCommand(0xAE);
    writeCommand(0xD5);
    writeCommand(0x80);
    writeCommand(0xA8);
    writeCommand(0x3F);
    writeCommand(0xD3);
    writeCommand(0x00);
    writeCommand(0x40);
    writeCommand(0xA1);
    writeCommand(0xC8);
    writeCommand(0xDA);
    writeCommand(0x12);
    writeCommand(0x81);
    writeCommand(0xCF);
    writeCommand(0xD9);
    writeCommand(0xF1);
    writeCommand(0xDB);
    writeCommand(0x30);
    writeCommand(0xA4);
    writeCommand(0xA6);
    writeCommand(0xAD);
    writeCommand(0x8B);
    writeCommand(0xAF);
    Delay_ms(200);
    clear();
    update();
}

void OLED::clear()
{
    for (auto &row : displayBuf_) {
        row.fill(0);
    }
    pending_refresh_ = true;
}

void OLED::update()
{
    if (i2c_busy_) return;
    i2c_busy_ = true;

    for (uint8_t page = 0; page < 8; ++page) {
        setCursor(0, page);

        while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET);
        I2C_GenerateSTART(I2C1, ENABLE);
        while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT) != SUCCESS);
        I2C_Send7bitAddress(I2C1, 0x78, I2C_Direction_Transmitter);
        while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != SUCCESS);
        I2C_SendData(I2C1, 0x40);
        while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);

        for (uint8_t col = 0; col < 128; ++col) {
            I2C_SendData(I2C1, displayBuf_[page][col]);
            while (I2C_GetFlagStatus(I2C1, I2C_FLAG_TXE) == RESET);
        }

        I2C_GenerateSTOP(I2C1, ENABLE);
    }

    i2c_busy_ = false;
    pending_refresh_ = false;
}

static int16_t clamp_x(int16_t v)
{
    if (v < 0) return 0;
    if (v > 127) return 127;
    return v;
}

void OLED::areaClear(int16_t x, int16_t y, int16_t x1, int16_t y1)
{
    x  = clamp_x(x);
    x1 = clamp_x(x1);
    if (x >= x1) return;

    if (y < 0) y = 0;
    if (y1 > 63) y1 = 63;
    if (y >= y1) return;

    uint8_t startPage = static_cast<uint8_t>(y) / 8;
    uint8_t endPage   = (static_cast<uint8_t>(y1) + 7) / 8;
    if (endPage > 8) endPage = 8;
    uint8_t startBit  = static_cast<uint8_t>(y) % 8;
    uint8_t endBit    = static_cast<uint8_t>(y1) % 8;

    for (uint8_t p = startPage; p < endPage; ++p) {
        for (int16_t col = x; col < x1; ++col) {
            if (p == startPage && startBit != 0) {
                displayBuf_[p][col] &= ~static_cast<uint8_t>(0xFF << startBit);
            } else if (p == endPage - 1 && endBit != 0) {
                displayBuf_[p][col] &= ~static_cast<uint8_t>(0xFF >> (8 - endBit));
            } else {
                displayBuf_[p][col] = 0x00;
            }
        }
    }
    pending_refresh_ = true;
}

void OLED::areaReversal(int16_t x, int16_t y, int16_t x1, int16_t y1)
{
    x  = clamp_x(x);
    x1 = clamp_x(x1);
    if (x >= x1) return;

    if (y < 0) y = 0;
    if (y1 > 63) y1 = 63;
    if (y >= y1) return;

    int16_t startPage = y / 8;
    int16_t endPage   = y1 / 8;
    if (y1 % 8 != 0) endPage++;
    if (endPage > 8) endPage = 8;

    for (int16_t p = startPage; p < endPage; ++p) {
        uint8_t maskStart = 0xFF, maskEnd = 0xFF;
        if (p == startPage && y % 8 != 0) {
            maskStart = static_cast<uint8_t>(0xFF << (y % 8));
        }
        if (p == endPage - 1 && y1 % 8 != 0) {
            maskEnd = static_cast<uint8_t>(0xFF >> (8 - (y1 % 8)));
        }
        uint8_t mask = maskStart & maskEnd;
        for (int16_t col = x; col < x1; ++col) {
            displayBuf_[p][col] ^= mask;
        }
    }
    pending_refresh_ = true;
}

void OLED::areaRefresh(int16_t x, int16_t y, int16_t x1, int16_t y1)
{
    if (y % 8 == 0 && y1 % 8 == 0) {
        uint8_t startPage = static_cast<uint8_t>(y) / 8;
        uint8_t endPage   = static_cast<uint8_t>(y1) / 8;
        if (endPage > 8) endPage = 8;
        for (uint8_t p = startPage; p < endPage; ++p) {
            setCursor(static_cast<uint8_t>(x), p);
            while (I2C_GetFlagStatus(I2C1, I2C_FLAG_BUSY) == SET);
            I2C_GenerateSTART(I2C1, ENABLE);
            while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_MODE_SELECT) != SUCCESS);
            I2C_Send7bitAddress(I2C1, 0x78, I2C_Direction_Transmitter);
            while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_TRANSMITTER_MODE_SELECTED) != SUCCESS);
            I2C_SendData(I2C1, 0x40);
            while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);

            for (int16_t col = x; col < x1; ++col) {
                I2C_SendData(I2C1, displayBuf_[p][col]);
                while (I2C_CheckEvent(I2C1, I2C_EVENT_MASTER_BYTE_TRANSMITTED) != SUCCESS);
            }
            I2C_GenerateSTOP(I2C1, ENABLE);
        }
    } else {
        areaReversal(0, 0, 127, 63);
        update();
        Delay_ms(500);
        areaReversal(0, 0, 127, 63);
        update();
    }
}

void OLED::showChar(int16_t x, int16_t y, uint32_t charIndex)
{
    if (charIndex >= 95) return;
    if (x < 0 || x > 120) return;
    if (y < 0 || y > 55) return;

    uint8_t page  = static_cast<uint8_t>(y) / 8;
    uint8_t shift = static_cast<uint8_t>(y) % 8;

    uint8_t pages_needed = (shift == 0) ? 2 : 3;
    if (page + pages_needed > 8) return;

    for (int16_t col = x; col < x + 8; ++col) {
        if (shift == 0) {
            displayBuf_[page][col] = 0x00;
        } else {
            displayBuf_[page][col] &= static_cast<uint8_t>(~static_cast<uint8_t>(0xFF << shift));
        }
        displayBuf_[page + 1][col] = 0x00;
        if (shift != 0) {
            displayBuf_[page + 2][col] &= static_cast<uint8_t>(~static_cast<uint8_t>(0xFF >> (8 - shift)));
        }
    }

    for (uint8_t i = 0; i < 16; ++i) {
        uint8_t data = OLED_F8x16[charIndex][i];
        uint8_t col  = static_cast<uint8_t>(x) + (i % 8);
        if (i < 8) {
            displayBuf_[page][col] = static_cast<uint8_t>(data << shift);
            if (shift != 0) {
                displayBuf_[page + 1][col] = static_cast<uint8_t>(data >> (8 - shift));
            }
        } else {
            displayBuf_[page + 1][col] = static_cast<uint8_t>(data << shift);
            if (shift != 0) {
                displayBuf_[page + 2][col] = static_cast<uint8_t>(data >> (8 - shift));
            }
        }
    }
    pending_refresh_ = true;
}

void OLED::showString(int16_t x, int16_t y, const std::string &str)
{
    int16_t cursorX = x;
    for (char ch : str) {
        if (ch < 32 || ch > 126) continue;
        showChar(cursorX, y, ch - 32);
        cursorX += 8;
    }
}

void OLED::out(int16_t x, int16_t y, const std::string &str)
{
    showString(x, y, str);
}

void OLED::showImage(uint8_t x, uint8_t y, uint8_t width, uint8_t height, const uint8_t *imageData)
{
    if (imageData == nullptr) return;
    if (width == 0 || height == 0) return;

    if (width % 8 != 0 || height % 8 != 0 || y % 8 != 0) {
        return;
    }

    uint8_t startPage = y / 8;
    uint8_t endPage   = (y + height) / 8;
    if (endPage > 8) return;

    uint8_t maxCol = x + width;
    if (maxCol > 128) maxCol = 128;

    for (uint8_t p = startPage; p < endPage; ++p) {
        for (uint8_t col = x; col < maxCol; ++col) {
            uint32_t idx = static_cast<uint32_t>(col - x) + static_cast<uint32_t>(p - startPage) * width;
            displayBuf_[p][col] = imageData[idx];
        }
    }

    pending_refresh_ = true;
}
