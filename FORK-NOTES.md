# 本分支改了什么（FORK-NOTES）

对照基准：`zhang-chong/SI4735`（= PU2CLR SI4735 2.1.8 + 1 笔搜索循环修复）。
改动只涉及 `src/SI4735.cpp` 与 `src/SI4735.h`，不新增/删除任何函数签名，**API 兼容**。

---

## 0. 中间来源带来的那一笔改动（原样保留）

`zhang-chong` 唯一的提交把 `seekStation(up_down, 0)` 从两个 `seekStationProgress()` 的
`do/while` 循环体内提到了循环之前。这一处是对的：`SEEK_START` 是一次性命令，芯片收到后自己扫频，
扫完把状态字里的 `VALID`（找到台）或 `BLTF`（到波段边界）置位；正确用法是"发一次 + 轮询状态"。
放在循环里等于每轮都"从当前频率重新起搜"，表现为搜台停不下来。

本次把它留下的痕迹清理掉：循环里注释掉的死代码、多余的缩进、混进上游文件的中文注释，
以及 `library.properties` 里仍写着 `PU2CLR SI4735` / `2.1.8` / 指向 `pu2clr` 的问题（改名 `SI4732`）。

---

## 1. `waitToSend()`：加超时 + 不再把读失败当成 CTS

原实现：

```cpp
do {
    delayMicroseconds(MIN_DELAY_WAIT_SEND_LOOP);
    Wire.requestFrom(deviceAddress, 1);
} while (!(Wire.read() & 0B10000000));
```

两个问题：

1. **没有上限**。芯片不在线（未焊、处于复位、`powerDown()` 之后）时永远等不到 CTS，函数不返回。
2. **`Wire.read()` 无数据时返回 `-1`**，而 `-1 & 0B10000000` 是真。也就是说"一个字节都没收到"
   会被判定为"CTS 已就绪"，接着所有后续读取都拿到 `0xFF`。

修改后：

```cpp
uint32_t start = millis();
while (millis() - start < CTS_TIMEOUT_MS) {          // 600 ms
    delayMicroseconds(MIN_DELAY_WAIT_SEND_LOOP);
    if (Wire.requestFrom(deviceAddress, 1) != 1) continue;   // 收不到字节 = 总线不正常
    if (Wire.read() & 0B10000000) { busTimeout = false; return; }
}
busTimeout = true;                                    // 超时，交给调用方处理
```

超时取 **600 ms** 而不是几十毫秒，是因为 `radioPowerUp()` 之后那次等待：按数据手册，POWER_UP 后
晶振起振最长要 500 ms。普通命令的 CTS 通常 1 ms 内就到，循环会立刻退出，不影响速度。

---

## 2. `getStatus()`：重试有上限，失败保留上次有效状态

原实现：

```cpp
do {
    waitToSend();
    Wire.requestFrom(deviceAddress, limitResp);
    for (uint8_t i = 0; i < limitResp; i++)
        currentStatus.raw[i] = Wire.read();      // 读不到 -> 0xFF
} while (currentStatus.resp.ERR);                // ERR 恒为 1 -> 永久循环
```

叠加上一条：总线一断，`raw[]` 全填 `0xFF`，`ERR`（bit 6）恒置位，**整个 sketch 卡死在这种状态**，
只能复位。而 `getStatus()` 在 FM 里被 RDS、立体声、信号强度轮询反复调用。

修改后：

- 先读进本地 `buf[]`，检查 byte0 的 `ERR` 位；
- 最多重试 `STATUS_READ_ATTEMPTS`（3）次，任何一次 `requestFrom` 字节数不对就重试；
- 成功才拷进 `currentStatus`；NBFM 只回 6 字节时把 `raw[6..7]` 清零（原来留着上一次的残留值）；
- 三次都失败 → `busTimeout = true`，**保留上一次的有效状态**，上层读到的频率仍是一个合理值，不是 `0xFFFF`。

---

## 3. `getFrequency()`：读状态不再 `CANCEL=1`

原实现 `getStatus(0, 1)`，第二个参数是 `CANCEL`："中止正在进行的搜索"。
纯读取不该改变芯片状态。这个副作用直接导致：

```cpp
seekNextStation():
    seekStation(1, 1);              // 内部 delay(30 << 2) = 120 ms
    delay(maxDelaySetFrequency);    // 再 30 ms
    getFrequency();                 // -> getStatus(0,1) -> 把刚发的搜索掐掉
```

FM 搜台正常要 100~500 ms，它在 150 ms 左右就下发 CANCEL，于是**停在半路频率上，没有真正锁定电台**。
改成 `getStatus(0, 0)` 后，这两个最常被新手直接调用的函数才真正可用。

---

## 4. `seekNextStation()` / `seekPreviousStation()`：等搜索结束再读频率

两者改为共用新的私有方法 `seekAndWait(up)`：

```cpp
seekStation(up, 1);
delay(maxDelaySetFrequency);
do {
    getStatus(0, 0);
    if (busTimeout) return;
    if (VALID || BLTF) break;
    delay(maxDelaySetFrequency);
} while (millis() - start < maxSeekTime);

if (!VALID && !BLTF) getStatus(0, 1);   // 超时了就把搜索掐掉
getFrequency();
```

---

## 5. `seekStationProgress()`（两个重载）：总线失效即返回 + 超时中止

- 循环里 `getStatus()` 之后加 `if (busTimeout) return;`，避免命令丢了以后空转到 `maxSeekTime`；
- 循环因超时退出、而 `VALID`/`BLTF` 都没置位时，补一次 `getStatus(0, 1)` **主动中止**。
  否则芯片会在后台继续调谐，松开按键几秒后刻度自己跳一下；
- 带 `stopSeeking` 重载里，用户要求停止时也先 `getStatus(0, 1)` 再返回。

> 注意：`maxSeekTime` 默认由 `MAX_SEEK_TIME` 决定（`setMaxSeekTime()` 可改）。这个值设得越大，
> 搜不到台时"等待"越久。它现在只是上限，正常搜索靠 `VALID`/`BLTF` 结束。

---

## 6. 其余四处同类无界循环

只给 `waitToSend()` 加超时**并不够**：下面这些 `do { ... } while (ERR)` 会在总线失效时变成
"每轮立刻返回 + ERR 恒真"的高速空转，同样是死锁。因此统一改成有界重试（读本地缓冲、
检查 `ERR`、成功才写回成员，失败置 `busTimeout`）：

| 函数 | 读取字节 |
|---|---|
| `getFirmware()` | 9 |
| `getAutomaticGainControl()` | 3 |
| `getRdsStatus()` | 13（RDS 打开时每个 loop 都在跑，最容易踩到） |
| `getSsbAgcStatus()` | 3 |
| `queryLibraryId()` | 8（另外原来在第一次读取前就 `while (libraryID.resp.ERR)` 判断了未初始化的栈变量，已 `memset` 清零） |

---

## 7. 两个编译期就能看出来的老问题

- `getRdsDateTime()`：`uint32_t mjd, day, month, year;` 之后直接 `mjd |= dt.refined.mjd;`
  —— **未初始化就参与位或**，编译器 `-Wmaybe-uninitialized` 报警。已改为 `uint32_t mjd = 0`。
  这个 bug 会让 RDS 日期偶发乱码。
- `removeUnwantedChar()`：`str[size-1] = '\0';` 缩进错位（看起来像在 `if` 里，其实在循环外），
  gcc `-Wmisleading-indentation` 报警。语义本来就是"循环结束后补终止符"，只修正缩进并加 `size > 0` 保护。

---

## 8. 新增接口

```cpp
bool busTimedOut(void);   // 上一次总线访问是否超时/未被应答
```

官方库没有任何途径知道"读回来的是不是一个真实值"。有了它，可以在芯片掉线时停在上一屏、
或者重试初始化，而不是拿着 `0xFFFF` 当频率去画界面。

`busTimeout` 是 `protected` 成员，派生类可以直接判断（`ats-mini-cn` 就是这么用的，见
`extras/ats-mini-cn/SI4735-fixed.h`）。

---

## 9. 已知的、**没有**改的地方

这些属于语义/设计层面，改了会影响兼容性，留作记录：

1. `seekStationProgress()` 返回 `void`，调用方无法区分"没搜到台"和"命令没送到"（现在至少可以用
   `busTimedOut()` 判断后者）。
2. SSB 模式下 `seekStationProgress()` 静默 `return`，不报错。SI4732 在 SSB 下没有硬件 seek，是硬件限制。
3. `seekStation()` 里 AM 分支 `ANTCAPL = (currentWorkFrequency > 1800) ? 1 : 0` 看着像硬编码，
   其实符合数据手册（SW 要 `ANTCAPH=0 / ANTCAPL=1`），**不是 bug**。
4. 补丁（patch）只存在芯片 RAM，`powerDown()` / 掉电即失效，必须重新 `loadPatch()`。这是芯片特性。
5. 没有处理多任务/中断抢占：`Wire` 不是线程安全的，别在中断里调这些方法。

---

## 附：与 ats-mini-cn 的关系

`extras/ats-mini-cn/SI4735-fixed.h` 是 [ats-mini-cn](https://github.com/openwrt-1983/ats-mini-cn)
固件里用的派生类，原本以**覆盖**的方式（`class SI4735_fixed : public SI4735`）修同样的几处。
现在这些修复已经下沉到库本体，那份派生类仍可继续用，但两个都启用时以派生类为准（名字隐藏）。

`extras/` 目录不参与 Arduino 编译，只作参考。

---

## 10. 新增：参考机型 MINI4732（1.1.8G）的驱动行为对齐

上面第 1~9 条是**健壮性**修复，不改变正常路径的输出。这一节是另一种性质的改动：
把参考机型 MINI4732（固件 1.1.8G）的出厂参数做进库里，让换库即得同样手感。

数值不是猜的，来自该机型的「配置导出」文件（头标记 `#MINI4732-CFG-V3`，
101 个键 = 47 参数 + 51 主题色）。

### 10.1 `applyReferenceProfile()`

```cpp
void applyReferenceProfile();   // 按当前调谐模式下发一组 property
```

| 模式 | 下发内容 | 来源键 |
|---|---|---|
| FM | 去加重 50 µs + Seek 门限 SNR 5 / RSSI 10 | `fm_deemph=1` / `fm_snr=5` / `fm_rssi=10` |
| AM / SSB | AVC 最大增益 50 dB + Seek 门限 SNR 3 / RSSI 10 | `avcmaxgain=50` / `am_snr=3` / `am_rssi=10` |

宏都叫 `REF_*`，集中定义在 `SI4735.h` 顶部，改默认行为只改那一处。

**这些 property 只在对应调谐模式下有效**，切模式后要重新调一次。只影响「搜台判定」，
不影响手动调谐，所以对手动调台没有副作用。

### 10.2 两个门限 API

官方库只暴露了 property 宏（`FM_SEEK_TUNE_SNR_THRESHOLD` 等），应用里到处写
`sendProperty` 不好维护，补了两层封装：

```cpp
void setFmSeekThresholds(uint8_t snrDb, uint8_t rssiDbuv);
void setAmSeekThresholds(uint8_t snrDb, uint8_t rssiDbuv);
```

### 10.3 `SI4732Bands.h` —— 波段表

参考机型有 21 个波段，**顺序**照抄实机（业余段插在 SWn 之间，不是排在最后）：

```
VHF / LW / MW / SW1 / 80M / SW2 / SW3 / 40M / SW4 / SW5 /
SW6 / SW7 / 20M / SW8 / SW9 / 15M / SW10 / CB / 10M / ALL / AIR
```

结构体 `SI4732Band { name, minFreq, maxFreq, step, mode }`，频率统一 kHz
（`uint32_t`，因为 VHF 108000 会溢出 `uint16_t`）。取用走 `si4732BandByIndex(i)`。

> ⚠ **可信度分级**：顺序 = 实机照片，确定；起止频率和步进 = 按常规广播/业余划分推定，
> 未逐项核对实机。要精确对齐就改这一张表，应用层不用动。
>
> ⚠ 第 21 项 `AIR`（118–137 MHz 航空段）参考机型有，但 SI4735/SI4732 的 FM 上限是 108 MHz，
> **物理上收不到**。表里保留该项以便和实机菜单一致，应用层应当过滤掉它再调谐。

### 10.4 没有做的

- **SSB 28 kHz 栅格对齐**：芯片 SSB 的 LO 与 BFO 合成规则在应用层才能确定（取决于
  用 `setSSB` 还是手动设 frequency + BFO），下沉到库会限制调用方式，暂不提供。
- **调谐加速手感**（`tune_speed_time=60`）：纯应用层计时逻辑，库不掺和，只导出宏
  `REF_TUNE_SPEED_MS` 供应用使用。
