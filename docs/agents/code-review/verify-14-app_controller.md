# Verification: app_controller + main.c

## ✅ 确认的问题

### M2. `app_controller_stop_impl` 末尾 mutex take 失败时 `stopping` 标志不清除
- **原报告条目**: M2
- **验证结论**: 重新阅读 `app_controller.c:409-411`，确认 `if (xSemaphoreTake(me->mutex, portMAX_DELAY) != pdTRUE)` 分支直接 return，跳过 `me->stopping = false`（行 416）。`portMAX_DELAY` 使 take 失败概率极低，但一旦发生，`stopping=true` 永久锁定 start/stop。**确认**，但注意概率极低。

### M4. 1Hz 遥测 publish 使用 `ESP_LOGI` 违反日志规则
- **原报告条目**: M4
- **验证结论**: 重新阅读 `app_controller.c:1039-1049`，确认 `s_telemetry_publish_count++; ESP_LOGI(TAG, "telemetry publish #%lu ok: ...")` 在每次 `thingsboard_client_publish_telemetry` 返回 `ESP_OK` 后执行。metering snapshot 频率 1Hz（`main.c:106` `.sample_period_ms = 1000`），即每秒输出一条 `ESP_LOGI`。`err.md` §6 明确规定"周期性遥测...默认不使用 `ESP_LOGI`"。**确认**。

### M6. `app_controller_internal.c` 引用 `thingsboard_client_internal.h` 跨模块内部头文件
- **原报告条目**: M6
- **验证结论**: `rg` 搜索确认 `thingsboard_client_internal.h` 仅被 3 个文件 include：`thingsboard_client.c`、`thingsboard_client_internal.c`（同模块内）和 `app_controller_internal.c`（跨模块）。`app_controller_internal.c:17` include 该头文件，`app_controller_internal.c:101` 调用 `tb_internal_format_power_limit_response`。`thingsboard_client_internal.h` 位于 `main/thingsboard/` 目录，定义了 `tb_internal_*` 系列内部函数。architecture.md §8.1 允许 app_controller 依赖"所有公共模块 API"，内部头文件不在公共 API 范围内。**确认**。

### M7. classes.md §15.3 内部结构体文档严重过时
- **原报告条目**: M7
- **验证结论**: 对比 `classes.md:2064-2068`（`struct app_controller { app_controller_config_t cfg; bool started; };`）与 `app_controller.c:37-58`（实际结构体共有 20 个字段：1 个 config、1 个 mutex、3 个 handler、以及 15 个状态/标志字段）。文档与实现严重不符。**确认**。

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

### M1. `app_controller_destroy` 在 `stop` 失败时存在泄漏路径，但规范依据与修复策略需收敛
- **原报告条目**: M1
- **验证结论**: 重新阅读 `app_controller.c:255-272`，确认 `ret = app_controller_stop(me); if (ret != ESP_OK) { return ret; }` 在 `stop` 失败时直接返回，跳过 `vSemaphoreDelete(me->mutex)` 和 `free(me)`，因此对象和 mutex 的未释放路径成立。需要调整的是论据与修复口径：`docs/agents/err.md` §3.2 约束的是生命周期 API 的一般写法，不能直接推出“`destroy` 失败时也必须强制 free”这一更强语义。更准确的结论应是：当前 `destroy` 失败路径会泄漏本对象资源，是否改为吞掉 `stop` 错误后继续释放，或拆分 `stop/cleanup` 契约，需要单独设计。

### M3. 上行遥测 publish 在 esp_event 任务中执行，可能延迟安全保护判定，但链路论据需修正
- **原报告条目**: M3
- **验证结论**:
  1. `app_controller_start`（`app_controller.c:298-308`）先调用 `app_controller_register_event_handlers`，后调用 `app_controller_start_modules`（行 325）。
  2. `app_controller_register_event_handlers`（`app_controller.c:697-747`）先注册 `METERING_EVENT_SNAPSHOT` handler。
  3. `app_controller_start_modules`（`app_controller.c:817-878`）中才调用 `safety_guard_start`（行 839）。
  4. `safety_guard_start`（`safety_guard.c:186-216`）随后注册自己的 `METERING_EVENT_SNAPSHOT` handler。
  5. 因此 esp_event 会先执行 `app_controller_on_metering_snapshot`，再执行 `safety_guard` 的 metering handler。
  6. `app_controller_on_metering_snapshot`（`app_controller.c:488-509`）会同步调用 `app_controller_publish_telemetry`，后者再进入 `thingsboard_client_publish_telemetry` 和 `network_manager_publish`。
  7. 需要修正的是链路分析：`wifi_link_internal.c:39-45` 实际调用的是 `esp_mqtt_client_publish` 直接发布接口；因此 Wi-Fi 实现也会在该 handler 中直接 publish，只是 LTE 链路通常更可能阻塞更久，风险更突出。

### M5. `GET_POWER_LIMIT` 查询类 RPC 在失败时可能无 response，但问题范围需要收敛
- **原报告条目**: M5
- **验证结论**: 重新阅读 `app_controller.c:567-578`，确认 `TB_COMMAND_GET_POWER_LIMIT` 路径中，只要 `safety_guard_get_thresholds`、`app_controller_internal_format_power_limit_response` 或 `thingsboard_client_send_rpc_response` 任一步失败，函数最终只会记录 `ESP_LOGW`，调用方拿不到 RPC response。对 `GET_POWER_LIMIT` 这类查询命令，这确实是 response 契约缺口。需要收窄的是问题范围：`SET_RELAY`（`app_controller.c:560-565`）和 `SET_POWER_LIMIT`（`app_controller.c:580-589`）当前走的是“执行动作/上报属性”模式，是否还需要单独错误 response 取决于 ThingsBoard 侧命令契约，不能与 `GET_POWER_LIMIT` 一概并为同类缺陷。

### M8. classes.md §15.5 的 `SET_POWER_LIMIT` 示例错误，照抄会触发 `ESP_ERR_INVALID_ARG`
- **原报告条目**: M8
- **验证结论**: 对比 `classes.md:2144-2146`、`app_controller.c:580-585` 与 `safety_guard.c:318-339`，确认示例中的 `safety_guard_set_thresholds(app->cfg.safety, 0, cmd->power_limit_w);` 不会把过流阈值设为 0 生效；该 API 明确要求 `overcurrent_a > 0.0f && overpower_w > 0.0f`，传 0 会直接返回 `ESP_ERR_INVALID_ARG`。因此这里的问题应收敛为“文档示例错误且与真实实现不一致”，而不是“0 阈值会被应用”。

---

## 修复记录

N/A（review-only，无代码改动）

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）

### Resource budget

**app_controller 自身分配**:
- `calloc(1, sizeof(struct app_controller))` ≈ 80 字节（20 个字段：1 config struct + 1 mutex + 3 handler instances + 15 bool flags）
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
| `app_controller_destroy` stop 失败 | **返回错误，不释放 me 和 mutex** | 泄漏路径成立；但修复策略需要先界定 `destroy` 的失败语义 |
| `app_controller_stop_impl` 末尾 mutex take 失败 | **返回错误，不清除 stopping 标志** | 存在永久锁定风险 |
| `thingsboard_client_publish_telemetry` calloc 失败 | 释放 mutex 返回 `ESP_ERR_NO_MEM` | ✅ TB 模块正确处理 |
| `publish_telemetry` 失败 | discard_energy_delta 释放 pending 增量 | ✅ 正确，不丢失电能数据 |
| `on_tb_command` `GET_POWER_LIMIT` 查询失败 | 只记日志，不回 RPC response | 查询类 RPC 存在 response 契约缺口 |
| main.c 对象创建失败 | idle_forever，不 destroy 已创建对象 | 功能无影响，但清理不完整 |

### Cross-module contract review

| 契约 | 遵守情况 |
|------|----------|
| app_controller 不创建底层模块（classes.md §15.7） | ✅ main.c 创建所有对象，app_controller 只接收句柄 |
| 四条数据流经过 app_controller（architecture.md §6） | ✅ 上行遥测/下行RPC/本地按键/安全保护均在 app_controller.c 中以回调实现 |
| 安全保护本地闭环（architecture.md §6.4） | ✅ on_safety_snapshot → relay_set 是本地 GPIO 调用，不依赖网络 |
| app_controller 不直接解析 BL0942 / 拼接 TB topic / 操作 LVGL widget / 判定安全规则（architecture.md §3.1） | ✅ |
| app_controller 是唯一知道所有模块的模块（architecture.md §8.1） | ✅ |
| 不直接依赖 wifi_link / lte_link | ✅ 通过 network_manager 间接使用 |
| **不依赖其他模块内部头文件** | app_controller 目前仍 include `thingsboard_client_internal.h` |
| TB RPC SET_POWER_LIMIT 保留 overcurrent 阈值 | 实际代码正确（先读后写）；但 classes.md §15.5 示例若照抄会返回 `ESP_ERR_INVALID_ARG` |
| 遥测 handler 不阻塞安全判定路径 | handler 注册顺序导致 telemetry publish 先于 safety_guard 判定；Wi-Fi/LTE 都是直接 publish，LTE 更可能阻塞更久 |

### Residual risks

1. **安全路径延迟**：当前 handler 注册顺序会让 telemetry publish 先于 `safety_guard` 判定。Wi-Fi 与 LTE 实现都会在 handler 中直接 publish，只是 LTE 链路更可能出现更长阻塞。当前 `persistence_samples=3`（3s 窗口）降低了影响，但极端过流场景下仍值得关注，最好以上机实测量化各链路的 publish 阻塞时间。

2. **M2 低概率永久锁定风险**：`portMAX_DELAY` 使 `app_controller_stop_impl` 末尾 mutex take 失败概率极低，但一旦发生，`stopping` 标志不会被清除，后续 start/stop 会被永久锁定。

3. **M6 跨模块内部头耦合**：`app_controller_internal.c` 目前直接 include `thingsboard_client_internal.h` 并调用内部格式化函数。这个问题不是“低概率事件”，而是已存在的架构耦合，会增加模块边界脆弱性和后续重构成本。

4. **查询类 RPC response 契约缺口**：`GET_POWER_LIMIT` 查询失败时当前只记日志、不回 response，ThingsBoard 客户端侧可能表现为请求超时。

5. **L2 资源泄漏**：main.c 创建失败时未 destroy 已创建对象。系统进入 idle_forever，功能无影响，但 relay 等执行器可能停留在创建时的默认状态（relay_create 初始化时 GPIO 设为关闭电平）。

6. **并发 start/stop**：当前设计支持单任务调用 start/stop（main task），多任务并发场景下的 flag 竞态经分析基本安全（poll loop + flag 门控），但未经过压力测试验证。

7. **event_loop 配置**：`app_controller_config_t.event_loop` 支持自定义事件循环，但其他模块（metering_service/safety_guard/relay）使用默认循环。若配置非 NULL event_loop，handler 不会收到事件。main.c 使用 NULL（默认循环），当前无问题。
