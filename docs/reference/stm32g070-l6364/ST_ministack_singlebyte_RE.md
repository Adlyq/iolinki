# ST IO-Link Device MiniStack — 单字节引擎逆向文档

> 目的：还原 ST 官方单字节引擎（`iol_device_ministack_stm32g0_wc16_ot.a`）的收/发机制，
> 尤其是「应答如何在中端里同步生成并写入 FR0」这一套逻辑，供后续在 iolinki 上复刻。
> 所有偏移量、寄存器、结构体字段均标注来源（对象文件 + 段名 + 段内偏移），方便日后定位查询。

---

## 1. 素材与工具

| 项 | 路径 / 命令 |
|---|---|
| 静态库 | `Drivers/Middlewares/ST/IoLinkLib/IOD02A1/Lib/iol_device_ministack_stm32g0_wc16_ot.a` |
| 对象成员 | `iolinkstack.o` `stackbase.o` `stackcore.o` `stackevents.o` `stackisdu.o` `stackmultibyte.o` `stackparammanager.o` `stacksinglebyte.o` `stacksyscmd.o` `stacktransparent.o` |
| 头文件 | `.../IOD02A1/inc/iolinkstack.h` `stackbase.h` `stackevents.h` |
| L6364 寄存器头 | `Drivers/BSP/Components/l6364/l6364_reg.h` |
| 旧接线 glue | `Src/iolink_int_interface.c` `Src/iolink_interface.c` `Inc/iolink_interface.h` |
| 反汇编工具 | `/usr/bin/arm-none-eabi-objdump -dr <lib>`（llvm-objdump 会解错字节序，必须用 arm-none-eabi 版本） |

重新生成反汇编（单字节引擎对象）：
```bash
cd /tmp && ar x <lib> && for f in stacksinglebyte stackcore iolinkstack stackbase; do \
  arm-none-eabi-objdump -dr $f.o > $f.dis; done
```

> 说明：`.a` 内每个函数独占一个段（`.text.<FuncName>` / `.bss.<Name>` / `.rodata.<Name>`），
> 段内地址从 0 开始。下文「偏移」一律指 **段内偏移**（`<FuncName>+0xNN`）。
> 「文件偏移」= `objdump -h` 的 `File off` 列，即该段在 `.o` 文件内的起始字节偏移。

### 1.1 关键段文件偏移速查（`objdump -h` 的 File off）

`stacksinglebyte.o`：

| 段 | 文件偏移 | 大小 |
|---|---|---|
| `.text.StackConfigureStackSinglebyte` | 0x34 | 0x0c |
| `.text.StackSingleByteHandleFirstOperatingModes` | 0x40 | 0x108 |
| `.text.StackSingleByteHandleOperatingMode` | 0x148 | 0x478 |
| `.rodata.StackSingleByteHandleOperatingMode` | 0x5c0 | 0x60 |
| `.text.StackSingleByteStartCallbackTimerAfterReply` | 0x620 | 0x0c |
| `.text.StackSingleByteOnPhyInterrupt` | 0x62c | 0xb8 |
| `.text.StackSingleBytePhyIntHandler` | 0x6e4 | 0xe0 |
| `.bss.gStackSingleBytePrivData` | 0x7c4 | 0x48 |
| `.bss.phyIntHandlerOnGoing.0` | 0x7c4 | 0x01 |

`stackcore.o`（SPI 原语）：

| 段 | 文件偏移 | 段 | 文件偏移 |
|---|---|---|---|
| `.text.Stack_Board_SpiReadNext` | 0x34 | `.text.Stack_Board_SpiWrite` | 0xc0 |
| `.text.Stack_Board_SpiReadLast` | 0x50 | `.text.Stack_Board_SpiWriteBegin` | 0xfc |
| `.text.Stack_Board_SpiReadBegin` | 0x74 | `.text.Stack_Board_SpiWriteBeginWithData` | 0x128 |
| `.text.Stack_Board_SpiReadStatus` | 0x9c | `.text.Stack_Board_SpiWriteNext` | 0x15c |
| | | `.text.Stack_Board_SpiWriteDone` | 0x17c |
| | | `.text.Stack_Board_SpiAbortAccess` | 0x18c |
| | | `.text.Stack_Board_GetIntPinState` | 0x1a0 |

`iolinkstack.o`：

| 段 | 文件偏移 | 大小 |
|---|---|---|
| `.text.IOLinkDecodeCycleTime` | 0x34 | 0x28 |
| `.text.IOLinkEncodeCycleTime` | 0x5c | 0x3c |
| `.text.IOLinkCalculateChecksum` | 0x98 | 0x1c |
| `.rodata.aChecksum6` | 0xb4 | 0x100 |

`stackbase.o`（关键段）：

| 段 | 文件偏移 | 段 | 文件偏移 |
|---|---|---|---|
| `.text.StackBaseConfigurePhy` | 0x610 | `.text.StackBaseOdWrite` | 0xfb4 |
| `.text.StackBaseInitOdOctetCount` | 0xae0 | `.text.StackBaseOdRead` | 0x106c |
| `.text.StackBaseValidateControlOctet` | 0xe84 | `.text.StackBaseStartCallbackTimer` | 0x11a8 |
| `.text.StackBaseValidateFrameType` | 0xea0 | `.text.StackBaseOnTimerChanBInterrupt` | 0x1230 |
| `.bss.aGetOdOctetCount` | 0x1418 (0x05) | `.bss.gStackBasePrivData` | 0x1418 (0x14) |
| `.bss.gStackBaseSharData` | 0x1418 (0x5c) | | |

---

## 2. 架构：两个中断

**单字节模式的协议处理完全在中端里，主循环不参与**。

| 事件 | 入口（旧 glue `Src/iolink_int_interface.c`） |
|---|---|
| L6364 `INT`(PA5) 数据中断（每收/发 1 octet） | `EXTI4_15_IRQHandler`(line 188) → `StackSingleBytePhyIntHandler()` |
| TIM3 **CC2(ChanB)** 输出比较中断 = 13×Tbit 帧结束定时器 | `HAL_TIM_OC_DelayElapsedCallback`(line 126) → `StackBaseOnTimerChanBInterrupt()` |

定时器接口（`Inc/iolink_interface.h` + `Src/iolink_interface.c`）：
- `STACK_TIMX = TIM3`，`STACK_TIMX_CHAN_A = CC1`，`STACK_TIMX_CHAN_B = CC2`。
- `IOLinkIfInitStackTimer(baudrate)`：`Pulse = (SYSCLOCK/((prescaler+1)*baudrate))*13 - 1`（line 122），
  即 **13×Tbit** 的 OC 比较周期。
- `IOLinkIfRestartTimerChanB()` = `__HAL_TIM_ENABLE(&hStackTim)`（重开定时器，重新计时）。
- 字节 ISR 每处理完一个 octet 就调 `IOLinkIfRestartTimerChanB`，若 13×Tbit 内没有下一个 octet → 定时器中断 = 帧结束。

---

## 3. L6364 寄存器映射（`l6364_reg.h`）

| 地址 | 名字 | 说明 |
|---|---|---|
| 0x00 | MSEQ | M[1:0] OD 宽度 / M2CNT[3:0] 期望 octet 数（读操作） |
| 0x01 | CFG | 波特率 BD、RF、UVT |
| 0x02 | CCTL | SIO/SGL/LS/HS 等 |
| 0x03 | DCTL | |
| 0x04 | LINK | SND=0x01, END=0x02, CNT[3:0] |
| 0x05 | THERM | |
| 0x06 | TEMP | |
| 0x07 | LED | |
| 0x08 | DCDC | |
| 0x09 | DSTAT | |
| 0x0A | STATUS | RST=0x80,IINT=0x40,UV=0x20,CHK=0x08,DAT=0x04,SSC=0x02,SOT=0x01 |
| 0x0F | LINK2 | |
| 0x10 | FR0 | 单字节模式的数据寄存器（读=收，写=发，写 FR0 ≡ LINK:SND） |

STATUS 位域（`l6364_reg.h` 与 DS 一致）：`DAT=0x04`（有数据/请求下一字节）、`CHK=0x08`（校验/超时/溢出错误）。

---

## 4. 关键数据结构布局

### 4.1 `gStackSingleBytePrivData`（`.bss.gStackSingleBytePrivData`，72 字节）

状态机私有数据。**反汇编证据**：`stacksinglebyte.o` 中所有 `[r5,#off]` 访问（r5 指向此结构）。

| 偏移 | 大小 | 含义 | 证据 |
|---|---|---|---|
| 0x00 | 1 | state（跳转表下标 0..11） | `ldrb r2,[r5,#0]` 后 `lsls r2,#2; ldr r3,[jt,r2]` |
| 0x01 | 1 | 帧类型（m-seq type，来自 sharData[8]） | `strb r2,[r5,#1]` |
| 0x02 | 1 | MC（主站控制 octet） | `strb r0,[r5,#2]` |
| 0x03 | 1 | CKT（收到的校验 octet） | `strb r0,[r5,#3]`；`ldrb r1,[r5,#3]` 与算出的校验比较 |
| 0x04 | 1 | CKS（应答校验 octet） | `strb r0,[r5,#4]`；`ldrb r1,[r5,#4]` 后 `SpiWrite(0x10,r1)` |
| 0x05 | 1 | 校验累加器（XOR 滚动） | `eors r0,r3; strb r0,[r5,#5]` |
| 0x06..0x25 | 32 | 应答/OD 数据缓冲（PDin 方向） | `strb r0,[r3,#6]`（r3=r5+count） |
| 0x26..0x45 | 32 | RX 帧缓冲（主站发来的 octet 累积区） | `adds r3,#0x26; strb r0,[r3,#0]`；`memcpy(sharData, r5+0x26, len)` |
| 0x46 | 1 | 字节计数（rx count） | `movs r6,#70(0x46); ldrb r3,[r5,r6]` 后 `adds r3,#1; strb` |
| 0x47 | 1 | 标志（帧已校验合法 → 置位） | `movs r3,#71(0x47); strb r2,#0` 清零；`ldrb r3,[r5,#0x47]; cmp #0` 判断 |

> 注：0x06 缓冲（32 字节）与 0x26 缓冲（32 字节）连续，0x26=0x06+32，合计 64 字节数据区 + 2 字节计数/标志 = 66，对齐到 72（0x48）。

### 4.2 `gStackBaseSharData`（`.bss.gStackBaseSharData`，共享配置/状态）

| 偏移 | 含义 | 证据 |
|---|---|---|
| 0x03 | PDIN 长度 | `ldrb r3,[sharData,#3]` 赋值给 r9（type-4 路径） |
| 0x04 | PDOUT 长度 | `ldrb r3,[sharData,#4]` 赋值给 sl；`ldrb r2,[sharData-0x31+4]` 作 memcpy 长度 |
| 0x08 | 栈模式（SIO/IOLink，值 0/1/2…） | `ldrb r3,[sharData,#8]`；`StackBaseMode()` 返回 `[8]` 是否为 1/2 |
| 0x0A | m-seq 能力（OP 位） | `ldrb r3,[sharData,#10]`（`StackBaseValidateFrameType` line 8） |

### 4.3 `gStackBasePrivData`（`.bss.gStackBasePrivData`，栈内部私有）

| 偏移 | 含义 | 证据 |
|---|---|---|
| 0x02 | 回调定时器周期（自动重装值） | `strh r2,[r4,#2]`（`StackBaseStartCallbackTimer`） |
| 0x04 | 回调定时器倒计数 | `strh r5,[r4,#4]` |
| 0x06 | 周期基值 | `ldrh r3,[r4,#6]` |
| 0x08 | ? | `strh r3,[r4,#8]` |
| 0x0C | ?（对比 0x0D 计数） | `StackBaseOnTimerChanBInterrupt` line 62-66 |
| 0x0D | ?（对比 0x0C） | 同上 |
| 0x0E | 倒计数（SIO 相关） | `ldrb r3,[r4,#14]` |
| 0x0F | 标志 | `strb r3,[r4,#15]` |
| 0x10 | 标志 | `strb r3,[r4,#16]` |

### 4.4 `stack_iol_pins`（`IOL_Pins_t`，栈基址里的引脚映射，见 `stackbase.h:116`）

| 偏移 | 字段 | 证据（`stackcore.o`） |
|---|---|---|
| 0x00 | `spi`（SPI_TypeDef*） | `ldr r2,[r3,#0]` → 访问 SPI 寄存器 |
| 0x04 | `spi_ss_pin`（u16） | `ldrh r5,[r3,#4]` → 写 SS |
| 0x08 | `spi_ss_port`（GPIO*） | `ldr r4,[r3,#8]` |
| 0x26 | `int_pin`（u16） | `ldrh r1,[r3,#38(0x26)]` → `HAL_GPIO_ReadPin` |
| 0x28 | `int_port`（GPIO*） | `ldr r0,[r3,#40(0x28)]` |

GPIO 寄存器（SS 操作）：`GPIO->BRR=0x28`（写位=置低），`GPIO->BSRR=0x18`（写位=置高）。

### 4.5 `aGetOdOctetCount`（`.bss.aGetOdOctetCount`，5 字节，`stackbase.o`）

`aGetOdOctetCount[frame_type]` = 该帧类型携带的 OD octet 数（有符号，-1 表无效）。
由 `StackBaseInitOdOctetCount` 按 m-seq 能力初始化：
- `[0]=1`、`[2]=1`（固定）
- `[3]`、`[4]` 按 OP 代码取值 1/2/8/32（对应 1/2/8/32 字节 PD）。
- 状态机里 `ldrsb r7,[aGetOdOctetCount, frame_type]` 取当前帧的 OD 数。

---

## 5. SPI 原语（`stackcore.o`，纯寄存器 LL 访问）

> ST 的 SPI 读协议与我们 `iod02a1_bus.c` 相同：**读 = Begin(8clk 地址) + Last(8clk 数据)，SS 在 Begin/Last 间保持低**；**写 = 16clk（地址|0x80 + 数据）**。
> 关键差异：ST 的 `Stack_Board_SpiWrite(reg,data)` **返回 STATUS 字节**（地址段的回读），状态机据此判断 DAT/CHK/RST。

| 函数 | 段名 | 逻辑 |
|---|---|---|
| `Stack_Board_SpiReadBegin(reg)` | `.text.Stack_Board_SpiReadBegin` | SS 低（写 BRR）、`SPI->CR1\|=0x40`(SPE)、写地址到 DR、等 TXE、读 DR 返回 STATUS。**SS 保持低** |
| `Stack_Board_SpiReadLast()` | `.text.Stack_Board_SpiReadLast` | 写 0 到 DR（补 8clk）、等 TXE、读 DR=数据、SS 高（写 BSRR）。返回数据字节 |
| `Stack_Board_SpiReadStatus()` | `.text.Stack_Board_SpiReadStatus` | SS 低 + 8clk 读 STATUS + SS 高 |
| `Stack_Board_SpiWrite(reg,data)` | `.text.Stack_Board_SpiWrite` | SS 低、SPE、写 `reg\|0x80`、读 STATUS（**返回值**）、写 data、等 TXE、SS 高 |
| `Stack_Board_SpiWriteBegin/BeginWithData/Next/Done` | 各 `.text.*` | 多字节写（`ConfigurePhy` 用），Begin 只写地址不写数据、Next 补数据、Done 拉高 SS |
| `Stack_Board_SpiAbortAccess()` | `.text.Stack_Board_SpiAbortAccess` | 若 SS 低则拉高（`tst ss_pin,[ss_port+0x10](IDR)` 判断，否则写 BSRR） |
| `Stack_Board_GetIntPinState()` | `.text.Stack_Board_GetIntPinState` | `HAL_GPIO_ReadPin(int_port, int_pin)` |

---

## 6. 字节 ISR 与分发器

### 6.1 `StackSingleBytePhyIntHandler`（`.text.StackSingleBytePhyIntHandler`，`stacksinglebyte.o`）

这是 L6364 DAT 中断的字节级处理函数。**重入保护 + INT 轮询排空 + 逐字节调状态机 + 重启 13×Tbit 定时器**。

```c
void StackSingleBytePhyIntHandler(void) {
    if (phyIntHandlerOnGoing) return;       // 重入保护（.bss.phyIntHandlerOnGoing.0，1字节）
    phyIntHandlerOnGoing = 1;

loop:
    if (!Stack_Board_GetIntPinState()) goto done;   // INT 低：无待处理 octet

    state = privData[0];
    if (state <= 2) {
        r = StackSingleByteHandleFirstOperatingModes();   // STARTUP/PREOP
    } else {
        ftype = privData[1];                              // 帧类型
        arg = (ftype==3)?3 : (ftype==4)?4 : 2;
        r = StackSingleByteHandleOperatingMode(arg);
    }

    switch (r) {
    case 3:  // TRANSMISSION → 收尾：写 LINK:END
        Stack_Board_SpiWrite(0x04, 0x02);    // LINK:END
        privData[0] = 1;                      // 回到 idle
        if (sharData[8] > 2) sharData[8] = 2;
        goto restart;
    case 4:  // TRANSMISSION_COMPLETE → 复位重建 PHY
        Stack_Board_SpiWrite(0x0A, 0x80);     // STATUS:RST
        StackBaseConfigurePhy();
        privData[0] = 1;
        goto restart;
    case 2:  // PROCESSING → 放弃/错误恢复
        Stack_Board_SpiAbortAccess();
        Stack_Board_SpiWrite(0x04, 0x02);     // LINK:END
        if (sharData[8]==0) StackBaseStartCallbackTimer(0x7f);
        privData[0] = 1;
        goto restart;
    }
restart:
    IOLinkIfRestartTimerChanB();              // 每字节后重启 13×Tbit 定时器
    goto loop;

done:
    phyIntHandlerOnGoing = 0;
}
```

关键点：
- **每处理完一个 octet 都重启 13×Tbit 定时器**；主站停发后定时器超时 → `StackBaseOnTimerChanBInterrupt`。
- 状态机返回值 `2/3/4` 决定收尾动作（LINK:END / RST / Abort）。

### 6.2 `StackSingleByteOnPhyInterrupt`（`.text.StackSingleByteOnPhyInterrupt`）

与 6.1 几乎相同的分发器（供旧版 HAL 回调路径使用；当前固件走 6.1）。同样 `state<=2→FirstOperatingModes`、否则按帧类型调 `HandleOperatingMode`，处理返回码 2/3/4。

---

## 7. 状态机（核心）

### 7.1 `StackSingleByteHandleOperatingMode(r0=frame_type)`（`.text.StackSingleByteHandleOperatingMode`）

**这是「应答在中断里同步生成」的核心。** 每被调用一次处理一个 octet。

入口：
```c
r6 = frame_type;              // 2 / 3 / 4
r7 = aGetOdOctetCount[frame_type];  // 有符号 OD 数
r5 = privData;
if (frame_type == 4) { r9 = sharData[3]/*PDIN len*/; sl = sharData[4]/*PDOUT len*/; jt = table2; }
else                 { jt = table1; }
state = privData[0]; if (state > 11) return 3;
goto jt[state];
```

**跳转表 1（frame_type 2/3）** `.rodata.StackSingleByteHandleOperatingMode`（24×u32，段内 0x00 起）：
```
state: 0    1    2    3    4    5    6    7    8    9   10   11
off :  0x70 0x6c 0x6c 0x244 0x106 0x15a 0xec 0x42 0x96 0x4c 0x4c 0x74
```

**跳转表 2（frame_type 4）**（段内 0x30 起）：
```
state: 0    1    2    3    4    5    6    7    8    9   10   11
off :  0x70 0x6c 0x6c 0x2da 0x10c 0x15e 0xf0 0x31e 0x38c 0x1e0 0x4c 0x74
```

（原始字节：`.rodata` 段内容 0x60 字节，前 24 字 = 表1，后 24 字 = 表2，见 `objdump -s -j .rodata.StackSingleByteHandleOperatingMode`）

### 7.2 状态含义与关键片段（段内偏移）

| 偏移 | 返回/动作 |
|---|---|
| `+0x6c` | `return 3`（TRANSMISSION） |
| `+0x70` | `return 4`（TRANSMISSION_COMPLETE） |
| `+0x74` | `SpiWrite(0x04, 0x02)`（LINK:END）、state=1、`StackBaseStartCallbackTimer(0)`、`StackBaseSensorsReadEnable()`，然后按 SpiWrite 状态返回 |
| `+0x42` | **写 CKS 到 FR0**：`ldrb r1,[r5,#4]; SpiWrite(0x10, r1)`、state=11；返回 = `(SpiWrite status 位7(RST) ? 0 : 4)` |
| `+0x4c` | 同上（`ldrb r1,[r5,#4]; SpiWrite(0x10,r1)`），state=11，返回同上 |
| `+0x96` | **发 OD/PD 数据 octet**：`StackBaseOdRead(0x60, index=0x1F&MC, count=r7, 8)` → `SpiWrite(0x10, 结果)`；滚动校验 `eors`；`IOLinkCalculateChecksum` 更新 CKS；计数→ 到 r7 则 state=9/11 |
| `+0x106`(表1)/`+0x10c`(表2) | **收数据 octet**：`SpiReadBegin(0x10)`；`status&0x0C != 0x04`→`return 2`；`SpiReadLast`；存 `[r5+count+0x26]`；`eors` 进 `[r5,#5]`；count++ |
| `+0x15a`(表1)/`+0x15e`(表2) | **收数据 octet（另缓冲）**：存 `[r5+count+6]`；count 达 r7 则 `IOLinkCalculateChecksum` 校验 CKT（`[r5,#3]`）→ 合法则 `StackBaseOdWrite` 把 PDout 写回应用、`memcpy(sharData, r5+0x26, PDOUT_len)`、state=9 |
| `+0xec`/`+0xf0` | 收 CKS octet：`SpiReadBegin`，DAT 清→`return 2`，否则 `SpiReadLast`，校验校验和，合法则 state=… |
| `+0x244`(表1)/`+0x2da`(表2) | **启动应答分支**：OD 数 ≤0 或 MC>127 → 跳 `+0x300`（最终校验和）；否则 state=5 收数据 |
| `+0x300` | 最终校验：`IOLinkCalculateChecksum([r5,#5], flags)` 与 `[r5,#3]` 比 → 合法 state=7 |
| `+0x31e`(表2) | state=7、清零 `[r5,#5]`，r7>0 则 `StackBaseOdRead(0x60,…)`→`SpiWrite(0x10,…)` 发 PD 数据，state=8 |
| `+0x38c`(表2) | `StackBaseOdRead`→`SpiWrite(0x10,…)` 发数据，state=8 |
| `+0x1e0`(表2) | 发 CKS/收尾 |

### 7.3 收发时序（关键结论）

**单字节模式下，`写 FR0 = LINK:SND`（启动发送），每个应答 octet 由状态机在中断里直接 `SpiWrite(0x10, octet)` 送出**：

1. 主站逐 octet 发请求 → L6364 每收一个 octet 置 DAT → INT → `PhyIntHandler` → `HandleOperatingMode`：
   - 状态 4/5 收数据，字节累积到 `[r5+0x26]`（RX 缓冲），同时 XOR 累加校验到 `[r5,#5]`。
2. 收满 `aGetOdOctetCount[frame_type]` 个 octet（帧长由 MC/CKT 决定，**栈从一开始就知道要收多少**）→ 校验 CKT → 合法。
3. **同一个中断里**立即切换到发送态（state 7/8/3）：`StackBaseOdRead` 取应答数据 → `SpiWrite(0x10, data)` 把首个应答 octet 写进 FR0（=启动发送）。
4. 后续每个应答 octet：L6364 置 DAT 请求 → 下一个 INT → 状态机继续 `SpiWrite(0x10, 下一 octet)`。
5. 最后一个数据 octet 后，写 CKS（`SpiWrite(0x10, CKS)`，state 9→11），再下一个 DAT → `SpiWrite(0x04, LINK:END)` 终止（返回 3，由 PhyIntHandler 执行）。

**所以 ST 没有「主循环生成→写 RAM→中断搬运」的分离，也没有预生成——应答的生成与写入 FR0 都在字节中断里、与收数据同步完成。** 这正是它首字节延迟是微秒级、无需紧主循环的原因。

---

## 8. 校验和

### `IOLinkCalculateChecksum(acc, flags)`（`.text.IOLinkCalculateChecksum`，`iolinkstack.o`）

```c
uint8_t IOLinkCalculateChecksum(uint8_t acc, uint8_t flags) {
    uint8_t f = flags & 0xC0;        // 只保留 bit7(events) bit6(PD not valid)
    uint8_t x = acc ^ f;             // acc 是滚动 XOR 累加值
    if ((int8_t)x < 0) x = ~x;       // 符号扩展为负则取反
    return aChecksum6[x] | f;        // 6bit 查表后或回事件/有效标志
}
```
- `aChecksum6` = 256 字节 6-bit 校验表（`.rodata.aChecksum6`，`iolinkstack.o`，段内 0x00 起）。
- 与 iolinki 的 `iolink_checksum6` 等价。

---

## 9. 帧校验

### `StackBaseValidateControlOctet(mc)`（`.text.StackBaseValidateControlOctet`）
校验 MC octet：`(mc & 0x60)==0x60` 且 `mc & 0x1F` 满足条件 → 返回 1/0（是否为合法 MC 通道）。

### `StackBaseValidateFrameType(r0=frame_type, r1=flags)`（`.text.StackBaseValidateFrameType`）
按 frame_type(2/3/4) 与 m-seq 能力（sharData[10]、aGetOdOctetCount）校验帧类型/长度合法性，返回 0/1。frame_type 与 M 序列的映射需结合 `StackBaseSetmSeqCapOP/PREOP` 的配置值（0x02/0x08/0x0A/0x0C/0x0E）。

---

## 10. OD/PD 数据搬运

### `StackBaseOdRead(channel, index, arg2, count)`（`.text.StackBaseOdRead`）
按 `channel`（r0）分派取应答数据：
- `0x20`：读直接参数页 `directParameter[index]`。
- `0x40`：读事件存储（`StackEventsGetEventMemoryData`）。
- `0x60`：ISDU 流（`StackISDUGetState/IncIndex/GetDataValue` 等）。
- 其它：返回 0。
状态机里应答数据 octet 全部经此函数取（例如 `OdRead(0x60, 0x1F&MC, 8, count)`）。

### `StackBaseOdWrite(channel, index, data, count)`（`.text.StackBaseOdWrite`）
按 `channel` 写收到的 PDout / ISDU 数据到应用：`0x20`→直接参数、`0x60`→ISDU（`StackISDUSaveDataFrame`）、其它→`gStackBasePrivData` 相关标志。

---

## 11. 帧结束定时器回调

### `StackBaseOnTimerChanBInterrupt`（`.text.StackBaseOnTimerChanBInterrupt`，`stackbase.o`）
13×Tbit 超时（主站帧结束/停发）后触发：
- 递减 sharData 里的看门狗/超时计数；
- 周期到达时递增 `gStackBasePrivData[0]`（`sharData[0]`？见 `ldrb r3,[r5,#0]; cmp #0xff; add #1`）；
- 递减 SIO 相关倒计数（privData[14]），到 0 时 `sharData[8]=0`、`sharData[2]=1`；
- 比较 privData[12]/[13]，不相等时写 `SpiWrite(0x07, privData[13])`（**LED 寄存器刷新**）；
- 若干分支调 `StackBaseSetSioActive`（回 SIO）或 `StackTransparentSuspend/ResumeUart`。

> 该回调主要承担 **超时→回 SIO/主站丢失、LED 刷新、周期回调** 等后台职责；正常应答不依赖它（应答已在字节中断里完成）。

### `StackBaseStartCallbackTimer(delay)`（`.text.StackBaseStartCallbackTimer`）
启动回调定时器（周期来自 sharData / directParameter，`IOLinkIfResetStackTimerChanB` + `IOLinkIfStartStackTimerChanB`）。

---

## 12. 完整「收→判→答」时序（一句话版）

```
主站发帧：  ... DAT(DAT+0x04) ──INT──▶ PhyIntHandler ─▶ HandleOperatingMode(state 4/5)
           每 octet: SpiReadBegin(0x10)+SpiReadLast → 存入 privData+0x26 → XOR 累加校验
           ──每 octet 后 IOLinkIfRestartTimerChanB()──

收满 N octet（N 由 MC/CKT 决定）: 校验 CKT → 合法(privData[0x47]=1)
           ──同一次 INT 里──▶ 切发送态(state 3/7/8) → StackBaseOdRead 取数据
           → SpiWrite(0x10, 应答octet)   ← 写 FR0 = 启动应答（首字节在此发出）

后续每 octet: L6364 置 DAT ──INT──▶ 状态机 → SpiWrite(0x10, 下一 octet)
最后一个数据后: SpiWrite(0x10, CKS)；再下一个 DAT → SpiWrite(0x04, LINK:END) 终止
```

---

## 13. 待确认 / 后续可深入的点

- [ ] `StackSingleByteHandleFirstOperatingModes` 中 frame_type 2/3/4 与 M 序列类型（TYPE_0 / TYPE_2_1 / TYPE_2_V…）的精确映射，需结合 `StackBaseSetmSeqCapOP/PREOP` 的配置值继续核对。
- [ ] `StackBaseValidateFrameType` 各分支返回语义（尚未逐分支还原）。
- [ ] 跳转表 2（type-4）中 `+0x31e/+0x38c/+0x1e0` 与表 1 对应片段（`+0x244/+0x96` 等）的差异细节（type-4 = 带 OD 的 OPERATE 循环）。
- [ ] `gStackBaseSharData` 完整字段表（当前只确认 0x03/0x04/0x08/0x0A）。
- [ ] `StackBaseOnTimerChanBInterrupt` 里 `gStackBasePrivData` 各计数字段的完整语义。

> 复刻到 iolinki 时，**最关键的可迁移结论**：
> ① 帧长由 MC/CKT 在第一个 octet 就能确定；
> ② 应答在「收满最后一个 octet 的同一个中断里」生成并写 FR0，无主循环、无预生成、无阻塞发送；
> ③ 帧边界靠 13×Tbit 硬件定时器，而不是「读 FR0 撞 CHK 超时」。
