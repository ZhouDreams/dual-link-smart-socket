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
