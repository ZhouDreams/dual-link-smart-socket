# AGENTS_ZH.md

本文件为 AI 编码助手在此仓库中工作时的索引。

## 项目概述

Smart_Socket 是一个基于 ESP32-S3 的双模联网智能插座系统，集成 BL0942 电参量采集、继电器控制、本地按键输入、LVGL 本地显示、ThingsBoard 云端遥测与 RPC，以及 Wi-Fi/LTE 双模联网与自动故障切换。

## 文档索引

| 主题 | 文档 |
|------|------|
| 目录说明 | [docs/agents/directory-structure.md](docs/agents/directory-structure.md) |
| 架构概览 | [docs/agents/architecture.md](docs/agents/architecture.md) |
| 构建与调试 | [docs/agents/build-and-debug.md](docs/agents/build-and-debug.md) |
| 代码规范与模板 | [docs/agents/coding-style.md](docs/agents/coding-style.md) |
| C 语言 OOP 设计规范 | [docs/agents/oop-design.md](docs/agents/oop-design.md) |
| 类设计 | [docs/agents/classes.md](docs/agents/classes.md) |
| 错误处理机制 | [docs/agents/err.md](docs/agents/err.md) |

## 文件使用指南

- 编写代码时**必须**遵循 [代码规范与模板](docs/agents/coding-style.md) 和 [C 语言 OOP 设计规范](docs/agents/oop-design.md)
- 涉及编译项目、烧录、串口监视时**必须**遵循 [构建与调试](docs/agents/build-and-debug.md)
- [reference/](reference/) 存放硬件数据手册、旧项目 EEE532 及毕业论文等参考资料，仅供查阅，主项目的构建与编码**必须**以本文件及上述规范为准
- 在编写任何模块实现代码之前，**必须**先在 [docs/agents/classes.md](docs/agents/classes.md) 中定义该模块的类，格式参照 [esp-lwlte 的 classes.md](../esp-lwlte/docs/agents/classes.md)

## 文档修改指南

- 不要直接修改本文件（AGENTS_ZH.md），除非需要调整索引结构
- 内容变更请修改 `docs/agents/` 下的对应文件
- 如需新增主题，在 `docs/agents/` 下创建新文件并在此添加链接
