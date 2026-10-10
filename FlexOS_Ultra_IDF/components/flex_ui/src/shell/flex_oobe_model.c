// Flex OS Ultra · OOBE, reglas del nombre. Ver flex_oobe_model.h.
#include "flex_oobe_model.h"

#include <stdio.h>
#include <string.h>

bool flex_oobe_name_key(char *buf, size_t cap, char key)
{
    size_t n = strlen(buf);
    size_t max = cap - 1 < FLEX_OOBE_NAME_MAX ? cap - 1 : FLEX_OOBE_NAME_MAX;
    if (key == '\b') {
        if (n == 0) {
            return false;
        }
        buf[n - 1] = 0;
        return true;
    }
    bool letter = key >= 'A' && key <= 'Z';
    bool space = key == ' ' && n > 0;
    if ((!letter && !space) || n >= max) {
        return false;
    }
    buf[n] = key;
    buf[n + 1] = 0;
    return true;
}

void flex_oobe_name_final(const char *in, char *out, size_t cap)
{
    snprintf(out, cap, "%s", in && in[0] ? in : FLEX_OOBE_NAME_DEFAULT);
}
