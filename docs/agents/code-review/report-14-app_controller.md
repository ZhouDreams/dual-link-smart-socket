# Code Review: app_controller + main.c

**日期**: 2026-07-07
**文件**:
- `main/app/app_controller.c`
- `main/app/app_controller.h`
- `main/app/app_controller_internal.c`
- `main/app/app_controller_internal.h`
- `main/main.c`

**期望 API**: classes.md §15 (App Controller)
**架构角色**: architecture.md §3.1 (应用编排层), §6 (核心数据流), §7 (事件与回调原则)

---

## 🔴 高严重度

（无）

---

## 🟡 中严重度

### M1. `app_controller_destroy` 在 `stop` 失败时泄漏对象和 mutex

- **文件:行号**: `main/app/app_controller.c:255-273`
- **问题描述**: `app_controller_destroy` 调用 `app_controller_stop(me)`，若返回非 `ESP_OK`，直接 `return ret` 而不释放 `me` 和 `me->mutex`。调用方无法恢复——再次调用 `destroy` 会再次调用 `stop`，若 `stop` 持续失败则永久泄漏。`err.md` §3.2 要求 `destroy` 对失败场景也安全回收资源。
- **建议修复**: 即使 `stop` 失败，`destroy` 仍应删除 mutex 并释放 `me`；将 stop 的错误码作为返回值但不影响资源释放：
  ```c
  esp_err_t stop_ret = app_controller_stop(me);
  if (me->mutex != NULL) {
      vSemaphoreDelete(me->mutex);
  }
  free(me);
  return stop_ret;
  ```

### M2. `app_controller_stop_impl` 末尾 mutex take 失败时 `stopping` 标志不清除

- **文件:行号**: `main/app/app_controller.c:409-411`
- **问题描述**: `stop_impl` 在清理完成后重新获取 mutex 以清除 `starting`/`stopping` 标志。若 `xSemaphoreTake(portMAX_DELAY)` 失败（极端情况：mutex 损坏），函数提前返回但 `me->stopping` 仍为 `true`。此后任何 `start`/`stop` 调用都会因 `stopping=true` 返回 `ESP_ERR_INVALID_STATE`，控制器永久锁定。
- **建议修复**: mutex take 失败时属于不可恢复的致命状态，应至少记录后继续尝试清理标志，或使用 `xSemaphoreGive` 的忽略返回值模式（当前已用 `(void)`）。若 take 确实不可能失败（`portMAX_DELAY`），可考虑用 assert 替代静默返回，让系统崩溃而非静默锁定。

### M3. 上行遥测 publish 在 esp_event 任务中执行，可能延迟安全保护判定

- **文件:行号**: `main/app/app_controller.c:488-509`（metering handler）, `main/app/app_controller.c:964-1057`（publish_telemetry）
- **问题描述**: `app_controller_register_event_handlers`（start step 2，`main/app/app_controller.c:697-747`）注册 `METERING_EVENT_SNAPSHOT` handler 早于 `safety_guard_start`（start step 6，`main/safety/safety_guard.c:213`）。esp_event 按注册顺序派发 handler，因此 `app_controller_on_metering_snapshot` 先于 `safety_guard_on_metering_snapshot` 执行。前者调用 `thingsboard_client_publish_telemetry`，该函数内部做 JSON 格式化 + `calloc` + `network_manager_publish`（LTE 链路可能阻塞）。若 publish 阻塞，safety_guard 的规则判定和后续 `relay_set(SAFETY, false)` 被延迟。architecture.md §6.4 要求"安全保护必须是本地闭环"。
  - Wi-Fi 链路：`esp_mqtt_client_enqueue` 非阻塞，延迟 <1ms，可忽略。
  - LTE 链路：`lwlte_mqtt_publish` 可能阻塞，延迟可达数十至数百毫秒。
  - safety_guard 默认 `persistence_samples=3`（3s），实际安全动作延迟相对 3s 窗口占比小，但极端过流场景下仍应优先保障安全路径时延。
- **建议修复**: 将遥测 publish 从 metering event handler 中移出，defer 到独立任务或 work queue；或调整 handler 注册顺序使 safety_guard 先于 app_controller 注册。

### M4. 1Hz 遥测 publish 使用 `ESP_LOGI` 违反日志规则

- **文件:行号**: `main/app/app_controller.c:1040-1049`
- **问题描述**: `app_controller_publish_telemetry` 在每次成功发布后调用 `ESP_LOGI` 打印电压/电流/功率等。metering snapshot 频率为 1Hz，此日志每秒输出一次。`err.md` §6 明确规定"周期性遥测、采样循环、频繁事件默认不使用 `ESP_LOGI`，避免刷屏"。
- **建议修复**: 降级为 `ESP_LOGD`，或仅在 `s_telemetry_publish_count` 达到整百/整千时输出一次摘要。

### M5. `on_tb_command` 命令处理失败时不发送 RPC 错误响应

- **文件:行号**: `main/app/app_controller.c:545-600`
- **问题描述**: ThingsBoard RPC 协议中，`GET_POWER_LIMIT` 期望设备返回 RPC response。当前实现中，若 `safety_guard_get_thresholds` 失败、`app_controller_internal_format_power_limit_response` 失败（如 `power_limit_w <= 0`）或 `thingsboard_client_send_rpc_response` 失败，仅记录 `ESP_LOGW` 但不发送错误响应。TB 平台会等待直到 RPC 超时。`SET_RELAY` 和 `SET_POWER_LIMIT` 失败时也无任何错误反馈。
- **建议修复**: 对 `GET_POWER_LIMIT` 失败路径发送一个包含 error 字段的 RPC response JSON；对 `SET_RELAY`/`SET_POWER_LIMIT` 失败考虑发送错误响应或至少确保 TB 端有超时处理。

### M6. `app_controller_internal.c` 引用 `thingsboard_client_internal.h` 跨模块内部头文件

- **文件:行号**: `main/app/app_controller_internal.c:17`
- **问题描述**: `app_controller_internal.c` include 了 `main/thingsboard/thingsboard_client_internal.h`，调用 `tb_internal_format_power_limit_response`（`main/app/app_controller_internal.c:101`）。`thingsboard_client_internal.h` 是 thingsboard 模块的内部头文件（`main/thingsboard/` 目录下），定义了 `tb_internal_*` 系列函数和内部类型。应用编排层不应依赖业务服务层的内部实现。architecture.md §8.1 允许 app_controller 依赖"所有公共模块 API"，但不包括内部头文件。
- **建议修复**: 将 power limit response 格式化能力提升为 thingsboard_client 的公共 API（如 `thingsboard_client_format_power_limit_response`），或在 `thingsboard_client_send_rpc_response` 中接受结构化输入由 TB 内部格式化。

### M7. classes.md §15.3 内部结构体文档严重过时

- **文件:行号**: `docs/agents/classes.md:2064-2068`
- **问题描述**: classes.md §15.3 记录 `struct app_controller` 仅有 `app_controller_config_t cfg` 和 `bool started` 两个字段。实际实现（`main/app/app_controller.c:37-58`）有 15 个字段：`mutex`、3 个 `esp_event_handler_instance_t`、`relay_on`/`relay_known`/`screen_enabled`、`started`/`starting`/`stopping`、6 个回调注册标志、6 个模块启动标志。文档与实现严重不符，reviewer 据文档可能误判代码的并发保护能力。
- **建议修复**: 更新 classes.md §15.3 的内部结构体定义，补充 mutex、handler 实例、生命周期标志和模块启动标志字段。

### M8. classes.md §15.5 `SET_POWER_LIMIT` 示例传 0 作 overcurrent 阈值——危险且与实现不符

- **文件:行号**: `docs/agents/classes.md:2145`
- **问题描述**: classes.md §15.5 的 `on_tb_command` 示例中 `SET_POWER_LIMIT` 分支写为 `safety_guard_set_thresholds(app->cfg.safety, 0, cmd->power_limit_w);`，将 overcurrent 阈值设为 0。这会导致 safety_guard 判定任何电流都超标（0A 阈值），或禁用过流保护（取决于 safety_guard 对 0 阈值的处理）。实际实现（`main/app/app_controller.c:580-585`）正确地先通过 `safety_guard_get_thresholds` 读取当前 overcurrent 阈值，再保留原值只更新 overpower 阈值。文档示例是危险的错误模式。
- **建议修复**: 更新 classes.md §15.5 示例，匹配实际实现（先读后写，保留 overcurrent 阈值）。

---

## 🟢 低严重度

### L1. `app_controller_stop_impl` 等待 `starting` 标志无超时

- **文件:行号**: `main/app/app_controller.c:368-373`
- **问题描述**: `stop_impl(false)` 中 `while (!from_start_rollback && me->starting)` 循环以 10ms 间隔轮询 `starting` 标志，无最大重试次数或超时。若 `app_controller_start` 内某模块的 start 阻塞（如 LTE 初始化），`stop` 将永久挂起。
- **建议修复**: 添加最大等待时长或重试次数上限，超时后返回 `ESP_ERR_TIMEOUT`。

### L2. main.c 对象创建失败时未销毁已创建对象

- **文件:行号**: `main/main.c:84-88`, `main/main.c:127-131`, `main/main.c:191-198`, `main/main.c:221-224`
- **问题描述**: 当 `board_pinmap_get()` 返回 NULL、基础模块创建失败、网络/云模块创建失败或 app/display 模块创建失败时，`app_main` 调用 `smart_socket_idle_forever()` 但不销毁已创建的对象。例如 `tft` 创建失败时，`relay`/`button`/`bl0942`/`metering`/`safety` 已创建但未 destroy。系统进入永久空闲，资源泄漏不影响功能，但 destroy 中的清理逻辑（如 relay 关断、状态保存）不会执行。
- **建议修复**: 在 idle 前按反序 destroy 已创建的对象，或接受当前行为但在注释中说明设计决策。

### L3. main.c TAG 定义方式与项目其他文件不一致

- **文件:行号**: `main/main.c:58`
- **问题描述**: main.c 使用 `static const char *TAG = "main";`（指针变量，占 RAM），而 app_controller.c 等其他文件使用 `#define TAG "app_controller"`（编译期字符串字面量）。ESP-IDF 惯例是 `#define` 方式。
- **建议修复**: 改为 `#define TAG "main"` 以保持一致。

### L4. `s_telemetry_publish_count` 为文件级 static 变量，非实例封装

- **文件:行号**: `main/app/app_controller.c:212`
- **问题描述**: `s_telemetry_publish_count` 是文件级 static 变量，所有 app_controller 实例共享。当前项目单实例运行，功能无影响，但若未来多实例化（如测试场景），计数器会串扰。
- **建议修复**: 移入 `struct app_controller` 作为实例字段，或接受当前设计并在注释说明单实例约束。

### L5. `app_controller_publish_telemetry` mutex take 失败时静默使用默认值

- **文件:行号**: `main/app/app_controller.c:988-992`
- **问题描述**: `publish_telemetry` 中 `if (xSemaphoreTake(me->mutex, portMAX_DELAY) == pdTRUE)` 读取 `relay_on`/`relay_known`。若 take 失败（`portMAX_DELAY` 下极不可能），静默使用 `false` 作为 relay 状态上报，数据不准确但无告警。
- **建议修复**: take 失败时记录 `ESP_LOGW` 或标记数据为 `valid=false`。

### L6. `set_flag` 在动作完成后调用，失败时动作不可回滚

- **文件:行号**: `main/app/app_controller.c:319-323`（tb_command_registered）, `main/app/app_controller.c:613-616`（button_single_registered）, `main/app/app_controller.c:824-827`（bl0942_started）等
- **问题描述**: 模式为"执行动作 → set_flag 标记成功"。若动作成功但 `set_flag` 失败（mutex take 超时，`portMAX_DELAY` 下极不可能），cleanup 路径因 flag=false 而跳过该模块的 stop/clear，导致已启动模块或已注册回调不被清理。`portMAX_DELAY` 使此场景概率极低。
- **建议修复**: 接受当前设计（概率极低），或将 flag 设置移入动作函数内部使其原子化。

---

## 无问题维度

- **A. 资源账本与乘法型分配**: app_controller 无大块分配，`calloc(1, sizeof(*me))` 约 80 字节。`main.c` 的对象创建参数（`rx_buf_size=256`, `max_subscriptions` 等）在各模块自身 review 中审查。app_controller 的 `response[96]` 栈分配在合理范围。
- **B. 内存安全与生命周期（部分）**: 指针偏移无下溢风险；无 VLA；事件 handler 中 event_data 作为 borrowed 指针使用，未跨回调持有。半初始化失败的反序清理通过 `*_started` flag 门控实现，逻辑正确。
- **D. 失败路径完整性（部分）**: `app_controller_start` 的 `goto err` → `stop_impl(true)` 反序清理路径完整；`stop_modules` 按 flag 门控只停止已启动模块；`capture_first_error` 保留首个错误码。`ESP_ERROR_CHECK` 在 main.c 中仅用于 `nvs_flash_init`/`esp_netif_init`/`esp_event_loop_create_default`，符合 err.md §2.2。`app_controller_start` 失败时 main.c 使用 `if (ret != ESP_OK) { idle_forever(); }` 而非 abort，正确。
- **E. 跨模块契约（部分）**: 四条数据流均经过 app_controller：上行遥测（metering handler → TB publish）、下行 RPC（TB command cb → relay/safety）、本地按键（button cb → relay toggle）、安全保护（safety handler → relay_set）。lvgl_dashboard 直接订阅业务事件属设计决策（classes.md §12.6）。app_controller 不直接解析 BL0942 帧、不拼接 TB topic、不操作 LVGL widget、不判定安全规则，符合 architecture.md §3.1。
- **F. 类型与边界**: `response[APP_CONTROLLER_RPC_BUF_SIZE]` 传入 `sizeof(response)` 一致；`tb_telemetry_input_t` 字段类型与 `app_controller_telemetry_output_t` 对应一致；无整数溢出风险。
- **G. 代码质量与一致性（部分）**: 圈复杂度合理（`on_tb_command` switch 约 8，`stop_impl` 约 10）；section 组织符合 coding-style.md 模板；static 函数注释在 STATIC PROTOTYPES 区域；双语注释格式正确。
