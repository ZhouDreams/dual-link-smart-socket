# Code Review: board_pinmap

**日期**: 2026-07-07
**文件**: main/platform/board_pinmap.c, main/platform/board_pinmap.h

## 审查范围

board_pinmap 是驱动适配层的基础模块，为所有硬件模块提供统一的 GPIO 映射。实现为编译期 `static const` 单例，对外仅暴露 `board_pinmap_get()` 只读指针。

对应文档：classes.md §1（Board Pinmap）、architecture.md §3.4（驱动适配层）。

## 🔴 高严重度

无。

## 🟡 中严重度

无。

## 🟢 低严重度

无。

## 无问题维度

- **A. 资源账本与乘法型分配**：无堆分配。`s_pinmap` 为 `static const`，编译期确定，落入 `.rodata`。sizeof(board_pinmap_t) = 16 字段 × 4 bytes = 64 bytes。无 `count * size` 乘法型占用，无池化分配。
- **B. 内存安全与生命周期**：无指针运算、无 VLA、无缓冲区。`board_pinmap_get()` 返回 `&s_pinmap`（static storage，程序生命周期内有效）。返回类型为 `const board_pinmap_t *`，编译器阻止写入。
- **C. 并发、竞态、死锁与实时性**：`s_pinmap` 为 `static const`，初始化在程序加载时完成（先于 `main`），运行期不可变。读取不可变数据无需同步，天然线程安全。无锁、无任务、无 ISR。
- **D. 失败路径完整性**：`board_pinmap_get()` 不可失败（无 malloc、无资源创建），始终返回有效指针。无失败路径。
- **E. 跨模块契约**：无依赖（classes.md "board_pinmap (无依赖)"）。`board_pinmap.h` 仅 include `driver/gpio.h`（获取 `gpio_num_t`），不依赖任何项目内模块。被 `app_controller`（`main.c:83` `board_pinmap_get()`）和各驱动模块借用读取。`app_controller.h:51` 存储 `const board_pinmap_t *pinmap` 标注为 borrowed，契约清晰。
- **F. 类型与边界**：所有字段使用 `gpio_num_t` 和 `board_active_level_t`，类型一致。14 个 GPIO 编号唯一无冲突（2, 4, 5, 6, 7, 8, 10, 11, 39, 40, 41, 42, 45, 46）。`s_pinmap` 使用 designated initializers 完整初始化全部 16 个字段。GPIO45/46 虽为 strapping pin，但用于 TFT MOSI/背光，在 boot 后初始化，不影响 boot-time strapping。
- **G. 代码质量与一致性**：`.c` 和 `.h` 严格遵循 `coding-style.md` 模板的 section 组织（INCLUDES / DEFINES / TYPEDEFS / STATIC PROTOTYPES / STATIC VARIABLES / MACROS / GLOBAL FUNCTIONS / STATIC FUNCTIONS）。Doxygen 双语注释格式合规（@brief 中文 + @details 英文 + 行尾 `/**< 中文； English */`）。`board_pinmap_get()` 圈复杂度 = 1。
- **H. 文档与注释一致性**：`board_pinmap_t` 结构体字段、顺序与 classes.md §1.3 完全一致。`board_active_level_t` 枚举值与 classes.md §1.2 一致。`board_pinmap_get()` 签名与 classes.md §1.3 API 一致。classes.md §1.3 关键设计决策（单例、只读、`gpio_num_t` 原生类型、不含 UART port/SPI host）均与实现吻合。
