# Verification: network_link + network_types

## ✅ 确认的问题

### H-1: 文档漂移 — classes.md §6.2 ops 表与 §6.3 包装 API 清单均未记录 `set_active`

- **验证结论**: 确认。
  1. `rg -n "set_active" docs/agents/classes.md` 返回空——classes.md 全文无 `set_active` 任何提及。
  2. `network_link_priv.h:43`: `esp_err_t (*set_active)(network_link_t *me, bool active);` — ops 表第 9 个方法。
  3. `network_link.h:147`: `esp_err_t network_link_set_active(network_link_t *me, bool active);` — 公开包装 API，含完整 Doxygen（行 137-146，标注"选填方法"）。
  4. `network_link.c:123-130`: 包装实现，`me->ops->set_active == NULL` 时返回 `ESP_OK` no-op。
  5. `lte_link.c:127`: `.set_active = lte_link_set_active_impl` — LTE 子类实现了此方法。
  6. `wifi_link.c:400-409`: `wifi_link_ops` 未设置 `.set_active`（NULL）— Wi-Fi 子类按设计不实现。
  7. `network_manager.c:443`: `network_link_set_active(selected, true)` — start 时上岗。
  8. `network_manager.c:1249`: `network_link_set_active(link, true)` — switch_active 时新链路上岗。
  9. `network_manager.c:1257`: `network_link_set_active(old, false)` — switch_active 时旧链路卸岗。
  10. classes.md §6.2（行 716-728）ops 表只列 8 个方法；§6.3（行 758-772）包装 API 清单只列 8 wrapper + get_type。
  11. 其他 review 报告已独立确认此漂移：`report-07-wifi_link.md` G-4、`report-08-lte_link.md` H-5。
  - **结论**: `set_active` 是实现中存在、被子类实现、被 network_manager 实际调用的 ops 方法，但 classes.md §6 完全未记录。文档漂移确认。

### H-2: `network_link_register_rx_cb` Doxygen 未文档化 NULL 清除回调语义

- **验证结论**: 确认。
  1. 读取 `network_link.h:123-135` Doxygen：`@brief 注册下行消息回调`，`@param[in] cb 下行消息回调`，无任何关于 `cb=NULL` 清除语义的说明。
  2. `network_link.c:113-121` 实现：仅检查 `me != NULL` 和 `me->ops != NULL && me->ops->register_rx_cb != NULL`，**不**检查 `cb != NULL`，直接透传给 ops。即 wrapper 允许 `cb=NULL` 透传。
  3. `network_manager.c:860-872`（`network_manager_clear_link_rx_cb`）：`network_link_register_rx_cb(link, NULL, NULL)` — 实际依赖此语义清除回调。
  4. `wifi_link.c:911-932`（`wifi_link_register_rx_cb_impl`）：`me->rx_cb = cb; me->rx_ctx = (cb != NULL) ? ctx : NULL;` — cb=NULL 时清除 rx_cb，并调用 `wifi_link_wait_runtime_actions_drained` 等待在途回调完成。
  5. `lte_link.c:424-440`（`lte_link_register_rx_cb_impl`）：`me->rx_cb = cb; me->rx_ctx = cb ? ctx : NULL;` — cb=NULL 时清除，并调用 `lte_link_wait_rx_callbacks_drained` 等待在途回调。
  6. classes.md §6.3（行 774）："回调清除语义：`network_link_register_rx_cb(me, NULL, NULL)` 清除当前回调并返回 `ESP_OK`。" — 文档有，头文件无。
  - **结论**: 语义在实现层正确（两个子类都正确处理 NULL + drain），wrapper 正确透传。但头文件 Doxygen 缺失此语义说明，调用方仅看头文件无法得知。确认。

### G-1: `network_link_get_type` 直接读 `me->type` 的安全性依赖未文档化的不变式

- **验证结论**: 确认（低严重度）。
  1. `network_link.c:132-139`：`if (me == NULL) return NETWORK_LINK_TYPE_NONE; return me->type;` — 无锁直接读。
  2. `network_link_priv.h:50-53`：`struct network_link { const network_link_ops_t *ops; network_link_type_t type; };` — `type` 非 `const`，但从无写入函数。
  3. `rg -n "->type\s*=" main/network/` 搜索写入点：仅在子类 create 函数中设置（`wifi_link.c` 和 `lte_link.c` 的 create 函数设 `self->base.type = NETWORK_LINK_TYPE_*`），create 后永不再修改。
  4. classes.md §6.6（行 859）："`network_link_get_type()` 直接读 `me->type`，不经过 ops 表（类型查询不需要多态）" — 设计意图有文档化，但"为何无锁安全"（即 type 不可变不变式）未在代码注释中说明。
  5. `me->ops` 同理：`const network_link_ops_t *ops` 是 `const` 指针指向 `const` 表，指针本身在 create 后不变，但代码未注释此假设。
  - **结论**: 当前代码安全（不变式成立），但安全性依赖未在代码中文档化的假设。低严重度确认。

### F-1: `network_link_get_status` 包装函数 const 与 ops 方法非 const 的契约不一致

- **验证结论**: 确认（低严重度，C OOP 已知限制）。
  1. `network_link.c:70`：`esp_err_t network_link_get_status(const network_link_t *me, ...)` — wrapper 签名 const。
  2. `network_link.c:78`：`return me->ops->get_status((network_link_t *)me, out);` — 强转消除 const。
  3. `network_link_priv.h:36`：`esp_err_t (*get_status)(network_link_t *me, network_link_status_t *out);` — ops 方法非 const。
  4. classes.md §6.2（行 721）：ops 方法非 const；§6.3（行 762-763）：wrapper const — 文档本身也体现了此不一致。
  5. 子类实现 `wifi_link_get_status_impl` / `lte_link_get_status_impl` 需要取 mutex 读取状态（mutex 取放修改 mutex 对象），因此 ops 方法确实需要非 const 访问。
  - **结论**: 这是 C 语言无 `mutable` 关键字的已知限制。强转在实践上安全（mutex 取放是逻辑 const 操作），但 wrapper 的 const 承诺对调用方有轻微误导。低严重度确认，建议用 Doxygen 说明而非改签名。

---

## ❌ 误报

无。

---

## ⚠️ 部分正确（需调整修复方案）

无。

---

## 修复记录

N/A（review-only，无代码改动）。

---

## 模块交付清单

### Change summary
N/A（review-only，无代码改动）。本次仅审查 `network_link.c` / `network_link.h` / `network_link_priv.h` / `network_types.h` 四个文件，产出报告与验证，未修改任何源代码或现有文档。

### Resource budget
- **启动 heap**: `network_link.c` 无 `malloc`/`calloc` — 基类包装层零堆分配。`network_types.h` 纯类型定义。N/A。
- **运行 heap**: 基类无运行时分配。子类（wifi_link/lte_link）的资源账本见各自 report-07/report-08。
- **峰值 heap**: N/A（本模块不分配）。
- **task stack**: 基类无内部任务。包装函数在调用方任务上下文执行（如 network_manager monitor 任务、esp_event 任务）。无栈大块分配。
- **queue size**: N/A（基类无队列）。
- **buffer size**: N/A（基类无 buffer）。
- **乘法型占用**: 无。`network_types.h` 中的 `network_publish_request_t` / `network_rx_data_t` 是值对象定义，不预分配。`network_link_ops_t` 是函数指针表（`sizeof` = 9 × 指针宽度 = 36 字节 on Xtensa，由子类 `static const` 实例化放 `.rodata`）。

### Lifecycle / ownership notes
- **`network_link_t *me`**: 基类句柄。子类 create 函数 `calloc` 子类结构体后返回 `&self->base`（向上转型，符合 oop-design.md §2.3 `&obj.base` 禁止强转）。ownership 归调用方（app_controller 装配后注入 network_manager）。
- **`me->ops`**: 指向子类 `static const network_link_ops_t`（`.rodata`），borrowed，生命周期与程序一致。create 后不可变。
- **`me->type`**: 值类型 `network_link_type_t`，create 后不可变。
- **`network_publish_request_t *req`**: wrapper 透传给 ops，borrowed（调用方栈上构造，如 thingsboard_client）。wrapper 不拷贝不持有。
- **`network_rx_data_t`**: 在子类回调中构造，传给 `rx_cb`，borrowed（仅回调期间有效）。子类负责释放（wifi_link malloc+free around callback，见 report-07）。
- **`network_link_destroy(me)`**: 声明并实现但**从未被调用**（见跨模块观察）。设计上应释放子类全部资源，但当前 lifecycle 未接线。

### Failure-path review
- **malloc 失败**: N/A（基类不分配）。
- **queue/event send 失败**: N/A（基类无队列/事件）。
- **UART/SPI 传输失败**: N/A（基类不直接操作硬件）。
- **参数校验失败**: 所有包装函数使用 `ESP_RETURN_ON_FALSE` 返回 `ESP_ERR_INVALID_ARG`（NULL me / NULL req / NULL topic / NULL out）或 `ESP_ERR_NOT_SUPPORTED`（ops 或方法为 NULL）。`destroy` 特殊处理 NULL me 返回 `ESP_OK`（符合 err.md §3.2）。`get_type` 特殊处理 NULL me 返回 `NETWORK_LINK_TYPE_NONE`。`set_active` 选填方法 ops 为 NULL 时返回 `ESP_OK` no-op（符合 oop-design.md §4.3）。
- **结论**: 失败路径完备，无遗漏。

### Cross-module contract review
- **分层契约**: `network_link` 是网络抽象层基类，只依赖 `network_types.h` + `esp_err.h`（符合 classes.md §6.7）。`network_manager` 只 include `network_link.h`（公共头），通过包装函数操作，不 include `network_link_priv.h`（`rg` 确认 `network_manager.c` 不 include priv 头）— 分层契约遵守 ✓。
- **`network_link_priv.h` 可见性**: 只被 `network_link.c` / `wifi_link.c` / `lte_link.c` include（`rg` 确认），未泄漏给 network_manager 或上层 ✓。
- **opaque handle**: `network_link.h` 只暴露 `typedef struct network_link network_link_t;` 前置声明，struct 定义在 priv.h ✓（符合 oop-design.md §1.2）。
- **向上转型**: 子类 create 返回 `&self->base`（wifi_link.c / lte_link.c），未使用强转 ✓（符合 oop-design.md §2.3）。
- **ops 表 `static const`**: `wifi_link_ops`（wifi_link.c:400）、`lte_link_ops`（lte_link.c:118）均为 `static const` ✓（符合 oop-design.md §3.4）。
- **未破坏的契约**: network_link 不理解 ThingsBoard topic、不解析业务遥测、不直接操作继电器 — 纯链路抽象 ✓。
- **跨模块观察（非 network_link 自身缺陷）**:
  1. `network_link_destroy` 从未被调用 — lifecycle API 未接线（应由 network_manager 或 app_controller 在销毁路径调用）。当前程序生命周期内链路对象不释放，对嵌入式设备可接受但架构不完整。
  2. `set_active` 被network_manager 持锁调用（report-06 C-1 已审查延迟风险）。

### Residual risks
1. **`network_link_destroy` 死代码**：声明+实现完整但从未被调用，子类 `destroy_impl` 不可达。若未来 OTA/重启场景需要清理链路资源，需先在 network_manager 或 app_controller 补全 destroy 调用链。此为跨模块 lifecycle 缺口，非 network_link 自身 bug。
2. **`me->type` / `me->ops` 无锁读取**：当前安全（create 后不可变），但无编译期或运行期防护防止未来代码违反此不变式（见 G-1）。
3. **`set_active` 文档漂移**（H-1）：classes.md 未记录此 API，reviewer 据文档审查会遗漏。需同步更新 classes.md §6.2/§6.3/§6.6。
4. **const 契约不一致**（F-1）：`get_status` wrapper const 但 ops 非 const，强转消除 const。C OOP 已知限制，实践安全但语义不严谨。
5. **上机验证项**：无（基类无运行时行为需上机验证）。
