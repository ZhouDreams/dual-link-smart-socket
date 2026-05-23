# AGENTS.md

This file serves as an index for AI coding agents working with this repository.

## Project Overview

Smart_Socket is a dual-mode networked smart socket system based on ESP32-S3, featuring BL0942 power metering, relay control, local button input, LVGL display, ThingsBoard cloud integration, and Wi-Fi/LTE dual-mode networking with automatic failover.

## Document Index

| Topic | Document |
|-------|----------|
| Directory Structure | [docs/agents/directory-structure.md](docs/agents/directory-structure.md) |
| Architecture | [docs/agents/architecture.md](docs/agents/architecture.md) |
| Class Design | [docs/agents/classes.md](docs/agents/classes.md) |
| Build & Debug | [docs/agents/build-and-debug.md](docs/agents/build-and-debug.md) |
| Coding Style & Templates | [docs/agents/coding-style.md](docs/agents/coding-style.md) |
| C OOP Design Guidelines | [docs/agents/oop-design.md](docs/agents/oop-design.md) |
| Error Handling | [docs/agents/err.md](docs/agents/err.md) |

## File Usage Guide

- When writing code, you **MUST** follow [Coding Style & Templates](docs/agents/coding-style.md) and [C OOP Design Guidelines](docs/agents/oop-design.md)
- When building, flashing, or monitoring serial output, you **MUST** follow [Build & Debug](docs/agents/build-and-debug.md)
- [reference/](reference/) contains hardware datasheets, the legacy EEE532 project, and the graduation thesis — for reference only; all main project work **MUST** follow this file and the guidelines above
- Before writing any module implementation, **MUST** first define its classes in [docs/agents/classes.md](docs/agents/classes.md) following the same format as [esp-lwlte's classes.md](../esp-lwlte/docs/agents/classes.md)

## Document Modification Guide

- Do NOT modify this file (AGENTS.md) directly unless changing the index structure
- Update the corresponding file under `docs/agents/` for content changes
- If adding a new topic, create a new file in `docs/agents/` and add the link here
