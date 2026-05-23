# 错误处理机制

本项目仅面向 ESP-IDF 平台，直接使用 ESP-IDF 内置的 `esp_err_t` 和 `esp_check.h` 错误检查宏，不自定义错误码体系。

---

## 1. 返回值约定

### 1.1 所有公共 API 返回 `esp_err_t`

公共 API 默认返回 `esp_err_t`。

```c
esp_err_t relay_set(relay_t *me, relay_source_t source, bool on);
esp_err_t metering_service_get_snapshot(metering_service_t *me,
                                        metering_snapshot_t *out);
esp_err_t network_manager_publish(network_manager_t *me,
                                  const network_publish_request_t *req);
```

例外情况：

1. `create` 函数如果采用直接返回句柄风格，可返回 `xxx_t *`，失败返回 `NULL`。
2. 简单只读 getter 可以返回值类型，但前提是不会失败且语义明确。
3. 回调函数按 ESP-IDF 或第三方组件要求定义。

### 1.2 网络基类 wrapper 和 ops 方法统一返回 `esp_err_t`

网络层是本项目使用继承和多态的核心区域。

`network_link_*` 包装 API 和内部 ops 方法必须统一返回 `esp_err_t`：

```c
esp_err_t network_link_start(network_link_t *me);
esp_err_t network_link_publish(network_link_t *me,
                               const network_publish_request_t *req);

typedef struct {
    esp_err_t (*start)(network_link_t *me);
    esp_err_t (*publish)(network_link_t *me,
                         const network_publish_request_t *req);
} network_link_ops_t;
```

### 1.3 常用错误码

| 错误码 | 含义 |
|--------|------|
| `ESP_OK` | 成功 |
| `ESP_FAIL` | 通用失败 |
| `ESP_ERR_INVALID_ARG` | 参数无效 |
| `ESP_ERR_NO_MEM` | 内存不足 |
| `ESP_ERR_TIMEOUT` | 超时 |
| `ESP_ERR_INVALID_STATE` | 状态错误 |
| `ESP_ERR_NOT_SUPPORTED` | 不支持的操作 |
| `ESP_ERR_NOT_FOUND` | 未找到 |
| `ESP_ERR_INVALID_SIZE` | 缓冲区大小不足或长度非法 |
| `ESP_ERR_INVALID_RESPONSE` | 外设或协议响应非法 |

完整列表参考 ESP-IDF Error Codes 文档。

---

## 2. ESP-IDF 内置错误检查宏

以下宏定义在 `<esp_check.h>`，本项目直接使用。

核心约定：`ESP_GOTO_ON_*` 系列依赖的局部变量必须命名为 `ret`。

### 2.1 宏一览

| 宏 | 行为 | 适用场景 |
|----|------|----------|
| `ESP_ERROR_CHECK(x)` | `x != ESP_OK` 时打印并 `abort()` | 致命错误，无法继续运行 |
| `ESP_ERROR_CHECK_WITHOUT_ABORT(x)` | `x != ESP_OK` 时打印，不终止 | 调试期临时检查 |
| `ESP_RETURN_ON_ERROR(x, TAG, fmt, ...)` | `x != ESP_OK` 时打印并返回 `x` | 无资源需清理，失败直接返回 |
| `ESP_RETURN_ON_FALSE(a, err_code, TAG, fmt, ...)` | `a == false` 时打印并返回 `err_code` | 参数或状态检查 |
| `ESP_GOTO_ON_ERROR(x, goto_tag, TAG, fmt, ...)` | `x != ESP_OK` 时打印、设 `ret`、跳转 | 有资源需清理 |
| `ESP_GOTO_ON_FALSE(a, err_code, goto_tag, TAG, fmt, ...)` | `a == false` 时打印、设 `ret`、跳转 | 资源创建失败需清理 |
| `ESP_RETURN_ON_ERROR_CLEANUP(x, ...)` | `x != ESP_OK` 时执行清理代码后返回 | 轻量清理 |
| `ESP_RETURN_VOID_ON_ERROR(x, TAG, fmt, ...)` | `x != ESP_OK` 时打印并返回 | void 函数中的错误检查 |
| `ESP_RETURN_VOID_ON_FALSE(a, TAG, fmt, ...)` | `a == false` 时打印并返回 | void 函数中的条件检查 |

### 2.2 `ESP_ERROR_CHECK` 使用范围

`ESP_ERROR_CHECK` 只能用于最外层不可恢复初始化，例如 `main.c` 中的基础系统初始化：

```c
void app_main(void)
{
    ESP_ERROR_CHECK(nvs_flash_init());
    ESP_ERROR_CHECK(esp_netif_init());
    ESP_ERROR_CHECK(esp_event_loop_create_default());
    ESP_ERROR_CHECK(app_controller_start());
}
```

普通模块内部不应使用 `ESP_ERROR_CHECK` 终止系统，而应返回错误给调用方处理。

### 2.3 `ESP_RETURN_ON_ERROR` / `ESP_RETURN_ON_FALSE`

用于无资源需清理的场景：

```c
esp_err_t relay_set(relay_t *me, relay_source_t source, bool on)
{
    ESP_RETURN_ON_FALSE(me != NULL, ESP_ERR_INVALID_ARG, TAG, "me is null");
    ESP_RETURN_ON_FALSE(source < RELAY_SOURCE_MAX, ESP_ERR_INVALID_ARG,
                        TAG, "invalid source");

    return gpio_set_level(me->gpio, on ? me->active_level : !me->active_level);
}
```

### 2.4 `ESP_GOTO_ON_ERROR` / `ESP_GOTO_ON_FALSE`

用于有资源需释放的场景。

硬性要求：

1. 函数开头定义 `esp_err_t ret = ESP_OK;`。
2. cleanup 标签统一命名为 `err`。
3. cleanup 区按资源创建的逆序释放。

```c
esp_err_t metering_service_create(const metering_service_config_t *config,
                                  metering_service_t **out)
{
    ESP_RETURN_ON_FALSE(config != NULL && out != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    esp_err_t ret = ESP_OK;
    metering_service_t *me = calloc(1, sizeof(*me));
    ESP_GOTO_ON_FALSE(me != NULL, ESP_ERR_NO_MEM, err, TAG,
                      "calloc metering service failed");

    me->mutex = xSemaphoreCreateMutex();
    ESP_GOTO_ON_FALSE(me->mutex != NULL, ESP_ERR_NO_MEM, err, TAG,
                      "create mutex failed");

    *out = me;
    return ESP_OK;

err:
    if (me != NULL) {
        if (me->mutex != NULL) {
            vSemaphoreDelete(me->mutex);
        }
        free(me);
    }
    return ret;
}
```

### 2.5 `ESP_RETURN_ON_ERROR_CLEANUP`

当清理动作很短时使用：

```c
esp_err_t thingsboard_client_publish_json(thingsboard_client_t *me, char *buf)
{
    ESP_RETURN_ON_FALSE(me != NULL && buf != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");

    ESP_RETURN_ON_ERROR_CLEANUP(
        network_manager_publish(me->network, &me->request),
        free(buf)
    );

    free(buf);
    return ESP_OK;
}
```

### 2.6 自定义宏：`ESP_LOG_ON_ERROR` — 失败可忽略

ESP-IDF 未内置"仅记录日志"的宏，项目自定义如下：

```c
#define ESP_LOG_ON_ERROR(x, tag, fmt, ...)                                 \
    do {                                                                   \
        esp_err_t _err_ = (x);                                            \
        if (unlikely(_err_ != ESP_OK)) {                                   \
            ESP_LOGW(tag, "%s:%d: " fmt " (err=%s)",                      \
                     __FUNCTION__, __LINE__, ##__VA_ARGS__,                \
                     esp_err_to_name(_err_));                              \
        }                                                                  \
    } while (0)
```

适用场景：失败不影响主流程的操作——deinit、事件通知、信号量释放等：

```c
ESP_LOG_ON_ERROR(uart_driver_delete(UART_NUM), TAG, "uart deinit failed");

ESP_LOG_ON_ERROR(event_post(me, EVENT_STARTED), TAG, "post event failed");
```

---

## 3. 生命周期错误处理

### 3.1 create 函数

推荐两种 create 风格，项目内同一模块应保持一致。

风格 A：返回 `esp_err_t`，通过 out 参数返回句柄。

```c
esp_err_t relay_create(const relay_config_t *config, relay_t **out);
```

风格 B：直接返回句柄，失败返回 `NULL`。

```c
relay_t *relay_create(const relay_config_t *config);
```

无论使用哪种风格，create 失败都必须在函数内部完成资源回滚，不允许返回半初始化对象。

### 3.2 destroy 函数

`destroy` 必须支持传入 `NULL` 并安全返回。

```c
void relay_destroy(relay_t *me)
{
    if (me == NULL) {
        return;
    }

    /* 逆序释放资源 */
    free(me);
}
```

### 3.3 start / stop 函数

`start` 和 `stop` 默认返回 `esp_err_t`。

推荐语义：

1. 未 create 或未 init 时返回 `ESP_ERR_INVALID_STATE`。
2. 重复 start 如果没有副作用，可以返回 `ESP_OK`。
3. 重复 stop 如果没有副作用，可以返回 `ESP_OK`。
4. 需要严格状态机的模块，可以对重复 start 返回 `ESP_ERR_INVALID_STATE`，但必须在文档中说明。

---

## 4. 资源清理规则

### 4.1 逆序释放

资源必须按创建的逆序释放。

```text
创建顺序：对象 → mutex → queue → task
释放顺序：task → queue → mutex → 对象
```

### 4.2 指针置空

如果对象仍可能继续存在，每个 `free` / `delete` 后应置 `NULL`。

如果对象即将整体释放，内部字段不强制逐个置空。

### 4.3 任务退出

任务不应被粗暴删除正在运行的逻辑。

推荐模式：

1. 设置 stop 标志。
2. 唤醒阻塞任务。
3. 等待任务通过 done semaphore 通知退出。
4. 释放任务相关资源。

---

## 5. 网络多态错误处理

### 5.1 wrapper 负责公共检查

`network_link_*` wrapper 负责检查 `me`、`ops` 和必填方法。

```c
esp_err_t network_link_publish(network_link_t *me,
                               const network_publish_request_t *req)
{
    ESP_RETURN_ON_FALSE(me != NULL && req != NULL, ESP_ERR_INVALID_ARG,
                        TAG, "invalid argument");
    ESP_RETURN_ON_FALSE(me->ops != NULL && me->ops->publish != NULL,
                        ESP_ERR_NOT_SUPPORTED, TAG, "publish not supported");

    return me->ops->publish(me, req);
}
```

### 5.2 必填方法

网络链路的以下方法属于必填：

1. `start`
2. `stop`
3. `get_status`
4. `publish`
5. `subscribe`
6. `unsubscribe`

必填方法缺失时，wrapper 返回 `ESP_ERR_NOT_SUPPORTED`，调试构建中也可以配合 `assert` 暴露实现错误。

### 5.3 选填方法

如果后续出现可选能力，例如低功耗、信号质量查询、手动重连，可以设计为选填方法。

选填方法缺失时，wrapper 应提供明确默认行为，例如返回 `ESP_ERR_NOT_SUPPORTED` 或安静跳过。

---

## 6. 日志规则

每个 `.c` 文件定义自己的 TAG：

```c
#define TAG "relay"
```

日志级别约定：

| 级别 | 用途 |
|------|------|
| `ESP_LOGE` | 模块无法继续工作、关键动作失败 |
| `ESP_LOGW` | 可恢复异常、降级、重试 |
| `ESP_LOGI` | 关键生命周期、联网成功、状态切换 |
| `ESP_LOGD` | 调试细节、周期性低价值信息 |

周期性遥测、采样循环、频繁事件默认不使用 `ESP_LOGI`，避免刷屏。

---

## 7. 禁止事项

| 禁止项 | 替代方案 |
|--------|----------|
| 自定义 `SMART_SOCKET_ERR_*` 错误码体系 | 使用 ESP-IDF 内置 `esp_err_t` 和标准错误码 |
| 自定义错误检查宏 | 使用 `esp_check.h` 的 `ESP_RETURN_ON_*` / `ESP_GOTO_ON_*` |
| `ESP_GOTO_ON_*` 使用 `ret` 之外的变量名 | 统一使用 `ret` |
| cleanup 标签随意命名 | 统一使用 `err` |
| 普通模块内部调用 `ESP_ERROR_CHECK` | 返回错误给调用方 |
| 返回 `int` 表示错误 | 统一返回 `esp_err_t` |
| create 失败后泄漏部分资源 | create 内部完整回滚 |
| destroy 不处理 `NULL` | destroy 对 `NULL` 安全返回 |
