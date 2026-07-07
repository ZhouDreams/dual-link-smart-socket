# Verification: app_controller + main.c

## ✅ 确认的问题

### M1. `app_controller_destroy` 在 `stop` 失败时泄漏对象和 mutex
- **原报告条目**: M1
- **验证结论**: 重新阅读 `app_controller.c:255-273`，确认 `ret = app_controller_stop(me); if (ret != ESP_OK) { return ret; }` 在 stop 失败时直接返回，跳过 `vSemaphoreDelete(me->mutex)` 和 `free(me)`。调用方无法通过再次调用 `destroy` 恢复（stop 可能持续失败）。`err.md` §3.2 要求 destroy 安全回收资源。**确认**。

### M2. `app_controller_stop_impl` 末尾 mutex take 失败时 `stopping` 标志不清除
- **原报告条目**: M2
- **验证结论**: 重新阅读 `app_controller.c:409-411`，确认 `if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE)` 分支直接 return，跳过 `me->stopping = false`（行 416）。`portMAX_DELAY` 使 take 失败概率极低，但一旦发生，`stopping=true` 永久锁定 start/stop。**确认**，但注意概率极低。

### M3. 上行遥测 publish 在 esp_event 任务中执行，可能延迟安全保护判定
- **原报告条目**: M3
- **验证结论**: 
  1. `app_controller_start`（`app_controller.c:298-308`）先调用 `app_controller_register_event_handlers`（step 2），后调用 `app_controller_start_modules`（step 5，行 325）。
  2. `app_controller_register_event_handlers`（行 697-747）注册 `METERING_EVENT_SNAPSHOT` handler 存入 `me->metering_handler`。
  3. `app_controller_start_modules`（行 817-878）调用 `safety_guard_start`（行 839）。
  4. `safety_guard_start`（`safety_guard.c:213-216`）注册 `METERING_EVENT_SNAPSHOT` handler。
  5. esp_event 按注册顺序派发：app_controller 的 metering handler 先于 safety_guard 的 handler 执行。
  6. `app_controller_on_metering_snapshot`（行 488-509）调用 `app_controller_publish_telemetry`（行 505），后者调用 `thingsboard_client_publish_telemetry`（行 1029）。
  7. `thingsboard_client_publish_telemetry`（`thingsboard_client.c:332-384`）内部做 JSON 格式化 + `calloc` + `network_manager_publish`（行 379），network publish 可能阻塞（LTE 链路）。
  **确认**。Wi-Fi 链路延迟可忽略（`esp_mqtt_client_enqueue` 非阻塞），LTE 链路存在阻塞风险。

### M4. 1Hz 遥测 publish 使用 `ESP_LOGI` 违反日志规则
- **原报告条目**: M4
- **验证结论**: 重新阅读 `app_controller.c:1039-1049`，确认 `s_telemetry_publish_count++; ESP_LOGI(TAG, "telemetry publish #%lu ok: ...")` 在每次 `thingsboard_client_publish_telemetry` 返回 `ESP_OK` 后执行。metering snapshot 频率 1Hz（`main.c:106` `.sample_period_ms = 1000`），即每秒输出一条 `ESP_LOGI`。`err.md` §6 明确规定"周期性遥测...默认不使用 `ESP_LOGI`"。**确认**。

### M5. `on_tb_command` 命令处理失败时不发送 RPC 错误响应
- **原报告条目**: M5
- **验证结论**: 重新阅读 `app_controller.c:545-600`，确认：
  - `GET_POWER_LIMIT`：若 `safety_guard_get_thresholds`（行 568）失败或 `format_power_limit_response`（行 570）失败，跳过 `send_rpc_response`（行 574），仅在最外层 `ESP_LOGW`（行 597）。
  - `SET_RELAY`：若 `relay_set` 失败（行 561），跳过 `report_relay_state`，仅 `ESP_LOGW`。
  - `SET_POWER_LIMIT`：若 `safety_guard_set_thresholds` 失败（行 583），跳过 `report_power_limit`，仅 `ESP_LOGW`。
  - `default`：返回 `ESP_ERR_INVALID_ARG`，无 RPC 响应。
  TB RPC 协议中 `GET_POWER_LIMIT` 期望 response，无响应会导致 TB 端超时。**确认**。

### M6. `app_controller_internal.c` 引用 `thingsboard_client_internal.h` 跨模块内部头文件
- **原报告条目**: M6
- **验证结论**: `rg` 搜索确认 `thingsboard_client_internal.h` 仅被 3 个文件 include：`thingsboard_client.c`、`thingsboard_client_internal.c`（同模块内）和 `app_controller_internal.c`（跨模块）。`app_controller_internal.c:17` include 该头文件，`app_controller_internal.c:101` 调用 `tb_internal_format_power_limit_response`。`thingsboard_client_internal.h` 位于 `main/thingsboard/` 目录，定义了 `tb_internal_*` 系列内部函数。architecture.md §8.1 允许 app_controller 依赖"所有公共模块 API"，内部头文件不在公共 API 范围内。**确认**。

### M7. classes.md §15.3 内部结构体文档严重过时
- **原报告条目**: M7
- **验证结论**: 对比 `classes.md:2064-2068`（`struct app_controller { app_controller_config_t cfg; bool started; };`）与 `app_controller.c:37-58`（实际 15 个字段含 mutex、3 个 handler、relay_on/known、screen_enabled、started/starting/stopping、6 个回调注册标志、6 个模块启动标志）。文档与实现严重不符。**确认**。

### M8. classes.md §15.5 `SET_POWER_LIMIT` 示例传 0 作 overcurrent 阈值
- **原报告条目**: M8
- **验证结论**: 对比 `classes.md:2145`（`safety_guard_set_thresholds(app->cfg.safety, 0, cmd->power_limit_w);`）与 `app_controller.c:580-585`（实际先 `safety_guard_get_thresholds(&current_a, NULL)` 再 `safety_guard_set_thresholds(current_a, cmd->power_limit_w)`）。文档示例将 overcurrent 阈值置 0，实际代码保留原值。文档是危险的错误模式。**确认**。

### L1. `app_controller_stop_impl` 等待 `starting` 标志无超时
- **原报告条目**: L1
- **验证结论**: 重新阅读 `app_controller.c:368-373`，确认 `while (!from_start_rollback && me->starting)` 循环内 `vTaskDelay(pdMS_TO_TICKS(10))` 无退出计数器或超时。**确认**。

### L2. main.c 对象创建失败时未销毁已创建对象
- **原报告条目**: L2
- **验证结论**: 重新阅读 `main.c:84-88`（pinmap NULL → idle）、`main.c:127-131`（基础模块失败 → idle）、`main.c:191-198`（网络模块失败 → idle）、`main.c:221-224`（app/display 失败 → idle），确认所有失败路径直接 `smart_socket_idle_forever()` 无 destroy 调用。已创建对象的 destroy 逻辑（如 relay 关断）不会执行。**确认**。

### L3. main.c TAG 定义方式与项目其他文件不一致
- **原报告条目**: L3
- **验证结论**: `main.c:58` 使用 `static const char *TAG = "main";`；`app_controller.c:29` 使用 `#define TAG "app_controller"`。`rg` 搜索确认项目内两种风格并存。**确认**。

### L4. `s_telemetry_publish_count` 为文件级 static 变量
- **原报告条目**: L4
- **验证结论**: `app_controller.c:212` 定义 `static uint32_t s_telemetry_publish_count;`，在 `publish_telemetry`（行 1039）中递增。非实例字段，多实例会共享。**确认**。

### L5. `app_controller_publish_telemetry` mutex take 失败时静默使用默认值
- **原报告条目**: L5
- **验证结论**: 重新阅读 `app_controller.c:988-992`，确认 `if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE) { relay_on = me->relay_on; ... }` 无 else 分支，take 失败时 `relay_on` 保持初始值 `false`、`relay_known` 保持 `false`，静默上报错误数据。**确认**，概率极低。

### L6. `set_flag` 在动作完成后调用，失败时动作不可回滚
- **原报告条目**: L6
- **验证结论**: 重新阅读 `app_controller.c:310-323`（tb_command_registered）、`app_controller.c:604-616`（button_single_registered）、`app_controller.c:819-827`（bl0942_started），确认模式为"执行动作 → set_flag"。set_flag 内部用 `portMAX_DELAY` take mutex（行 673），失败返回 `ESP_ERR_TIMEOUT`。若 set_flag 失败，cleanup 路径因 flag=false 跳过对应模块。**确认**，概率极低。

---

## ❌ 误报

（无）

---

## ⚠️ 部分正确（需调整修复方案）

（无——所有发现均在验证中确认）

---

## 修复记录

N/A（review-only，无代码改动）

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget

**app_controller 自身分配**:
- `calloc(1, sizeof(struct app_controller))` ≈ 80 字节（15 个字段：1 config struct + 1 mutex + 3 handler instances + 12 bool flags）
- `xSemaphoreCreateMutex()` ≈ 80 字节（FreeRTOS mutex）
- `s_telemetry_publish_count`：4 字节 static BSS
- `response[APP_CONTROLLER_RPC_BUF_SIZE]`：96 字节栈（在 TB command 回调上下文中）
- 总计：约 160 字节 heap + 4 字节 BSS + 96 字节栈

**main.c 分配（对象创建）**:
- relay: `calloc` ≈ 32 字节 + mutex
- button: `calloc` ≈ 64 字节 + iot_button 内部分配
- bl0942: `calloc` ≈ 80 字节 + mutex + UART RX buffer 256 字节 + sample task stack
- metering_service: `calloc` ≈ 80 字节 + mutex
- safety_guard: `calloc` ≈ 64 字节 + mutex
- tft_panel: `calloc` ≈ 48 字节 + SPI 驱动
- wifi_link: `calloc` + sub_table (`max_subscriptions * sizeof(sub_entry)`)
- lte_link（若启用）: `calloc` + sub_table + esp-lwlte 内部分配
- network_manager: `calloc` + sub_table (`max_subscriptions * sizeof(sub_entry)`)
- thingsboard_client: `calloc` + json_buf (`CONFIG_SMART_SOCKET_TB_JSON_BUF_SIZE`)
- lvgl_dashboard: `calloc` + LVGL framebuffer (`panel_width * panel_height * bpp`) + LVGL task stack 6144 字节
- app_controller: 见上

**乘法型分配（在 main.c 参数中配置）**:
- BL0942 RX buffer: `rx_buf_size = 256` 字节（单 buffer）
- wifi_link sub_table: `CONFIG_SMART_SOCKET_WIFI_MAX_SUBSCRIPTIONS * sizeof(wifi_link_sub_entry_t)` = `N * (max_topic_len + 1 + 4 + 1)`
- lte_link sub_table: 同上
- network_manager sub_table: `CONFIG_SMART_SOCKET_NET_MGR_MAX_SUBSCRIPTIONS * sizeof(network_manager_sub_entry_t)`
- thingsboard_client json_buf: `CONFIG_SMART_SOCKET_TB_JSON_BUF_SIZE` 字节
- LVGL framebuffer: `CONFIG_SMART_SOCKET_PANEL_WIDTH * CONFIG_SMART_SOCKET_PANEL_HEIGHT * bytes_per_pixel`（在 lvgl_dashboard 模块中分配，main.c 配置面板尺寸 172×320）
- LVGL task stack: 6144 字节

**注意**：app_controller 本身无乘法型分配。大块分配（LVGL framebuffer、sub_table、json_buf）在各模块自身 review 中审查。

### Lifecycle / ownership notes

| 数据 | Ownership | 说明 |
|------|-----------|------|
| `app_controller_config_t` 中的模块句柄 | Borrowed | main.c 创建并拥有，app_controller 借用，destroy 不释放 |
| `app_controller_config_t` 中的 `event_loop` | Borrowed | 可为 NULL（使用默认循环） |
| `app_controller_config_t` 中的 `pinmap` | Borrowed | 只读单例 |
| `metering_snapshot_t *event_data` | Borrowed | esp_event 派发期间有效，handler 内使用不跨回调持有 |
| `safety_guard_snapshot_t *event_data` | Borrowed | 同上 |
| `relay_state_changed_event_t *event_data` | Borrowed | 同上 |
| `tb_command_t *cmd` | Borrowed | TB 回调期间有效 |
| `response[96]` 栈 buffer | Owned（栈） | on_tb_command 内分配，函数结束自动释放 |
| `tb_telemetry_input_t input` | Owned（栈） | publish_telemetry 内构造，传入 TB 后不持有 |
| `thingsboard_client_publish_telemetry` 内 `json_copy` | Owned（heap） | TB 内部 calloc，publish 后 free |

### Failure-path review

| 失败场景 | 处理方式 | 评价 |
|----------|----------|------|
| `app_controller_create` calloc 失败 | 返回 NULL | ✅ 正确 |
| `app_controller_create` mutex 创建失败 | free(me) 返回 NULL | ✅ 正确 |
| `app_controller_start` 回调注册失败 | goto err → stop_impl(true) 反序清理 | ✅ 正确，flag 门控确保只清理已完成的步骤 |
| `app_controller_start` 模块 start 失败 | goto err → stop_impl(true) → stop_modules 按 flag 反序停止 | ✅ 正确 |
| `app_controller_stop` 模块 stop 失败 | `capture_first_error` 保留首个错误，继续清理其他模块 | ✅ 正确 |
| `app_controller_stop` 回调清理失败 | `filter_stop_error` 过滤 `ESP_ERR_INVALID_STATE`（模块已停止），其余记日志 | ✅ 正确 |
| `app_controller_destroy` stop 失败 | **返回错误，不释放 me 和 mutex** | ❌ M1：泄漏 |
| `app_controller_stop_impl` 末尾 mutex take 失败 | **返回错误，不清除 stopping 标志** | ❌ M2：永久锁定 |
| `thingsboard_client_publish_telemetry` calloc 失败 | 释放 mutex 返回 `ESP_ERR_NO_MEM` | ✅ TB 模块正确处理 |
| `publish_telemetry` 失败 | discard_energy_delta 释放 pending 增量 | ✅ 正确，不丢失电能数据 |
| `on_tb_command` relay_set 失败 | 跳过 report_relay_state，记日志 | ✅ 正确，但 M5：无 RPC 错误响应 |
| main.c 对象创建失败 | idle_forever，不 destroy 已创建对象 | ⚠️ L2：功能无影响，但不干净 |

### Cross-module contract review

| 契约 | 遵守情况 |
|------|----------|
| app_controller 不创建底层模块（classes.md §15.7） | ✅ main.c 创建所有对象，app_controller 只接收句柄 |
| 四条数据流经过 app_controller（architecture.md §6） | ✅ 上行遥测/下行RPC/本地按键/安全保护均在 app_controller.c 中以回调实现 |
| 安全保护本地闭环（architecture.md §6.4） | ✅ on_safety_snapshot → relay_set 是本地 GPIO 调用，不依赖网络 |
| app_controller 不直接解析 BL0942 / 拼接 TB topic / 操作 LVGL widget / 判定安全规则（architecture.md §3.1） | ✅ |
| app_controller 是唯一知道所有模块的模块（architecture.md §8.1） | ✅ |
| 不直接依赖 wifi_link / lte_link | ✅ 通过 network_manager 间接使用 |
| **不依赖其他模块内部头文件** | ❌ M6：app_controller_internal.c include thingsboard_client_internal.h |
| TB RPC SET_POWER_LIMIT 保留 overcurrent 阈值 | ✅ 实际代码正确（先读后写），但 classes.md §15.5 文档示例错误（M8） |
| 遥测 handler 不阻塞安全判定路径 | ⚠️ M3：handler 注册顺序导致 telemetry publish 先于 safety_guard 判定 |

### Residual risks

1. **M3 安全路径延迟**：Wi-Fi 链路下延迟可忽略（<1ms），LTE 链路下可能阻塞数十至数百毫秒。当前 `persistence_samples=3`（3s 窗口）使延迟占比小，但极端过流场景下仍值得关注。上机实测 LTE 链路下 `lwlte_mqtt_publish` 的实际阻塞时间可进一步量化风险。

2. **M2/M5/M6 概率极低**：`portMAX_DELAY` 使 mutex take 失败概率极低，但后果严重（永久锁定/无 RPC 响应/跨模块耦合）。

3. **L2 资源泄漏**：main.c 创建失败时未 destroy 已创建对象。系统进入 idle_forever，功能无影响，但 relay 等执行器可能停留在创建时的默认状态（relay_create 初始化时 GPIO 设为关闭电平）。

4. **并发 start/stop**：当前设计支持单任务调用 start/stop（main task），多任务并发场景下的 flag 竞态经分析基本安全（poll loop + flag 门控），但未经过压力测试验证。

5. **event_loop 配置**：`app_controller_config_t.event_loop` 支持自定义事件循环，但其他模块（metering_service/safety_guard/relay）使用默认循环。若配置非 NULL event_loop，handler 不会收到事件。main.c 使用 NULL（默认循环），当前无问题。
