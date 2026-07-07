# Code Review: network_link + network_types

**日期**: 2026-07-07
**文件**:
- `main/network/network_link.c`
- `main/network/network_link.h`
- `main/network/network_link_priv.h`
- `main/network/network_types.h`

**模块角色**: 网络抽象层基类接口 — C OOP 继承体系的根（opaque handle + ops 虚函数表 + 包装函数）。
**期望 API**: classes.md §2（Network Types）+ §6（Network Link）。
**架构角色**: architecture.md §3.3（网络抽象层）、§4（网络层 OOP 设计）、§4.1–§4.3。

---

## 🔴 高严重度

无。

---

## 🟡 中严重度

### H-1: 文档漂移 — classes.md §6.2 ops 表与 §6.3 包装 API 清单均未记录 `set_active`

- **文件:行号**: `network_link_priv.h:43`（ops 表定义 `set_active`）、`network_link.h:147`（包装 API 声明）、`network_link.c:123-130`（包装实现）；对比 `docs/agents/classes.md:716-728`（§6.2 ops 表）与 `docs/agents/classes.md:758-772`（§6.3 包装 API 清单）。
- **问题描述**: 实现中 `network_link_ops_t` 含 9 个方法（含 `set_active`），`network_link.h` 公开 `network_link_set_active()` 包装函数。但 classes.md §6.2 的 ops 表只列 8 个方法，§6.3 的包装 API 清单只列 8 个 wrapper + `get_type`，均遗漏 `set_active`。`rg -n "set_active" docs/agents/classes.md` 返回空——classes.md 中无 `set_active` 的任何提及。
- **影响**: 
  1. 该 API 被 `network_manager.c:443/1249/1257` 实际调用，控制 LTE 链路 MQTT 上岗/卸岗（热备语义），是双模联网的核心能力之一。
  2. reviewer 据 classes.md 审查时会完全遗漏此 API，误判 `network_link` 接口范围。
  3. classes.md §6.6 "关键设计决策" 未说明 `set_active` 作为选填方法的设计意图（wifi_link 未实现 NULL → wrapper no-op；lte_link 实现 → 控制 MQTT 启停）。
- **建议修复**: 
  1. 在 classes.md §6.2 ops 表末尾增加 `esp_err_t (*set_active)(network_link_t *me, bool active);` 并标注"选填"。
  2. 在 classes.md §6.3 包装 API 清单增加 `esp_err_t network_link_set_active(network_link_t *me, bool active);`。
  3. 在 classes.md §6.6 关键设计决策增加一条："`set_active` 为选填方法，控制链路上岗/卸岗；子类未实现时 wrapper 返回 `ESP_OK` no-op。lte_link 通过它控制 MQTT 启停实现热备，wifi_link 不需要此语义。"

---

## 🟢 低严重度

### H-2: `network_link_register_rx_cb` Doxygen 未文档化 NULL 清除回调语义

- **文件:行号**: `network_link.h:123-135`（Doxygen 注释）、`network_link.c:113-121`（实现）。
- **问题描述**: classes.md §6.3 明确记载"回调清除语义：`network_link_register_rx_cb(me, NULL, NULL)` 清除当前回调并返回 `ESP_OK`"。`network_manager.c:866` 实际依赖此语义（`network_manager_clear_link_rx_cb` 调用 `network_link_register_rx_cb(link, NULL, NULL)`）。但 `network_link.h:123-135` 的 Doxygen 仅描述"注册下行消息回调"，未提及传 `NULL` cb 清除回调的语义，也未在 `@param cb` 说明 NULL 的含义。
- **影响**: 调用方仅看头文件无法得知清除回调的正确方式，可能误以为需要单独的 unregister API。
- **建议修复**: 在 `network_link.h:123-135` Doxygen 增加一行 `@note 传入 cb=NULL, ctx=NULL 清除当前回调并返回 ESP_OK； Clear current callback by passing cb=NULL, ctx=NULL.`。

### G-1: `network_link_get_type` 直接读 `me->type` 的安全性依赖未文档化的不变式

- **文件:行号**: `network_link.c:132-139`。
- **问题描述**: `network_link_get_type` 直接读 `me->type` 不经 ops 表，也不加锁。classes.md §6.6 说明这是设计意图（"类型查询不需要多态"）。安全性依赖于"`type` 在子类 create 函数中设置一次后永不再修改"这一不变式——但代码中无任何注释说明此假设。`me->ops` 的无锁读取同理（同样在 create 后不变）。
- **影响**: 当前安全（`type`/`ops` 确实只写一次），但未来若有人新增"运行时切换 ops 表"或"动态改 type"的逻辑，会引入无锁读写的数据竞争且无编译期防护。
- **建议修复**: 在 `network_link_get_type` 实现处增加注释：`/* type 在 create 后不可变，可无锁读取； type is immutable after create, lock-free read is safe */`。或在 `network_link_priv.h` 的 `struct network_link` 字段处加注释标注 `/* immutable after create */`。

### F-1: `network_link_get_status` 包装函数 const 与 ops 方法非 const 的契约不一致

- **文件:行号**: `network_link.c:70-79`（包装函数签名 `const network_link_t *me`，行 78 强转 `(network_link_t *)me` 调用 ops）、`network_link_priv.h:36`（ops 方法签名 `network_link_t *me` 非 const）。
- **问题描述**: 包装函数向调用方承诺 `me` 只读（`const`），但 ops 方法签名为非 const，行 78 通过 `(network_link_t *)me` 强转消除 const。子类实现（如 `wifi_link_get_status_impl` / `lte_link_get_status_impl`）可能需要取 mutex（修改 mutex 内部状态），因此 ops 方法确实需要非 const 访问。classes.md §6.2（ops 非 const）与 §6.3（wrapper const）本身也体现了这一不一致。
- **影响**: 这是 C OOP 的已知限制（无 `mutable` 关键字），强转在实践上可工作（mutex 取放是"逻辑 const"操作），但包装函数的 const 承诺对调用方有误导性——调用方可能认为 `get_status` 绝不修改 `me` 的任何字段。
- **建议修复**: 二选一：(a) 将包装函数改为非 const `network_link_t *me`（诚实但降低 const-correctness）；(b) 保留现状但在 `network_link.h:71-82` Doxygen 增加说明："`get_status` 实现可能取内部互斥锁，逻辑上不修改链路状态； implementation may take internal mutex but does not logically mutate link state."。推荐 (b)，因为 classes.md 已文档化 const 签名。

---

## 无问题维度

- **A（资源账本与乘法型分配）**: `network_link.c` 是基类包装层，无 `malloc`/`calloc`/`xQueueCreate`/buffer 分配。`network_types.h` 是纯类型定义。无乘法型占用。N/A。
- **B（内存安全与生命周期）**: 无指针偏移、无 VLA、无栈大块分配。包装函数仅做参数校验和委托，无失败路径泄漏风险。`destroy` 正确处理 `NULL`（`network_link.c:45-47` 返回 `ESP_OK`，符合 err.md §3.2）。`get_type` 正确处理 `NULL`（返回 `NETWORK_LINK_TYPE_NONE`）。
- **D（失败路径完整性）**: 所有包装函数使用 `ESP_RETURN_ON_FALSE` 返回 `ESP_ERR_INVALID_ARG` / `ESP_ERR_NOT_SUPPORTED`，符合 err.md §5.1。`set_active` 作为选填方法，ops 或方法为 NULL 时返回 `ESP_OK` no-op（`network_link.c:126-128`），符合 oop-design.md §4.3 选填策略。无 `abort()` 类宏在可恢复路径。
- **C（并发/竞态/死锁）**: 基类无共享状态、无锁、无任务。`me->type` / `me->ops` 无锁读取的安全性见 G-1（依赖未文档化不变式，但当前安全）。包装函数不在 ISR 上下文使用。无死锁风险（无锁获取）。
- **G（代码质量，除 G-1 外）**: 
  - 文件结构严格遵循 `coding-style.md` 模板（INCLUDES / DEFINES / TYPEDEFS / STATIC PROTOTYPES / STATIC VARIABLES / MACROS / GLOBAL FUNCTIONS / STATIC FUNCTIONS）✓
  - 双语 Doxygen 注释格式正确（`@brief` 中文 + `@details` 英文）✓
  - `TAG` 定义为 `"network_link"` ✓
  - 所有包装函数圈复杂度 ≤ 3 ✓
  - 子类 ops 表为 `static const`（`wifi_link.c:400`、`lte_link.c:118`），符合 oop-design.md §3.4 ✓
  - `network_link_priv.h` 只被 `network_link.c` / `wifi_link.c` / `lte_link.c` include，未泄漏给 `network_manager` 或上层 ✓
- **H（文档，除 H-1/H-2 外）**: 
  - `network_types.h` 完全匹配 classes.md §2.2–2.7（枚举值、结构体字段、回调签名一致）✓
  - `network_types.h` 纯头文件，只依赖 `<stdbool.h>` / `<stddef.h>` / `<stdint.h>`，不引入 ESP-IDF 编译依赖 ✓（符合 classes.md §2.7）
  - `network_link.h` opaque handle 前置声明 ✓（符合 oop-design.md §1.2）
  - `struct network_link` 定义在 `network_link_priv.h`，不暴露给 `network_manager` ✓（符合 classes.md §6.3）
  - 包装函数参数校验模式与 classes.md §6.3 示例一致 ✓

---

## 跨模块观察（供维度 E 汇总）

1. **`network_link_destroy` 在整个 `main/` 下从未被调用**（`rg` 确认仅声明 `network_link.h:47` + 定义 `network_link.c:43`，无调用点）。`network_manager_destroy`（`network_manager.c:362-397`）只清理自身资源（sub_table / sema / mutex / me），不调用 `network_link_destroy` 释放 primary/backup 链路。`network_manager_destroy` 本身也从未被调用。这意味着 `wifi_link_destroy_impl` / `lte_link_destroy_impl` 当前是死代码，链路对象及其内部资源（UART、MQTT client、netif、订阅表等）在程序生命周期内不被显式释放。对 ESP32 嵌入式系统而言，这通常是可接受的（设备关机即断电），但 lifecycle API 存在却不接线属于架构层面的不完整——建议在 app_controller 或 network_manager 层补全 destroy 链路。此为跨模块问题，不计入 network_link 自身发现。

2. **`network_link_set_active` 的调用方**（`network_manager.c:443/1249/1257`）已在 network_manager review（report-06）中审查过持锁调用延迟风险，此处不重复。

---

## 验证方法

- `rg -n "set_active" docs/agents/classes.md` → 空（确认文档遗漏）
- `rg -n "set_active" main/` → 确认 ops 定义、wrapper 实现、lte_link 实现、network_manager 调用点
- `rg -n "network_link_priv\.h" main/` → 确认只被 network_link.c / wifi_link.c / lte_link.c include
- `rg -n "network_link_destroy\(" main/` → 确认从未被调用
- `rg -n "network_link_register_rx_cb" main/` → 确认 network_manager.c:866 依赖 NULL 清除语义
- 对比 classes.md §6.2/§6.3 与 `network_link_priv.h`/`network_link.h` 逐字段核对
