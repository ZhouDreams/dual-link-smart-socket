# Verification: thingsboard_client

对应报告：`report-11-thingsboard_client.md`

每条发现均重新阅读源码（前后 ≥30 行上下文）并用 `rg` 交叉验证。

---

## ✅ 确认的问题

- **原报告条目**: 🟡 `device_token` 字段被存储但从不使用（`thingsboard_client.h:46` + `thingsboard_client.c:148`）
  - 验证结论: 确认。
  - `rg -n "device_token|->config\." main/thingsboard/` 结果显示：`device_token` 仅出现在 `thingsboard_client.h:46`（声明）一处；`me->config.*` 的全部访问点为 `net_mgr`（.c:214/225/229/238/242/246/254/256/260/298/376/417/460/491）、`enable_rpc`（.c:213/299）、`enable_attributes`（.c:224/300），无任何 `me->config.device_token` 读取。`main.c:186` 传入 `CONFIG_SMART_SOCKET_TB_TOKEN`，classes.md §13.2 将其列为配置字段。字段以 borrowed 指针形式随 `me->config = *config`（.c:148）浅拷贝常驻句柄，属真实死字段 + borrowed 悬空隐患。

- **原报告条目**: 🟢 `thingsboard_client_start` 末尾 `destroying || stopping` 二次检查为不可达分支（`thingsboard_client.c:253-265`）
  - 验证结论: 确认。
  - 上下文：line 203 `xSemaphoreTake(me->mutex, portMAX_DELAY)` 取互斥量，line 205-212 检查 `started`/`destroying`/`stopping`，line 213-251 调用 network_manager（均持锁），line 253 再次检查 `destroying || stopping`，line 267 才 `xSemaphoreGive`。`destroy`（.c:175-178）与 `stop`（.c:284-296）置位 `destroying`/`stopping` 均需先取同一互斥量，故 line 253 条件不可能在持锁期间变为真。属维度 G 不可达分支。

- **原报告条目**: 🟢 `thingsboard_client_copy_command` default 分支静默映射为 `TB_COMMAND_GET_POWER_LIMIT`（`thingsboard_client.c:631-633`）
  - 验证结论: 确认。
  - `tb_internal_parse_rpc`（_internal.c:225-244）对 method 仅匹配 `setRelay`/`getPowerLimit`/`setPowerLimit`，其余返回 `ESP_ERR_INVALID_RESPONSE`；`thingsboard_client_on_rx`（.c:572-575）对该错误 `return` 不调用 cb。故 copy_command 的 default 不可达，但映射语义（未知→读功率限制）确具误导性。

---

## ❌ 误报

（本报告无被验证为误报的条目。）

---

## ⚠️ 部分正确（需调整修复方案）

- **原报告条目**: 🟢 `thingsboard_client_stop` 轮询循环无超时（`thingsboard_client.c:286-291`）
  - 调整说明: 问题真实存在，但严重度与修复需精修。
  - 验证：line 286 `while (!me->started && me->stopping)` + line 288 `vTaskDelay(pdMS_TO_TICKS(TB_STOP_POLL_MS))` 确为无界轮询。根因链：stop 在 line 301 释放互斥量后于 line 303 调用 `network_manager_register_rx_cb(NULL)`，其 drain（network_manager.c:738-744）轮询 `active_rx_callbacks==0`，而该计数在 `network_manager_on_link_rx`（network_manager.c:1305-1308）于 `rx_cb` 返回后才递减——即等价于等待 in-flight `cmd_cb` 返回。若 `cmd_cb`（app_controller.c:545 `app_controller_on_tb_command`）阻塞，则 drain 与本轮询均挂死。
  - 但 `cmd_cb` 契约要求短小非阻塞（architecture.md §6.2：命令交 app_controller 协调），且当前 cb 内的 `relay_set` / `network_manager_publish` / `safety_guard_*` 均为同步快速调用。故"挂死"需 cb 实现违约才触发，属防御性缺失而非现实高频 bug。
  - 修复方案调整：维持 🟢 低严重度；修复改为给 drain 轮询加上限（如 `TB_STOP_MAX_WAIT_MS`），超时 `ESP_LOGE` + 返回 `ESP_ERR_TIMEOUT`，避免一次 cb 阻塞级联。不必移除串行化逻辑本身（并发 stop 串行化是合理的）。

- **原报告条目**: 🟢 `thingsboard_client_start` 持互斥量期间调用 network_manager（`thingsboard_client.c:214-251`）
  - 调整说明: 现象属实，但经追踪不构成死锁，且为启动期路径。
  - 验证：`network_manager_subscribe`（network_manager.c:656-）取 NM 互斥量记录订阅后调用 `network_link_subscribe`（同步 esp-mqtt SUBSCRIBE，快速返回）；`network_manager_register_rx_cb` 非 NULL 分支（network_manager.c:745-748）仅赋值不 drain。锁序：start 持 TB 互斥量→取 NM 互斥量；on_link_rx 取 NM 互斥量→释放→调 rx_cb 取 TB 互斥量。无环。on_rx 若在 start 期间触发会阻塞在 .c:577 取 TB 互斥量，但 start 很快释放。
  - 修复方案调整：可作为优化（参照 stop 用本地快照），但非必须；建议保留并加注释说明持锁范围是有意为之，或降级为 residual risk。

---

## 修复记录

N/A — 本次为 review-only，未修改任何源代码（遵循 `_agent_instructions.md` 关键规则 #2）。

---

## 模块交付清单

- **Change summary**: N/A（review-only，无代码改动）。

- **Resource budget**:
  - 启动 heap：`sizeof(struct thingsboard_client)`（约 80-96B，含 config 结构体 + 指针 + int + bool×3 + uint32）+ `json_buf`（默认 512B，可配 `json_buf_size`）+ FreeRTOS mutex（约 80B）。合计约 0.7KB（默认配置）。
  - 运行 heap：`json_buf` 常驻；每次 publish 临时 `calloc(json_len+1, 1)`（telemetry JSON 约 180-220B，relay/power_limit 属性约 15-25B），publish 后立即 free。
  - 峰值 heap：默认配置下约 512 + 256（json_copy）≈ 0.75KB 常驻 + 临时。
  - task stack：本模块不创建任务；`on_rx` / `cmd_cb` 在 link 任务栈执行（wifi/lte 任务），publish 在调用方任务栈执行。`on_rx` 栈占用：`tb_internal_command_t`（~16B）+ `tb_command_t`（~12B）+ 解析临时（`quoted_key[32]`/`temp[32]` 在 _internal.c 栈上，但 _internal 函数已返回）。无 VLA。
  - queue size：本模块无队列。
  - buffer size：`json_buf` = `json_buf_size>0 ? json_buf_size : 512`（单次 calloc，非乘法型）；`topic[96]`（send_rpc_response 栈缓冲，prefix 29 + 最多 10 位数字 + NUL ≪ 96）；`quoted_key[32]`/`temp[32]`（_internal.c 解析栈缓冲，均带长度上限校验）。
  - 无 `count*size` 乘法型分配。

- **Lifecycle / ownership notes**:
  - `tb_client_config_t::net_mgr` — borrowed（调用方拥有 network_manager 句柄），thingsboard_client 仅保存指针，不 destroy。
  - `tb_client_config_t::device_token` — borrowed，存入 `me->config` 但从不解引用（见 🟡 发现）。
  - `me->json_buf` — owned（create 时 calloc，destroy 时 free），publish 路径持互斥量写入后 memcpy 到 `json_copy` 再释放互斥量，`json_buf` 不跨调用持有。
  - `json_copy`（publish_telemetry / report_relay_state / report_power_limit）— owned，calloc 后 memcpy，publish 后无论成败 free（.c:381/422/465）。
  - `send_rpc_response` 的 `json` 参数 — borrowed（调用方栈缓冲），同步 publish 期间有效，不拷贝。
  - `tb_telemetry_input_t::active_link` — borrowed，`safe_active_link_name` 同步 strcmp，不持有。
  - `cmd_cb` / `cmd_ctx` — borrowed（app_controller 注册），on_rx 在互斥量内快照到本地后释放互斥量再调用，cb 执行期间不持锁。

- **Failure-path review**:
  - malloc 失败：`create` 中 `calloc(me)` / `calloc(json_buf)` / `xSemaphoreCreateMutex` 失败按反序释放并返回 NULL（.c:143-163）；publish 中 `calloc(json_copy)` 失败返回 `ESP_ERR_NO_MEM` 并释放互斥量（.c:371-374/411-414/454-457）。
  - `network_manager_publish` 失败：`json_copy` 仍 free（.c:381/422/465），无泄漏；`send_rpc_response` 不持有 json_copy，由调用方管理。
  - `network_manager_subscribe` / `register_rx_cb` 失败：start 中按已订阅标志反序 `unsubscribe` + 清 rx_cb（.c:228-263）。
  - `network_manager_unsubscribe` / `register_rx_cb(NULL)` 失败：stop 用 `first_error` 汇总，对 `ESP_ERR_NOT_FOUND` 容忍（.c:304-322）；rx_cb 在 `register_rx_cb(NULL)` 内先置 NULL 再 drain（network_manager.c:735-744），即便 drain 失败也不会有新回调进入。
  - `finish_format` 截断：返回 `ESP_ERR_INVALID_SIZE`，publish 路径直接返回不发布半截 JSON（.c:366-369/407-410/450-453）。
  - 无 `abort()` 类宏用于可恢复路径。

- **Cross-module contract review**:
  - 分层：thingsboard_client 属业务服务层，仅依赖 network_manager（网络抽象层）+ 业务输入类型（`safety_guard_level_t`），符合 architecture.md §8.1。
  - 禁止依赖：`rg "wifi_link|lte_link|wifi/|lte/" main/thingsboard/` 无匹配——不直接依赖 wifi_link/lte_link，符合 §8.2 #1。
  - 禁止操作继电器：不调用 `relay_*`，仅经 `cmd_cb` 把语义命令交 app_controller，符合 §8.2（隐含）与 §6.2。
  - 数据流：上行遥测经 `publish_telemetry`（app_controller 调用）→ network_manager；下行 RPC 经 `on_rx` 解析 → `cmd_cb` → app_controller，不绕过 app_controller，符合 §6.1/§6.2。
  - 未破坏任何分层契约。

- **Residual risks**:
  - `device_token` 死字段未修（见 🟡 发现）。
  - `cmd_cb` 在 link 任务上下文同步执行且内部会同步 publish（app_controller.c:545-594）；当前快速可接受，若未来 RPC 量增大或 publish 阻塞延长，会拖慢该链路 MQTT 下行处理。属 app_controller 设计选择。
  - `thingsboard_client_destroy` 假定调用方在 destroy 期间不再并发调用 publish/stop/start；`destroying` 标志为 best-effort 守卫，destroy 完成 `vSemaphoreDelete` 后任何对 `me->mutex` 的访问均为 UB（标准"destroy 后禁用"契约）。
  - 手写 JSON 解析器（`find_in_payload` 做朴素子串匹配）对 `"method"` 出现在字符串值内的 crafted payload 会误解析；威胁模型为可信 ThingsBoard 云端，风险低，属已知设计折中（避免引入 cJSON）。
  - `telemetry_values_are_finite` 仅校验 `isfinite`，不校验数值范围；极端有限值（如 1e30）经 `%.2f` 展开后可能超 `json_buf` → `finish_format` 返回 `ESP_ERR_INVALID_SIZE` → 本次遥测丢弃（优雅失败，无内存破坏，但无模块内诊断日志，错误码上抛 app_controller 处理）。
