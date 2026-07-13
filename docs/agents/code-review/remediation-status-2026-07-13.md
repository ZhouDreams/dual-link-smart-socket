# Code Review 修复状态

**状态日期**：2026-07-13

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

## 仍需处理

| 优先级 | 问题 | 当前判断 |
|--------|------|----------|
| P1 防御性 | MS-02 / app M2：stop 尾部 mutex take 失败会遗留 `stopping` | `portMAX_DELAY` 下触发概率极低，但状态恢复路径仍不完整 |
| P2 | network_manager C-1：持锁调用链路 set_active / subscribe | 当前链路 API 为快速异步提交；扩展同步链路前应解耦 |
| P2 | network_manager C-2：链路事件任务无限等待 manager mutex | 当前临界区短，但会放大未来阻塞 |
| P2 | BL0942-MUTEX-DELAY：hard reset 持锁约 2 秒 | 当前无外部并发读取调用方，公共 API 风险仍存在 |
| P3 | app_controller M6：引用 thingsboard_client 内部头 | 应把响应格式化提升为公共 API 或由 ThingsBoard 层封装 |
| P3 | app_controller M5：GET_POWER_LIMIT 失败不返回 RPC 错误 | 需先定义 ThingsBoard 错误响应格式 |
| P4 | app_controller M4：1 Hz 成功遥测使用 ESP_LOGI | 应降为 debug 或抽样日志 |
| P4 | LVGL M1：tick_ctx 每次 destroy 泄漏约 20 字节 | 需确认 ESP-IDF 6.0 `esp_timer_stop_blocking` 契约后修复 |

## 验证边界

- Host tests 覆盖状态机、失败重试、事件顺序和 ST7789T 命令值。
- ESP-IDF 全量构建用于验证目标头文件和组件 API 兼容性。
- LTE/Wi-Fi 实际切换时延、TFT 颜色方向和安全关断时延仍需实机验证。
