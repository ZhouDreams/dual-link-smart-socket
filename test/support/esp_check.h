#pragma once

#define ESP_RETURN_ON_FALSE(cond, err_code, tag, fmt, ...) \
    do {                                                    \
        (void)(tag);                                        \
        if (!(cond)) {                                      \
            return (err_code);                              \
        }                                                   \
    } while (0)
