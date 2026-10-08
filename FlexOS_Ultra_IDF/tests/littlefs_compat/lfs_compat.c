// Compatibilidad del LittleFS de la version Arduino con el de la version ESP-IDF.
//
// Se compila dos veces: con el littlefs que usaba la version Arduino (el mas
// antiguo posible: esp_littlefs ^1.10.2 -> littlefs v2.8.0) y con el del
// firmware ESP-IDF (esp_littlefs v1.22.3, fijado en dependencies.lock). Las
// dos comparten una imagen de la particion "spiffs" (0xAE0000 B) que se
// comporta como una NOR real: borrar deja 0xFF y programar solo puede pasar
// bits de 1 a 0 (si alguna version reescribiera sin borrar, se detecta).
//
// Configuracion = la de esp_littlefs con los valores por defecto de Kconfig
// (iguales en la version Arduino y aqui): bloque 4096, lectura/escritura 128,
// cache 512, lookahead 128, block_cycles 512, block_count autodetectado.
//
//   lfs_compat <imagen> fill      formatea y escribe el conjunto de prueba
//   lfs_compat <imagen> check     comprueba el conjunto (y lo que anade "modify")
//   lfs_compat <imagen> modify    monta, comprueba, escribe/renombra/borra
//   lfs_compat <imagen> info      version de disco y bloques
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "lfs.h"

#define PART_SIZE  0xAE0000u
#define BLOCK      4096u

static uint8_t *s_img;
static int s_bad_prog;

static int bd_read(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, void *buf, lfs_size_t n)
{
    (void)c;
    memcpy(buf, s_img + (size_t)b * BLOCK + off, n);
    return 0;
}

static int bd_prog(const struct lfs_config *c, lfs_block_t b, lfs_off_t off, const void *buf, lfs_size_t n)
{
    (void)c;
    uint8_t *d = s_img + (size_t)b * BLOCK + off;
    const uint8_t *s = buf;
    for (lfs_size_t i = 0; i < n; i++) {
        if ((d[i] & s[i]) != s[i]) {
            s_bad_prog++;   // en una NOR esto no se puede escribir sin borrar
        }
        d[i] &= s[i];
    }
    return 0;
}

static int bd_erase(const struct lfs_config *c, lfs_block_t b)
{
    (void)c;
    memset(s_img + (size_t)b * BLOCK, 0xFF, BLOCK);
    return 0;
}

static int bd_sync(const struct lfs_config *c)
{
    (void)c;
    return 0;
}

static struct lfs_config cfg = {
    .read = bd_read, .prog = bd_prog, .erase = bd_erase, .sync = bd_sync,
    .read_size = 128, .prog_size = 128, .block_size = BLOCK, .block_count = 0,
    .cache_size = 512, .lookahead_size = 128, .block_cycles = 512,
};

static void pattern(uint8_t *p, size_t n, uint32_t seed)
{
    for (size_t i = 0; i < n; i++) {
        seed = seed * 1103515245u + 12345u;
        p[i] = (uint8_t)(seed >> 16);
    }
}

// Lo que la version Arduino deja en LittleFS: carpetas de Paint/Notas/Galeria y
// archivos de varios tamanos (inline < cache, de un bloque, de muchos).
static const struct { const char *path; size_t len; uint32_t seed; } k_files[] = {
    {"/paint/dibujo1.fpi", 230400, 1},
    {"/notes/nota.txt", 37, 2},
    {"/notes/lista de la compra.txt", 900, 3},
    {"/gallery/foto_0001.jpg", 61234, 4},
    {"/.flexsys/session.bin", 4096, 5},
    {"/music/cancion con nombre largo y acentuado — camión.wav", 300000, 6},
};
#define NFILES (sizeof(k_files) / sizeof(k_files[0]))

static int write_file(lfs_t *fs, const char *path, size_t len, uint32_t seed)
{
    uint8_t *buf = malloc(len ? len : 1);
    pattern(buf, len, seed);
    lfs_file_t f;
    int err = lfs_file_open(fs, &f, path, LFS_O_WRONLY | LFS_O_CREAT | LFS_O_TRUNC);
    if (!err) {
        lfs_ssize_t w = lfs_file_write(fs, &f, buf, len);
        err = lfs_file_close(fs, &f);
        if (w != (lfs_ssize_t)len) {
            err = -1;
        }
    }
    free(buf);
    return err;
}

static int check_file(lfs_t *fs, const char *path, size_t len, uint32_t seed)
{
    uint8_t *want = malloc(len ? len : 1), *got = malloc(len + 1);
    pattern(want, len, seed);
    lfs_file_t f;
    int err = lfs_file_open(fs, &f, path, LFS_O_RDONLY);
    if (!err) {
        lfs_ssize_t r = lfs_file_read(fs, &f, got, len + 1);
        lfs_file_close(fs, &f);
        if (r != (lfs_ssize_t)len || memcmp(want, got, len) != 0) {
            err = -1;
        }
    }
    if (err) {
        fprintf(stderr, "  %s: NO coincide (%d)\n", path, err);
    }
    free(want);
    free(got);
    return err;
}

static int mkdirs(lfs_t *fs, const char *path)
{
    char tmp[256];
    snprintf(tmp, sizeof(tmp), "%s", path);
    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            int e = lfs_mkdir(fs, tmp);
            if (e && e != LFS_ERR_EXIST) {
                return e;
            }
            *p = '/';
        }
    }
    return 0;
}

static int check_all(lfs_t *fs, int modified)
{
    int bad = 0;
    for (size_t i = 0; i < NFILES; i++) {
        if (modified && i == 1) {
            continue;   // "modify" lo reemplaza
        }
        bad += check_file(fs, k_files[i].path, k_files[i].len, k_files[i].seed) != 0;
    }
    if (modified) {
        bad += check_file(fs, "/notes/nota.txt", 5000, 77) != 0;     // reemplazo atomico
        bad += check_file(fs, "/paint/nuevo.fpi", 120000, 78) != 0;  // archivo nuevo
        struct lfs_info info;
        bad += lfs_stat(fs, "/notes/nota.txt.tmp~", &info) != LFS_ERR_NOENT;
        bad += lfs_stat(fs, "/gallery/borrar.jpg", &info) != LFS_ERR_NOENT;
    }
    return bad;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "uso: %s <imagen> fill|check|modify|info\n", argv[0]);
        return 2;
    }
    const char *img = argv[1], *mode = argv[2];
    s_img = malloc(PART_SIZE);
    FILE *f = fopen(img, "rb");
    if (f) {
        if (fread(s_img, 1, PART_SIZE, f) != PART_SIZE) {
            fprintf(stderr, "imagen corta\n");
            return 2;
        }
        fclose(f);
    } else {
        memset(s_img, 0xFF, PART_SIZE);   // flash borrada
    }
    lfs_t fs;
    int rc = 0;
    printf("[littlefs %d.%d, disco %d.%d] %s\n", LFS_VERSION_MAJOR, LFS_VERSION_MINOR, LFS_DISK_VERSION_MAJOR,
           LFS_DISK_VERSION_MINOR, mode);
    if (strcmp(mode, "fill") == 0) {
        cfg.block_count = PART_SIZE / BLOCK;   // como esp_littlefs al formatear
        if (lfs_format(&fs, &cfg)) {
            return 1;
        }
        cfg.block_count = 0;
        if (lfs_mount(&fs, &cfg)) {
            return 1;
        }
        for (size_t i = 0; i < NFILES; i++) {
            int e = mkdirs(&fs, k_files[i].path);
            if (!e) {
                e = write_file(&fs, k_files[i].path, k_files[i].len, k_files[i].seed);
            }
            if (e) {
                fprintf(stderr, "  no se pudo escribir %s (%d)\n", k_files[i].path, e);
                rc = 1;
            }
        }
        if (write_file(&fs, "/gallery/borrar.jpg", 7000, 9)) {
            rc = 1;
        }
        rc |= check_all(&fs, 0);
        lfs_unmount(&fs);
    } else {
        int err = lfs_mount(&fs, &cfg);
        if (err) {
            fprintf(stderr, "  NO MONTA (%d)\n", err);
            return 1;
        }
        if (strcmp(mode, "info") == 0) {
#if LFS_VERSION >= 0x00020007
            struct lfs_fsinfo fi;
            if (lfs_fs_stat(&fs, &fi) == 0) {
                printf("  disco %d.%d, %u bloques, name_max %u\n", (int)(fi.disk_version >> 16),
                       (int)(fi.disk_version & 0xffff), (unsigned)fi.block_count, (unsigned)fi.name_max);
            }
#endif
            printf("  usados %d bloques\n", (int)lfs_fs_size(&fs));
        } else if (strcmp(mode, "check") == 0) {
            rc = check_all(&fs, argc > 3 && strcmp(argv[3], "modified") == 0);
        } else if (strcmp(mode, "modify") == 0) {
            rc = check_all(&fs, 0);
            // Lo que hace flex_storage: escribir a ".tmp~" y renombrar encima.
            rc |= write_file(&fs, "/notes/nota.txt.tmp~", 5000, 77);
            rc |= lfs_rename(&fs, "/notes/nota.txt.tmp~", "/notes/nota.txt");
            rc |= write_file(&fs, "/paint/nuevo.fpi", 120000, 78);
            rc |= lfs_remove(&fs, "/gallery/borrar.jpg");
            rc |= check_all(&fs, 1);
        }
        lfs_unmount(&fs);
    }
    if (s_bad_prog) {
        fprintf(stderr, "  %d bytes programados sin borrar (no valdria en NOR)\n", s_bad_prog);
        rc = 1;
    }
    if (strcmp(mode, "check") != 0 && strcmp(mode, "info") != 0) {
        f = fopen(img, "wb");
        fwrite(s_img, 1, PART_SIZE, f);
        fclose(f);
    } else {
        // Montar y leer no debe cambiar ni un byte de la particion.
        FILE *g = fopen(img, "rb");
        uint8_t *orig = malloc(PART_SIZE);
        if (fread(orig, 1, PART_SIZE, g) != PART_SIZE || memcmp(orig, s_img, PART_SIZE) != 0) {
            fprintf(stderr, "  montar y leer CAMBIO la imagen\n");
            rc = 1;
        }
        fclose(g);
        free(orig);
    }
    printf("  %s\n", rc ? "FALLO" : "OK");
    return rc ? 1 : 0;
}
