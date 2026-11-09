p='/home/user/Flex-OS-Ecosistema/FlexOS_Ultra_IDF/tests/host/test_ota.c'
s=open(p).read()
def rep(a,b,cnt=1):
    global s
    assert s.count(a)>=1, a
    s=s.replace(a,b,cnt)
# 1) campos mas largos que los buffers del modelo (md5, url, firma, changelog): se acotan igual que en Arduino
L80='ab12'*20
L260='https://x.example/'+('p/'*121)+'fw.bin'
L600='-'.join(['Linea%03d'%i for i in range(75)])
L200='0123456789abcdef'*13
rep('"https://a/b?c=d&e=f",\n                                      "HTTPS://upper/fw.bin"};','"https://a/b?c=d&e=f",\n                                      "HTTPS://upper/fw.bin", "%s"};'%L260)
rep('"zzzd8cd98f00b204e9800998ecf8427e", ""};','"zzzd8cd98f00b204e9800998ecf8427e", "", "%s"};'%L80)
rep('"\\\\/"};','"\\\\/", "%s"};'%L600)
rep('0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef"};','0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef", "%s"};'%L200)
rep('char blk[1400];','char blk[3200];')
rep('pick(URL, 7)','pick(URL, 8)')
rep('pick(MD5, 6)','pick(MD5, 7)')
rep('pick(LOG, 6)','pick(LOG, 7)')
rep('pick(SIG, 3)','pick(SIG, 4)')
# 2) los bordes del micro-lector
edges='''
// El micro-lector no mira mas alla de `end`: cada clave cortada en cada punto, en un bloque EXACTO del heap (ASan vigila un byte de mas), contra Arduino; las
// comillas sueltas delante de una clave; y una cadena que empieza justo en `end` no toca el buffer de salida.
static void t_json_edges(void)
{
    static const char *const KEYS[] = {"version", "url", "md5", "size", "changelog", "sig", "flex_os_ultra_idf", "a}"};
    for (int k = 0; k < 8; k++) {
        char full[96];
        snprintf(full, sizeof(full), "\\"%s\\" : 1,", KEYS[k]);
        size_t fl = strlen(full);
        for (size_t L = 0; L <= fl; L++) {
            char *h = (char *)malloc(L + 1);
            memcpy(h, full, L);
            h[L] = 0;
            const char *f1 = flex_ota_json_find(h, h + L, KEYS[k]);
            if (k < 7) CHECK(f1 == R[0].find(h, KEYS[k]));
            else (void)f1;   // la clave con una llave dentro: solo la memoria se vigila
            free(h);
        }
        char *h = (char *)malloc(fl + 3);   // `end` antes del final de un buffer mas largo, con la clave justo despues
        memcpy(h, full, fl);
        memcpy(h + fl, "xyz", 3);
        for (size_t L = 0; L < fl; L++) {
            const char *f1 = flex_ota_json_find(h, h + L, KEYS[k]);
            CHECK(f1 == NULL || f1 <= h + L);
        }
        free(h);
    }
    static const char *const ODD[] = {"\\"\\"version\\": 1", "x\\"\\"url\\":\\"u\\"", "\\"\\"\\"\\"md5\\":2", "\\"a\\"\\"size\\":3", "\\"\\"changelog\\" : \\"c\\"", "\\"\\"sig\\":\\"\\"", "\\"\\"flex_os_ultra_idf\\":{}"};
    static const char *const OK[] = {"version", "url", "md5", "size", "changelog", "sig", "flex_os_ultra_idf"};
    for (int i = 0; i < 7; i++) {
        const char *f1 = flex_ota_json_find(ODD[i], ODD[i] + strlen(ODD[i]), OK[i]);
        CHECK(f1 != NULL && f1 == R[0].find(ODD[i], OK[i]));
    }
    char out[8];
    const char *str = "x\\"abc\\"";
    memset(out, 0x55, sizeof(out));
    CHECK(!flex_ota_json_str(str + 1, str + 1, out, sizeof(out)) && (unsigned char)out[0] == 0x55);   // p == end: ni se lee ni se escribe
    CHECK(!flex_ota_json_str(NULL, str, out, sizeof(out)) && (unsigned char)out[0] == 0x55);
    CHECK(!flex_ota_json_str(str + 1, str + 6, out, 0) && (unsigned char)out[0] == 0x55);
    // un \\u cortado justo en `end` (4 digitos que no estan)
    const char *cut = "\\"a\\\\u12";
    CHECK(!flex_ota_json_str(cut, cut + strlen(cut), out, sizeof(out)));
}
'''
rep('// ---- la comprobacion del manifiesto en paso con Arduino (otaCheck) ----', edges+'\n// ---- la comprobacion del manifiesto en paso con Arduino (otaCheck) ----')
rep('    s_seed = 11;\n    static const char *const FR[]','    s_seed = 11;\n    t_json_edges();\n    static const char *const FR[]')
if '#include <stdlib.h>' not in s:
    rep('#include <string.h>\n#include "flex_ota.h"','#include <stdlib.h>\n#include <string.h>\n#include "flex_ota.h"')
open(p,'w').write(s)
print("ok")
