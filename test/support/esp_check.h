#pragma once

#define ESP_RETURN_ON_FALSE(cond, err_code, tag, fmt, ...) \
    do {                                                    \
        (void)(tag);                                        \
        if (!(cond)) {                                      \
            return (err_code);                              \
        }                                                   \
    } while (0)

#define ESP_GOTO_ON_FALSE(cond, err_code, goto_tag, tag, fmt, ...) \
    do {                                                            \
        (void)(tag);                                                \
        if (!(cond)) {                                              \
            ret = (err_code);                                       \
            goto goto_tag;                                          \
        }                                                           \
    } while (0)

#define ESP_RETURN_ON_ERROR(expr, tag, fmt, ...) \
    do {                                          \
        (void)(tag);                              \
        esp_err_t err_rc_ = (expr);               \
        if (err_rc_ != ESP_OK) {                  \
            return err_rc_;                       \
        }                                         \
    } while (0)

#define ESP_GOTO_ON_ERROR(expr, goto_tag, tag, fmt, ...) \
    do {                                                    \
        (void)(tag);                                        \
        esp_err_t err_rc_ = (expr);                         \
        if (err_rc_ != ESP_OK) {                            \
            ret = err_rc_;                                  \
            goto goto_tag;                                  \
        }                                                   \
    } while (0)
