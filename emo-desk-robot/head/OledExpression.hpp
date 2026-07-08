#ifndef __OLED_EXPRESSION_HPP__
#define __OLED_EXPRESSION_HPP__

#include "stm32f4xx.h"

/* 表情枚举 —— 每个值对应 oled_expression 数组中的一帧（共 24 帧） */
enum class Expression{
    SADNESS     = 0,   /* 悲伤：流泪、嘴角下弯 */
    SURPRISE    = 1,   /* 惊讶：瞪大眼、张嘴 */
    HORRIFIED   = 2,   /* 惊恐：瞳孔缩小、眉毛上挑 */
    FURIOUS     = 3,   /* 暴怒：眉头紧皱、咬牙切齿 */
    ANGER       = 4,   /* 生气：皱眉、眼神锐利 */
    DISGUST     = 5,   /* 厌恶：撇嘴、眯眼 */
    SKEPTICAL   = 6,   /* 怀疑：斜眼、眉毛不对称 */
    SUSPICIOUS  = 7,   /* 可疑：眯眼打量、头微倾 */
    HAPPINESS   = 8,   /* 开心：眯眼笑、嘴角上扬 */
    FEAR        = 9,   /* 害怕：瞪大眼、眉毛惊恐上挑 */
    ANNOYED     = 10,  /* 烦躁：半眯眼、不耐烦 */
    DEJECTED    = 11,  /* 沮丧：低头、眼神无光 */
    PLEADING    = 12,  /* 乞求：水汪汪大眼、眉毛下垂 */
    GUILTY      = 13,  /* 愧疚：眼神回避、眉毛内收 */
    CONFUSED    = 14,  /* 疑惑：歪头、一侧眉毛上挑 */
    BORED       = 15,  /* 无聊：半闭眼、打哈欠 */
    VULNERABLE  = 16,  /* 脆弱：眼神闪躲、嘴唇微颤 */
    DISAPPIONTED = 17, /* 失望：眼睑下垂、叹气 */
    AMAZED      = 18,  /* 惊叹：眼睛发亮、嘴巴大张 */
    TIRED       = 19,  /* 疲惫：黑眼圈、眼皮沉重 */
    DESPAIR     = 20,  /* 绝望：空洞眼神、面无表情 */
    EMBARRASSED = 21,  /* 尴尬：脸红、眼神飘忽 */
    EXCITED     = 22,  /* 激动：眼睛发亮、笑容灿烂 */
    ASLEEP      = 23   /* 睡觉：闭眼、呼吸平稳 */
};

/* 表情位图数据：24 帧 × 1024 字节（128×64 单色，SSD1306 page 格式） */
extern const uint8_t oled_expression[][1024];

#endif
