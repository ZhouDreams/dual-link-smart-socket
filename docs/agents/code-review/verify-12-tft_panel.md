# Verification: tft_panel + tft_panel_st7789t

对应报告: report-12-tft_panel.md

---

## ✅ 确认的问题

### 发现 #1 — `tft_panel_st7789t_init` 硬编码 MADCTL=0x00

- **验证结论**: 确认。
- **验证过程**:
  1. 读 tft_panel_st7789t.c:324-326，确认 init 发送 `(uint8_t[]){0x00}` 到寄存器 `0x36`。
  2. 读 tft_panel_st7789t.c:200-211，确认 `tft_panel_new_st7789t` 根据 `rgb_endian` 设置 `madctl_val`：BGR → `LCD_CMD_BGR_BIT`，RGB → `0x00`。
  3. 读 tft_panel.c:257-261，确认 `tft_panel_create` 传入 `LCD_RGB_ELEMENT_ORDER_BGR`，因此 `madctl_val = LCD_CMD_BGR_BIT`。
  4. 读 tft_panel.c:281，确认 init 后调用 `esp_lcd_panel_mirror(me->panel, true, false)`。
  5. 读 tft_panel_st7789t.c:452-474，确认 `mirror()` 在 `madctl_val` 上设置 MX 位后发送完整 `madctl_val`（含 BGR 位），覆盖 init 的 `0x00`。
  6. 对照 ESP-IDF v5.5.3 官方 `esp_lcd_panel_st7789.c:188-190`，确认官方驱动在 init 中发送 `st7789->madctl_val`（非硬编码）。
  7. 结论：init 发送的 MADCTL 值与构造时配置不一致，BGR 位在 init 阶段丢失，仅因后续 `mirror()` 调用巧合修复。这是潜伏 bug，修复方案正确。

### 发现 #2 — `tft_panel_flush_done_cb_t` 未文档化 ISR 调用上下文

- **验证结论**: 确认。
- **验证过程**:
  1. 读 tft_panel.h:52-57，确认 Doxygen 注释仅写"刷新完成回调"，无 ISR 上下文说明。
  2. 读 tft_panel.c:235-242，确认 `on_color_trans_done` 回调注册到 `esp_lcd_panel_io_spi_config_t`。
  3. 读 tft_panel.c:632-648，确认 `tft_panel_on_color_trans_done` 直接在 SPI DMA 完成中断上下文中调用 `cb(ctx)`，无任务 defer。
  4. 读 lvgl_dashboard.c:1327-1339，确认当前回调 `lvgl_dashboard_flush_ready` 仅调用 `lv_display_flush_ready`（ISR 安全）和 `lvgl_dashboard_complete_flush`（使用 `portENTER_CRITICAL_SAFE`），未违反 ISR 约束。
  5. 结论：回调确实在 ISR 上下文执行，文档缺失该约束说明。修复方案（增加 `@note`）正确。

### 发现 #3 — classes.md §11.3 内部结构体文档漂移

- **验证结论**: 确认。
- **验证过程**:
  1. 读 classes.md:1531-1539，确认文档记录的 `struct tft_panel` 包含 `spi_device_handle_t spi` 字段，且缺少 `io`、`panel`、`mutex`、`callback_lock`、`spi_bus_initialized`、`destroying`。
  2. 读 tft_panel.c:52-64，确认实际结构体使用 `esp_lcd_panel_io_handle_t io` + `esp_lcd_panel_handle_t panel`，无 `spi_device_handle_t spi` 字段。
  3. 结论：实现已从直接 SPI 设备操作重构为 ESP-IDF `esp_lcd_panel_io` + `esp_lcd_panel` 两层抽象，文档未同步。修复方案（更新 classes.md）正确。

### 发现 #4 — init 发送冗余 INVON / DISPON / RAMWR

- **验证结论**: 确认。
- **验证过程**:
  1. 读 tft_panel_st7789t.c:378-383，确认 init 末尾发送 `0x21`（INVON）、`0x29`（DISPON）、`0x2C`（RAMWR）。
  2. 读 tft_panel.c:287，确认 `tft_panel_create` 随后调用 `esp_lcd_panel_invert_color(me->panel, true)` → 发送 `LCD_CMD_INVON`（tft_panel_st7789t.c:442）。
  3. 读 tft_panel.c:299，确认 `tft_panel_create` 随后调用 `esp_lcd_panel_disp_on_off(me->panel, true)` → 发送 `LCD_CMD_DISPON`（tft_panel_st7789t.c:514）。
  4. 对照 ESP-IDF v5.5.3 官方 `esp_lcd_panel_st7789.c:180-199`，确认官方 init 不发送 INVON/DISPON/RAMWR。
  5. 结论：INVON 和 DISPON 冗余但幂等（无功能影响）；RAMWR 无数据跟随后被下一条命令中断，无实际效果。修复方案（移除三条命令）正确。

### 发现 #5 — `TFT_PANEL_SPI_HOST` 和 `TFT_PANEL_PIXEL_CLOCK_HZ` 硬编码

- **验证结论**: 确认。tft_panel.c:36-37 硬编码 `SPI3_HOST` 和 `12*1000*1000`，`tft_panel_config_t`（tft_panel.h:41-50）无对应字段。对单板项目可接受。

### 发现 #6 — `tft_panel_st7789t_init` 圈复杂度高

- **验证结论**: 确认。tft_panel_st7789t.c:312-386 包含约 20 条顺序 `ESP_RETURN_ON_ERROR` 调用。逻辑为线性序列无分支，但函数较长。建议重构为命令表，优先级低。

### 发现 #7 — init 中使用魔数 `0x36` 而非 `LCD_CMD_MADCTL`

- **验证结论**: 确认。tft_panel_st7789t.c:324 使用 `0x36`，而同文件 line 471、491 使用 `LCD_CMD_MADCTL`。`0x3A`（line 327）同理应为 `LCD_CMD_COLMOD`。`#include "esp_lcd_panel_commands.h"` 已在 line 21 引入，常量可用。

### 发现 #8 — `tft_panel_draw_bitmap` 未文档化 `color_data` 长度契约

- **验证结论**: 确认。tft_panel.h:97-113 的 Doxygen 注释未说明 `color_data` 缓冲区长度要求。`tft_panel_st7789t_draw_bitmap`（tft_panel_st7789t.c:429-433）根据区域面积计算 `len` 并传给 `esp_lcd_panel_io_tx_color`，若缓冲区不足则 DMA 越界读。API 签名与 ESP-IDF `esp_lcd_panel_draw_bitmap` 一致（无长度参数），属行业惯例，但文档应补充长度契约。

### 发现 #9 — `tft_panel_create` 硬编码 mirror 和 gap 值

- **验证结论**: 确认。tft_panel.c:281 硬编码 `mirror(true, false)`，tft_panel.c:293 + tft_panel.c:39-40 硬编码 `TFT_PANEL_X_GAP=34, TFT_PANEL_Y_GAP=0`。这些是 ESP32-S3-LCD-1.47B 板级参数。对单板项目可接受。

---

## ❌ 误报

（无）

---

## ⚠️ 部分正确（需调整修复方案）

（无）

---

## 修复记录

N/A — 本轮为 review-only，未修改源代码。

---

## 模块交付清单

### Change summary

N/A（review-only，无代码改动）。

### Resource budget

| 资源 | 大小 | 计算式 | 来源 |
|------|------|--------|------|
| `tft_panel_t` 结构体 | ~80 B | `sizeof(struct tft_panel)` | tft_panel.c:184 `calloc` |
| mutex | ~80 B | `xSemaphoreCreateMutex()` 内部分配 | tft_panel.c:192 |
| SPI DMA 缓冲区 | ~6.9 KB | `panel_width × TFT_PANEL_BUFFER_LINES × sizeof(uint16_t)` = 172×20×2 = 6880 B | tft_panel.c:224-225 `max_transfer_sz` |
| SPI DMA 描述符 | ~256 B | ESP-IDF 内部分配（`SPI_DMA_CH_AUTO`） | tft_panel.c:227 `spi_bus_initialize` |
| **tft_panel 启动 heap 总计** | **~7.3 KB** | — | — |
| LVGL draw buffer ×2 | ~13.8 KB | `width × LVGL_DASHBOARD_DRAW_BUF_LINES(20) × RGB565` = 172×20×2 = 6880 B ×2 | lvgl_dashboard.c:484-489 `heap_caps_malloc(MALLOC_CAP_DMA \| MALLOC_CAP_INTERNAL)` |
| LVGL task stack | 6144 B | `LVGL_DASHBOARD_DEFAULT_TASK_STACK` | lvgl_dashboard.c:40 |
| **显示路径总计** | **~27 KB** | tft_panel + lvgl_dashboard | — |

- 启动 heap：~7.3 KB（tft_panel）+ ~14 KB（LVGL draw buffers）= ~21 KB
- 运行 heap：无动态分配（draw_bitmap 不 malloc）
- 峰值 heap：~21 KB（启动时一次性分配）
- task stack：6144 B（LVGL task，由 lvgl_dashboard 创建）
- queue size：无队列
- buffer size：SPI DMA max_transfer_sz = 6880 B；LVGL draw buffer = 6880 B ×2

**一致性验证**：`max_transfer_sz`（6880 B）≥ LVGL 单次最大 flush 区域（width × draw_buf_lines × bpp = 6880 B），不会发生传输超限。

### Lifecycle / ownership notes

| 数据 | Ownership | 说明 |
|------|-----------|------|
| `tft_panel_t *` | owned by main.c | main.c 创建，借用给 lvgl_dashboard 和 app_controller |
| `tft_panel_config_t` | owned by tft_panel | create 时值拷贝到 `me->config` |
| `color_data`（draw_bitmap 参数）| borrowed | 调用方（LVGL）拥有，必须保持有效直到 `flush_done_cb` 被调用 |
| `flush_done_cb` + `flush_done_ctx` | owned by tft_panel | 值拷贝存储在 `me->flush_done_cb` / `me->flush_done_ctx`，由 `callback_lock` spinlock 保护 |
| SPI 传输缓冲区 | owned by ESP-IDF SPI 驱动 | `spi_bus_initialize` 内部分配，`spi_bus_free` 释放 |
| `esp_lcd_panel_io_handle_t io` | owned by tft_panel | `esp_lcd_new_panel_io_spi` 创建，`esp_lcd_panel_io_del` 释放 |
| `esp_lcd_panel_handle_t panel` | owned by tft_panel | `tft_panel_new_st7789t` 创建，`esp_lcd_panel_del` 释放 |

### Failure-path review

| 失败场景 | 处理方式 | 是否完备 |
|----------|----------|----------|
| `calloc(tft_panel_t)` 失败 | 返回 NULL，无资源泄漏 | ✅ |
| `xSemaphoreCreateMutex` 失败 | `cleanup_create_failure` → free(me) → 返回 NULL | ✅ |
| `gpio_config`(背光) 失败 | `cleanup_create_failure` → 删 mutex → free(me) → 返回 NULL | ✅ |
| `spi_bus_initialize` 失败 | `cleanup_create_failure` → 删 mutex → reset bl GPIO → free(me) → 返回 NULL | ✅ |
| `esp_lcd_new_panel_io_spi` 失败 | `cleanup_create_failure` → free SPI bus → 删 mutex → reset bl GPIO → free(me) → 返回 NULL | ✅ |
| `tft_panel_new_st7789t` 失败 | `cleanup_create_failure` → del panel IO → free SPI bus → 删 mutex → reset bl GPIO → free(me) → 返回 NULL | ✅ |
| `esp_lcd_panel_reset/init/...` 失败 | `cleanup_create_failure` → del panel → del panel IO → free SPI bus → 删 mutex → reset bl GPIO → free(me) → 返回 NULL | ✅ |
| `draw_bitmap` 状态/参数校验失败 | 返回 `ESP_ERR_INVALID_STATE`/`ESP_ERR_INVALID_ARG`，不调用 SPI | ✅ |
| SPI 传输失败（`esp_lcd_panel_draw_bitmap` 返回错误）| 调用方 `lvgl_dashboard_display_flush`（lvgl_dashboard.c:1382-1385）调用 `lv_display_flush_ready` + `complete_flush`，通知 LVGL 不再等待 | ✅ |
| `destroy` 中 `esp_lcd_panel_del` 失败 | 保留错误码，重置 `destroying=false`，返回错误，允许重试 | ✅ |
| `destroy` 中 `esp_lcd_panel_io_del` 失败 | 同上 | ✅ |
| `destroy` 中 `spi_bus_free` 失败 | 同上 | ✅ |
| `destroy` 期间 DMA 传输在途 | `esp_lcd_panel_io_del` 内部等待 pending 传输完成并注销 ISR 回调；`me` 在 `free(me)` 前一直有效 | ✅ |

**SPI 传输失败 / 超时后 RX 清理**：tft_panel 为 TX-only（无 SPI RX），无 RX 缓冲区需清理。`flush_done_cb` 在传输失败时由调用方（lvgl_dashboard）通过 `lv_display_flush_ready` 显式调用，不会卡住 LVGL。

### Cross-module contract review

| 契约 | 验证结果 |
|------|----------|
| tft_panel 位于驱动适配层，仅依赖 ESP-IDF 驱动 | ✅ 依赖：driver/spi_master.h、driver/gpio.h、esp_lcd_*。不依赖上层模块 |
| `tft_panel_t *` 被 lvgl_dashboard 借用（borrowed） | ✅ lvgl_dashboard.h:78 注释标注"Borrowed TFT panel handle" |
| `color_data` 为 borrowed，调用方保证有效期 | ✅ LVGL 通过 `flush_done_cb` 机制保证 |
| `flush_done_cb` 在 ISR 上下文调用 | ⚠️ 实现正确，但 tft_panel.h 未文档化该约束（发现 #2） |
| 非 LVGL 任务不直接操作 widget | ✅ tft_panel 不操作 widget，只做 SPI 传输 |
| 分层依赖规则（architecture.md §8）| ✅ tft_panel 依赖：driver/spi_master.h、driver/gpio.h（与 classes.md 一致） |
| 公共 API 与 classes.md §11.2-11.3 一致 | ⚠️ 公共 API 签名一致，内部结构体文档漂移（发现 #3） |

### Residual risks

1. **MADCTL init bug（发现 #1）**：当前被 `mirror()` 调用掩盖，但若调用序列变更则暴露。建议修复。
2. **`color_data` 长度无运行时校验（发现 #8）**：API 签名无长度参数，无法在运行时校验。依赖调用方契约。若未来新增非 LVGL 调用方，需特别注意。
3. **板级硬编码参数（发现 #5、#9）**：SPI host、像素时钟、mirror 方向、gap 值均为硬编码。更换面板需改源码。
4. **`destroy` 中 `portMAX_DELAY` 阻塞**：若 SPI 传输异常挂起，`destroy` 可能永久阻塞。ESP-IDF `esp_lcd_panel_io_del` / `spi_bus_free` 内部有超时机制，但极端情况下仍可能阻塞。
5. **上机验证风险**：init 序列中的厂商命令（0xB0~0xE1）参数为面板专有，需对照 ST7789T datasheet 和 ESP32-S3-LCD-1.47B 原理图确认正确性。本次 review 未验证厂商命令参数。
