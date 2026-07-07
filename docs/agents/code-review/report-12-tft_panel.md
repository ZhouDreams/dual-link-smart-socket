# Code Review: tft_panel + tft_panel_st7789t

**日期**: 2026-07-07
**文件**:
- main/display/tft/tft_panel.c
- main/display/tft/tft_panel.h
- main/display/tft/tft_panel_st7789t.c
- main/display/tft/tft_panel_st7789t.h

---

## 🔴 高严重度

（无）

---

## 🟡 中严重度

### 1. `tft_panel_st7789t_init` 硬编码 MADCTL=0x00，丢弃构造时设置的 BGR 位

- **文件:行号**: tft_panel_st7789t.c:324-326
- **问题描述**: `tft_panel_st7789t_init` 在初始化序列中向 MADCTL 寄存器（0x36）写入硬编码的 `0x00`，而不是使用构造时根据 `rgb_endian` 配置设置的 `st7789t->madctl_val`。当配置为 `LCD_RGB_ELEMENT_ORDER_BGR` 时，`tft_panel_new_st7789t`（line 205）将 `madctl_val` 设为 `LCD_CMD_BGR_BIT`，但 init 发送 `0x00`（RGB 顺序），BGR 位被丢失。
  
  目前该问题被 `tft_panel_create` 的调用顺序掩盖：init 之后紧接调用 `esp_lcd_panel_mirror(me->panel, true, false)`（tft_panel.c:281），`tft_panel_st7789t_mirror`（tft_panel_st7789t.c:460-474）在 `madctl_val` 上设置 MX 位后重新发送完整的 `madctl_val`（含 BGR 位），覆盖了 init 的错误值。最终硬件状态正确，但 init 本身发出的 MADCTL 是错误的。
  
  对照 ESP-IDF 官方 `esp_lcd_panel_st7789.c:188-190`，官方驱动在 init 中发送的是 `st7789->madctl_val`，而非硬编码值。本模块偏离了这一模式。
  
  风险：若未来有人单独调用 `esp_lcd_panel_init()` 而不随后调用 `mirror()`，BGR 位将丢失，导致颜色错误（红蓝互换）。这是一个潜伏 bug。

- **建议修复**: 将 init 中的 `(uint8_t[]){0x00}` 改为 `(uint8_t[]){st7789t->madctl_val}`，同时将魔数 `0x36` 改为命名常量 `LCD_CMD_MADCTL`（与 `mirror()`/`swap_xy()` 中的用法一致）：
  ```c
  ESP_RETURN_ON_ERROR(esp_lcd_panel_io_tx_param(st7789t->io, LCD_CMD_MADCTL,
                                                (uint8_t[]){st7789t->madctl_val}, 1),
                      TAG, "madctl init failed");
  ```

### 2. `tft_panel_flush_done_cb_t` 未文档化 ISR 调用上下文

- **文件:行号**: tft_panel.h:52-57
- **问题描述**: `tft_panel_flush_done_cb_t` 的 Doxygen 注释仅说明"刷新完成回调"，未提及该回调在 SPI DMA 传输完成中断（ISR）上下文中被调用。实际调用路径为：SPI DMA 完成 → `tft_panel_on_color_trans_done`（tft_panel.c:632-648，ISR 上下文）→ 直接调用 `cb(ctx)`。
  
  回调的调用上下文直接影响调用方必须遵守的约束：回调内不得调用阻塞 API（mutex take with timeout、malloc、vTaskDelay、重量级日志），只能做 ISR 安全操作（portENTER_CRITICAL_SAFE、xSemaphoreGiveFromISR、post-to-queue 等）。当前唯一调用方 `lvgl_dashboard_flush_ready`（lvgl_dashboard.c:1327-1339）正确地仅调用 `lv_display_flush_ready`（LVGL 文档标注 ISR 安全）和 `lvgl_dashboard_complete_flush`（使用 `portENTER_CRITICAL_SAFE`），未违反约束。但未来新增调用方时，缺乏文档约束可能导致在回调中误用阻塞 API。

- **建议修复**: 在 `tft_panel_flush_done_cb_t` 的 Doxygen 注释中增加 `@note` 说明回调在 SPI DMA 完成中断上下文中调用，必须短小非阻塞，不得调用 mutex take / malloc / vTaskDelay 等 API。

### 3. classes.md §11.3 内部结构体文档与实现不一致（文档漂移）

- **文件:行号**: docs/agents/classes.md:1531-1539（§11.3 内部结构）对比 tft_panel.c:52-64
- **问题描述**: classes.md §11.3 记录的 `struct tft_panel` 为：
  ```c
  struct tft_panel {
      tft_panel_config_t config;
      spi_device_handle_t spi;
      tft_panel_flush_done_cb_t flush_done_cb;
      void *flush_done_ctx;
      bool backlight_on;
      bool initialized;
  };
  ```
  实际实现（tft_panel.c:52-64）为：
  ```c
  struct tft_panel {
      tft_panel_config_t config;
      esp_lcd_panel_io_handle_t io;       // 文档无
      esp_lcd_panel_handle_t panel;       // 文档无
      SemaphoreHandle_t mutex;            // 文档无
      portMUX_TYPE callback_lock;         // 文档无
      tft_panel_flush_done_cb_t flush_done_cb;
      void *flush_done_ctx;
      bool backlight_on;
      bool initialized;
      bool spi_bus_initialized;           // 文档无
      bool destroying;                    // 文档无
  };
  ```
  差异：文档中有 `spi_device_handle_t spi` 字段（实际不存在），实际使用 ESP-IDF 的 `esp_lcd_panel_io_handle_t io` + `esp_lcd_panel_handle_t panel` 两层抽象；缺少 `mutex`、`callback_lock`、`spi_bus_initialized`、`destroying` 四个字段。这表明实现经过重构（从直接使用 `spi_device_handle_t` 改为使用 `esp_lcd_panel_io` + `esp_lcd_panel`），但文档未同步更新。

- **建议修复**: 更新 classes.md §11.3 的内部结构体定义，使其与 tft_panel.c:52-64 一致。

---

## 🟢 低严重度

### 4. `tft_panel_st7789t_init` 发送冗余的 INVON / DISPON / RAMWR 命令

- **文件:行号**: tft_panel_st7789t.c:378-383
- **问题描述**: init 末尾发送了三条命令：
  - `0x21`（LCD_CMD_INVON，line 378）— `tft_panel_create` 随后调用 `esp_lcd_panel_invert_color(me->panel, true)`（tft_panel.c:287）会再次发送 INVON。
  - `0x29`（LCD_CMD_DISPON，line 380）— `tft_panel_create` 随后调用 `esp_lcd_panel_disp_on_off(me->panel, true)`（tft_panel.c:299）会再次发送 DISPON。
  - `0x2C`（LCD_CMD_RAMWR，line 382）— 发送 RAMWR 但不跟数据，下一个命令（来自 `mirror()`）会中断 RAM 写入模式，此命令无实际效果。
  
  对照 ESP-IDF 官方 `esp_lcd_panel_st7789.c:180-199`，官方 init 仅发送 SLPOUT + MADCTL + COLMOD + RAMCTRL，不发送 INVON/DISPON/RAMWR，将这些留给调用方通过 `invert_color()` / `disp_on_off()` 控制。
  
  冗余命令不会造成功能错误（INVON 和 DISPON 是幂等的），但增加了 init 序列的混淆度，且 RAMWR 无数据是反常的。

- **建议修复**: 从 init 中移除 `0x21`、`0x29`、`0x2C` 三条命令，让 `tft_panel_create` 通过 `invert_color()` 和 `disp_on_off()` 统一控制。

### 5. `TFT_PANEL_SPI_HOST` 和 `TFT_PANEL_PIXEL_CLOCK_HZ` 硬编码

- **文件:行号**: tft_panel.c:36-37
- **问题描述**: SPI host（`SPI3_HOST`）和像素时钟（12 MHz）硬编码为编译期常量，未通过 `tft_panel_config_t` 暴露给调用方。对于 ESP32-S3-LCD-1.47B 单板项目这是可接受的，但如果未来更换面板或 SPI host，需要修改源码而非配置。
- **建议修复**: 可将 SPI host 和像素时钟加入 `tft_panel_config_t`，或保持现状但在注释中说明这是板级硬编码。优先级低。

### 6. `tft_panel_st7789t_init` 圈复杂度高

- **文件:行号**: tft_panel_st7789t.c:312-386
- **问题描述**: init 函数包含约 20 条顺序 `ESP_RETURN_ON_ERROR` 调用，圈复杂度约 20+。虽然逻辑是简单线性序列（无分支），但函数较长。可考虑将厂商初始化命令（0xB0~0xE1）提取为命令表 + 循环发送。
- **建议修复**: 可将命令序列重构为 `static const struct { uint8_t cmd; uint8_t params[N]; } init_cmds[]` 表 + 循环，降低代码行数。优先级低。

### 7. init 中使用魔数 `0x36` 而非 `LCD_CMD_MADCTL`

- **文件:行号**: tft_panel_st7789t.c:324
- **问题描述**: init 中 MADCTL 寄存器地址使用魔数 `0x36`，而同文件的 `mirror()`（line 471）和 `swap_xy()`（line 491）使用命名常量 `LCD_CMD_MADCTL`。同理，init 中 `0x3A` 应为 `LCD_CMD_COLMOD`，`0xB0` 等厂商命令也缺乏命名常量。不一致的命令引用风格降低可读性。
- **建议修复**: 统一使用 `esp_lcd_panel_commands.h` 中的命名常量。厂商专有命令（0xB0 等）可定义模块内 `#define`。

### 8. `tft_panel_draw_bitmap` 未文档化 `color_data` 长度契约

- **文件:行号**: tft_panel.h:97-113
- **问题描述**: `tft_panel_draw_bitmap` 的 Doxygen 注释说明 `color_data` 为"RGB565 像素数据"，但未明确说明缓冲区长度必须至少为 `(x2-x1)*(y2-y1)*bytes_per_pixel` 字节。实际在 `tft_panel_st7789t_draw_bitmap`（tft_panel_st7789t.c:429-433）中，传输长度 `len` 由区域面积计算，若调用方提供的缓冲区小于 `len`，DMA 将越界读。当前唯一调用方 LVGL 总是提供正确大小的缓冲区，但 API 缺乏对长度契约的文档约束。此 API 签名与 ESP-IDF `esp_lcd_panel_draw_bitmap` 一致（无长度参数），属行业惯例。
- **建议修复**: 在 `color_data` 参数注释中增加长度要求说明，例如"缓冲区长度必须 ≥ (x2-x1)*(y2-y1)*2 字节（RGB565）"。

### 9. `tft_panel_create` 硬编码 `mirror(true, false)` 和 gap 值

- **文件:行号**: tft_panel.c:281（mirror）、tft_panel.c:293（gap）、tft_panel.c:39-40（gap 常量）
- **问题描述**: `tft_panel_create` 硬编码了 `esp_lcd_panel_mirror(me->panel, true, false)` 和 `TFT_PANEL_X_GAP=34, TFT_PANEL_Y_GAP=0`。这些是 ESP32-S3-LCD-1.47B 板级参数，未通过配置暴露。若更换面板型号或安装方向，需修改源码。
- **建议修复**: 可将 mirror 方向和 gap 值加入 `tft_panel_config_t`，或保持现状。优先级低（单板项目）。

---

## 无问题维度

- **A. 资源账本与乘法型分配**: `max_transfer_sz = panel_width × TFT_PANEL_BUFFER_LINES × sizeof(uint16_t)` = 172×20×2 = 6880 字节（tft_panel.c:224-225），与 LVGL draw buffer 大小 `width × LVGL_DASHBOARD_DRAW_BUF_LINES(20) × RGB565` = 6880 字节（lvgl_dashboard.c:484-485）一致，不会发生单次传输超过 `max_transfer_sz` 的情况。SPI DMA 缓冲区由 ESP-IDF `spi_bus_initialize` 内部分配（`SPI_DMA_CH_AUTO`），大小受 `max_transfer_sz` 约束。`tft_panel_t` 结构体约 80 字节，mutex 约 80 字节，总 heap 占用 < 8 KB，合理。
- **B. 内存安全与 DMA/cache 一致性**: LVGL draw buffer 使用 `heap_caps_malloc(MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL)` 分配（lvgl_dashboard.c:486-489），确保 DMA 兼容内存区域和对齐。Cache 一致性由 ESP-IDF SPI 驱动在 `esp_lcd_panel_io_tx_color` 内部处理（writeback before DMA）。`color_data` 为 borrowed，调用方（LVGL）通过 `flush_done_cb` 机制保证缓冲区在 DMA 完成前不被复用。无 uint 下溢风险（区域校验 `x2 > x1, y2 > y1` 在 `draw_bitmap` 和 `st7789t_draw_bitmap` 两层都有）。
- **C. 并发与实时性**: `mutex`（`xSemaphoreCreateMutex`）保护 `draw_bitmap`/`set_backlight`/`register_flush_done_cb`/`destroy` 的共享状态；`callback_lock`（`portMUX_TYPE` spinlock）保护 `flush_done_cb`/`flush_done_ctx` 的 ISR 安全读写。ISR 回调 `tft_panel_on_color_trans_done` 仅做 load-cb + call-cb，短小非阻塞。`destroy` 通过 `destroying` 标志防止重入，`esp_lcd_panel_io_del` / `spi_bus_free` 内部等待 pending 传输完成。无死锁风险（mutex 不跨链路切换持锁，不回调查调持锁）。SPI 设备仅被 LVGL task 访问（通过 `draw_bitmap`），无多任务并发访问。
- **D. 失败路径完整性**: `tft_panel_create` 每一步失败都调用 `tft_panel_cleanup_create_failure` 完整回滚（backlight GPIO → panel → IO → SPI bus → mutex → free）。`tft_panel_destroy` 部分失败时保留首个错误码，重置 `destroying` 标志允许重试。SPI 传输失败（`esp_lcd_panel_draw_bitmap` 返回错误）由调用方（lvgl_dashboard.c:1382-1385）处理——调用 `lv_display_flush_ready` 通知 LVGL 并完成 flush 跟踪，不会卡住 LVGL 等 flush_done。无 `abort()` 类宏在 init 路径使用。
- **E. 跨模块契约**: `tft_panel` 作为驱动适配层，仅依赖 ESP-IDF 驱动（spi_master、gpio、esp_lcd），不依赖上层模块。`tft_panel_t *` 被 `lvgl_dashboard` 借用（borrowed），`color_data` 在 `draw_bitmap` 中为 borrowed。`flush_done_cb` 契约正确（ISR 上下文 + 短小非阻塞）。分层契约无违反。
- **F. 类型与边界**: `draw_bitmap` 的区域校验完整（x1≥0, y1≥0, x2>x1, y2>y1, x2≤panel_width, y2≤panel_height）。`st7789t_draw_bitmap` 的 `len` 计算使用 `size_t`，无溢出风险（172×320×24/8 ≈ 165KB，远小于 `size_t` 上限）。`max_transfer_sz` 计算使用 `int`，对实际面板尺寸无溢出风险。
- **G. 代码质量**: 双语注释格式、section 组织、include 风格均符合 coding-style.md。static 函数注释放在 STATIC PROTOTYPES 区域，符合规范。
- **H. 文档一致性**: 公共 API（tft_panel.h）与 classes.md §11.2-11.4 的公共 API 签名一致。内部结构体文档漂移见发现 #3。
