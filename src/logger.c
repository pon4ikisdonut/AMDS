#include "../include/amds.h"
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <stdio.h>
#include <stdarg.h>
#include <time.h>

amds_logger_t *g_amds_logger = NULL;

int amds_logger_init(amds_logger_t *lg, const char *path) {
    memset(lg, 0, sizeof(*lg));
    pthread_mutex_init(&lg->lock, NULL);
    snprintf(lg->path, sizeof(lg->path), "%s", path);

    lg->fd = open(path, O_CREAT | O_WRONLY | O_APPEND | O_SYNC, 0644);
    if (lg->fd < 0) return -1;

    lg->fp = fdopen(lg->fd, "a");
    if (!lg->fp) {
        close(lg->fd);
        lg->fd = -1;
        return -1;
    }

    setvbuf(lg->fp, NULL, _IONBF, 0);
    return 0;
}

void amds_logger_close(amds_logger_t *lg) {
    pthread_mutex_lock(&lg->lock);
    if (lg->fp) fclose(lg->fp);
    lg->fp = NULL;
    lg->fd = -1;
    pthread_mutex_unlock(&lg->lock);
    pthread_mutex_destroy(&lg->lock);
}

int amds_log_text(amds_logger_t *lg, const char *text) {
    pthread_mutex_lock(&lg->lock);
    if (!lg->fp) {
        pthread_mutex_unlock(&lg->lock);
        return -1;
    }
    fputs(text, lg->fp);
    fputc('\n', lg->fp);
    fflush(lg->fp);
    fsync(fileno(lg->fp));
    pthread_mutex_unlock(&lg->lock);
    return 0;
}

int amds_log_printf(amds_logger_t *lg, const char *fmt, ...) {
    pthread_mutex_lock(&lg->lock);
    if (!lg->fp) {
        pthread_mutex_unlock(&lg->lock);
        return -1;
    }

    char ts[64];
    time_t now = time(NULL);
    struct tm *tm_info = localtime(&now);
    strftime(ts, sizeof(ts), "%Y-%m-%d %H:%M:%S", tm_info);

    fprintf(lg->fp, "[%s] ", ts);

    va_list args;
    va_start(args, fmt);
    vfprintf(lg->fp, fmt, args);
    va_end(args);

    fputc('\n', lg->fp);
    fflush(lg->fp);
    fsync(fileno(lg->fp));

    pthread_mutex_unlock(&lg->lock);
    return 0;
}