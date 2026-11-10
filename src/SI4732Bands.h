/**
 * @file SI4732Bands.h
 * @brief 参考机型 MINI4732（1.1.8G）的波段表
 *
 * 顺序照抄实机（21 项，业余段插在 SWn 之间，不是排在最后）：
 *   VHF / LW / MW / SW1 / 80M / SW2 / SW3 / 40M / SW4 / SW5 /
 *   SW6 / SW7 / 20M / SW8 / SW9 / 15M / SW10 / CB / 10M / ALL / AIR
 *
 * 频率单位统一 kHz（VHF 用 64000 表示 64 MHz），避免应用里到处换算。
 *
 * ⚠ 顺序来自实机照片，**确定**；起止频率和步进是按常规广播/业余划分推定的，
 *   实机未逐项核对。要精确对齐，拿实机进「波段」菜单逐项看起止频率，
 *   然后只改这张表即可，应用层不用动。
 */
#pragma once
#include <stdint.h>

/// 调制方式
#define SI4732_MODE_FM 0  // FM（VHF 广播段）
#define SI4732_MODE_AM 1  // AM（LW/MW/SW/CB）
#define SI4732_MODE_SSB 2 // SSB（业余段 80M/40M/20M/15M/10M）

/**
 * @brief 一个波段
 */
struct SI4732Band
{
    const char *name; ///< 显示名，最长 5 字符（屏幕上那一格放得下）
    uint32_t minFreq; ///< 下限 kHz（VHF/AIR 超过 65535，必须 32 位）
    uint32_t maxFreq; ///< 上限 kHz
    uint32_t step;    ///< 调谐步进 kHz（FM 也按 kHz 记，100 = 0.1 MHz）
    uint8_t mode;     ///< SI4732_MODE_*
};

static const SI4732Band SI4732_BANDS[] = {
    {"VHF", 64000, 108000, 100, SI4732_MODE_FM},
    {"LW", 153, 279, 1, SI4732_MODE_AM},
    {"MW", 520, 1710, 10, SI4732_MODE_AM},
    {"SW1", 2300, 3200, 5, SI4732_MODE_AM},
    {"80M", 3500, 4000, 1, SI4732_MODE_SSB},
    {"SW2", 3200, 4200, 5, SI4732_MODE_AM},
    {"SW3", 4200, 5500, 5, SI4732_MODE_AM},
    {"40M", 7000, 7300, 1, SI4732_MODE_SSB},
    {"SW4", 5500, 6300, 5, SI4732_MODE_AM},
    {"SW5", 6300, 7600, 5, SI4732_MODE_AM},
    {"SW6", 7600, 9500, 5, SI4732_MODE_AM},
    {"SW7", 9500, 11500, 5, SI4732_MODE_AM},
    {"20M", 14000, 14350, 1, SI4732_MODE_SSB},
    {"SW8", 11500, 13500, 5, SI4732_MODE_AM},
    {"SW9", 13500, 15500, 5, SI4732_MODE_AM},
    {"15M", 21000, 21450, 1, SI4732_MODE_SSB},
    {"SW10", 15500, 26100, 5, SI4732_MODE_AM},
    {"CB", 26965, 27405, 1, SI4732_MODE_AM},
    {"10M", 28000, 29700, 1, SI4732_MODE_SSB},
    {"ALL", 2300, 26100, 5, SI4732_MODE_AM},
    {"AIR", 118000, 137000, 25, SI4732_MODE_AM},
};

#define SI4732_BAND_COUNT ((uint8_t)(sizeof(SI4732_BANDS) / sizeof(SI4732_BANDS[0])))

/**
 * @brief 按索引取波段，越界返回 nullptr
 *
 * @param idx 0..SI4732_BAND_COUNT-1
 */
static inline const SI4732Band *si4732BandByIndex(uint8_t idx)
{
    if (idx >= SI4732_BAND_COUNT)
        return nullptr;
    return &SI4732_BANDS[idx];
}
