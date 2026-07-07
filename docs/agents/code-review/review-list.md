# Code Review List

本清单根据 `docs/agents/review-checklist.md` 阶段 1 建立，覆盖 `main/` 下全部业务/驱动/网络/服务/应用模块。

本次审查范围：阶段 2（审查）+ 阶段 3（验证），不包含阶段 4（修复）——用户明确要求"先只检查、验证、出具报告，不修改代码"。

审查完成日期：2026-07-07

| # | Module | Path | Status | Report |
|---|--------|------|--------|--------|
| 01 | board_pinmap | main/platform/board_pinmap.{c,h} | ✅ Done | report-01-board_pinmap.md |
| 02 | relay | main/relay/relay.{c,h} | ✅ Done | report-02-relay.md |
| 03 | button | main/button/button.{c,h}, button_iot_adapter.{c,h} | ✅ Done | report-03-button.md |
| 04 | bl0942 | main/bl0942/bl0942.{c,h} | ✅ Done | report-04-bl0942.md |
| 05 | network_link | main/network/network_link.{c,h}, network_link_priv.h, network_types.h | ✅ Done | report-05-network_link.md |
| 06 | network_manager | main/network/network_manager.{c,h} | ✅ Done | report-06-network_manager.md |
| 07 | wifi_link | main/network/wifi/wifi_link.{c,h}, wifi_link_internal.{c,h} | ✅ Done | report-07-wifi_link.md |
| 08 | lte_link | main/network/lte/lte_link.{c,h}, lte_link_internal.{c,h} | ✅ Done | report-08-lte_link.md |
| 09 | metering_service | main/metering/metering_service.{c,h}, metering_service_internal.{c,h} | ✅ Done | report-09-metering_service.md |
| 10 | safety_guard | main/safety/safety_guard.{c,h} | ✅ Done | report-10-safety_guard.md |
| 11 | thingsboard_client | main/thingsboard/thingsboard_client.{c,h}, thingsboard_client_internal.{c,h} | ✅ Done | report-11-thingsboard_client.md |
| 12 | tft_panel | main/display/tft/tft_panel.{c,h}, tft_panel_st7789t.{c,h} | ✅ Done | report-12-tft_panel.md |
| 13 | lvgl_dashboard | main/display/lvgl/lvgl_dashboard.{c,h}, lvgl_dashboard_internal.{c,h} | ✅ Done | report-13-lvgl_dashboard.md |
| 14 | app_controller | main/app/app_controller.{c,h}, app_controller_internal.{c,h}, main/main.c | ✅ Done | report-14-app_controller.md |

## 审查进度说明

- 状态：`⬜ Pending` / `🔄 In Progress` / `✅ Done` / `⏭️ Skipped`
- 全部 14 个模块已完成阶段 2（report-*.md）+ 阶段 3（verify-*.md）
- 本次只做检查 + 验证 + 报告，不做阶段 4（修复）
- 汇总报告见 `summary-2026-07-07.md`
