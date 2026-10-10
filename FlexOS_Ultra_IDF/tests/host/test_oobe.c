// OOBE: reglas del nombre del equipo contra la referencia de Arduino (oobeNameTick).
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "flex_oobe_model.h"
#include "flex_test.h"

// Referencia: Home.h:405-436 (cfgName de 24, max 20)
static void ref_key(char *nm, char key)
{
    int len = (int)strlen(nm);
    if (key == '\b') {
        if (len > 0) {
            nm[len - 1] = 0;
        }
    } else if (key == ' ') {
        if (len > 0 && len < 20) {
            nm[len] = ' ';
            nm[len + 1] = 0;
        }
    } else if (key >= 'A' && key <= 'Z' && len < 20) {
        nm[len] = key;
        nm[len + 1] = 0;
    }
}

void test_oobe(void)
{
    const char keys[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZ \b";
    char a[24] = "", b[24] = "";
    uint32_t seed = 7;
    int same = 0;
    for (int i = 0; i < 20000; i++) {
        seed = seed * 1103515245u + 12345u;
        char k = keys[(seed >> 16) % (sizeof(keys) - 1)];
        if (i % 97 == 0) {
            a[0] = b[0] = 0;
        }
        flex_oobe_name_key(a, sizeof(a), k);
        ref_key(b, k);
        same += strcmp(a, b) == 0;
    }
    CHECK_EQ_I(same, 20000);
    char o[24];
    flex_oobe_name_final("", o, sizeof(o));
    CHECK(strcmp(o, "FlexOS Ultra") == 0);
    flex_oobe_name_final("SALON", o, sizeof(o));
    CHECK(strcmp(o, "SALON") == 0);
    char c[24] = "";
    CHECK(!flex_oobe_name_key(c, sizeof(c), ' '));   // no se empieza por espacio
    CHECK(!flex_oobe_name_key(c, sizeof(c), 'a'));   // solo mayusculas
    CHECK(!flex_oobe_name_key(c, sizeof(c), '\b'));
    printf("oobe: nombre igual a Arduino en 20000 teclas\n");
}
