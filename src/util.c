#include "../include/amds.h"
#include <string.h>
#include <stdlib.h>
#include <sys/stat.h>
#include <errno.h>

int amds_path_join(char *out, size_t outsz, const char *a, const char *b) {
    int r = snprintf(out, outsz, "%s/%s", a, b);
    if (r < 0 || (size_t)r >= outsz) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] path_join failed for %s and %s", a, b);
        return -1;
    }
    return 0;
}

int amds_read_first_line(const char *path, char *buf, size_t sz) {
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] reading %s", path);
    FILE *f = fopen(path, "r");
    if (!f) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] failed to open %s: %s", path, strerror(errno));
        return -1;
    }
    if (!fgets(buf, sz, f)) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] failed to read from %s", path);
        fclose(f);
        return -1;
    }
    fclose(f);
    size_t n = strlen(buf);
    while (n && (buf[n - 1] == '\n' || buf[n - 1] == '\r')) buf[--n] = 0;
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] read from %s: %s", path, buf);
    return 0;
}

int amds_read_u64(const char *path, uint64_t *v) {
    char buf[128];
    if (amds_read_first_line(path, buf, sizeof(buf)) < 0) return -1;
    *v = strtoull(buf, NULL, 0);
    return 0;
}

int amds_read_double_scaled(const char *path, double scale, double *v) {
    uint64_t t;
    if (amds_read_u64(path, &t) < 0) return -1;
    *v = (double)t / scale;
    return 0;
}

int amds_mkdir_p(const char *path) {
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] mkdir -p %s", path);
    char tmp[4096];
    snprintf(tmp, sizeof(tmp), "%s", path);

    for (char *p = tmp + 1; *p; p++) {
        if (*p == '/') {
            *p = 0;
            if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
                if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] mkdir %s failed: %s", tmp, strerror(errno));
                return -1;
            }
            *p = '/';
        }
    }
    if (mkdir(tmp, 0755) < 0 && errno != EEXIST) {
        if (g_amds_logger) amds_log_printf(g_amds_logger, "[SYS] mkdir %s failed: %s", tmp, strerror(errno));
        return -1;
    }
    return 0;
}