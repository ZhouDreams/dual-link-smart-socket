# Code Review 修复状态

**状态日期**：2026-07-14

本文只跟踪 `summary-2026-07-07.md` 中需要实际修复的高价值问题。原始 report/verify 保留为审查证据，其中的 `N/A` 和“未修复”描述代表 2026-07-07 当时状态。

## 已完成

| 范围 | 状态 | 依据 |
|------|------|------|
| MS-01：计量事件 post 失败后回滚 pending | 已修复 | `791a17d` + `test_metering_service_event_flow` |
| MS-07：fault 路径释放 pending | 已修复 | `791a17d` + `test_metering_service_event_flow` |
| relay TOCTOU 事件乱序 | 已修复 | `791a17d` + `test_relay_event_order` |
| app_controller M3：安全 handler 先于遥测 handler | 已修复 | `f510b7f` + `test_app_controller_event_order` |
| 生命周期等待和 callback drain 无超时 | 已修复 | `fc09786` + app/ThingsBoard lifecycle tests |
| ThingsBoard destroy 清理失败后 UAF | 已修复 | `fc09786` + `test_thingsboard_client_lifecycle` |
| safety event post 失败导致本周期关断动作丢失 | 已修复 | app_controller 直接读取 safety latest；relay post 失败保留最新状态并重试 |
| LTE C-1：public ops / event handler 与 destroy 竞态 | 已修复 | `active_ops` drain + `test_lte_link_lifecycle` |
| LTE D-1：吞掉 lwlte_destroy 错误 | 已修复 | 失败保留句柄并允许 destroy 重试 |
| network_manager F-1：preferred_primary 不生效 | 已修复 | create 时规范化主备 + `test_network_manager_preference` |
| TFT #1：MADCTL 初始化丢失 BGR 位 | 已修复 | 使用 `madctl_val` + `test_tft_panel_st7789t_init` |
| architecture 中不存在的 display_service | 已修复 | 显示流改为 lvgl_dashboard 直接订阅业务事件 |
| MS-02 / app M2：stop 收尾 mutex 中断后遗留 `stopping` | 已修复 | 重试取得收尾锁、恢复一致状态后返回超时 + app/metering lifecycle tests |
| network_manager C-1/C-2：持状态锁调用链路 API、RX 事件任务无限等待 | 已修复 | 独立 control mutex + 链路调用移出短状态锁 + RX 有限等待和原子 callback drain tests |
| BL0942-MUTEX-DELAY：hard reset 持缓存锁约 2 秒 | 已修复 | 拆分 cache/io mutex，复位仅独占 UART I/O；reset locking test 验证缓存读取不受阻塞 |

## 生命周期台账核对

| 原问题 | 当前结论 | 依据 |
|--------|----------|------|
| app M1：destroy 在 stop 失败后不释放对象 | 关闭，非消费式失败语义 | 未完成清理仍可能引用 controller；失败保留句柄、重试只处理 pending 项，测试覆盖首次失败后成功 destroy |
| MS-03：destroy 在 stop 失败后不释放对象 | 关闭，非消费式失败语义 | handler 注销失败时不能释放其 `arg=me`；测试覆盖部分注销失败后保留句柄并重试 |
| network_manager D-1：destroy 失败后不释放 manager | 关闭，非消费式失败语义 | monitor/link RX bridge 仍可能引用 manager；保持 `destroying`，链路 stop 失败时另保留 `stop_pending`，测试覆盖 link stop 失败 |
| button M1：底层 delete 失败后不释放 button | 接受当前约束，保留升级复核项 | 当前仅 GPIO backend 且 GPIO 创建前已校验，ESP-IDF 6.0 `gpio_reset_pin(valid_gpio)` 只返回 `ESP_OK`；`espressif/button` 4.1.6 对未来可失败 backend 存在部分销毁风险 |

## 仍需处理

| 优先级 | 问题 | 当前判断 |
|--------|------|----------|
| P3 | app_controller M6：引用 thingsboard_client 内部头 | 应把响应格式化提升为公共 API 或由 ThingsBoard 层封装 |
| P3 | app_controller M5：GET_POWER_LIMIT 失败不返回 RPC 错误 | 需先定义 ThingsBoard 错误响应格式 |
| P4 | app_controller M4：1 Hz 成功遥测使用 ESP_LOGI | 应降为 debug 或抽样日志 |
| P4 | LVGL M1：tick_ctx 每次 destroy 泄漏约 20 字节 | 需确认 ESP-IDF 6.0 `esp_timer_stop_blocking` 契约后修复 |

## 验证边界

- Host tests 覆盖状态机、失败重试、事件顺序和 ST7789T 命令值。
- ESP-IDF 全量构建用于验证目标头文件和组件 API 兼容性。
- LTE/Wi-Fi 实际切换时延、TFT 颜色方向和安全关断时延仍需实机验证。
