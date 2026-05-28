#include "../include/amds.h"
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <ncurses.h>

typedef enum {
    AMDS_UI_MONITOR = 0,
    AMDS_UI_VRAM,
    AMDS_UI_CORE,
    AMDS_UI_FULL
} amds_ui_mode_t;

typedef struct {
    WINDOW *header;
    WINDOW *monitor;
    WINDOW *menu;
    WINDOW *footer;

    int selected_gpu;
    int selected_menu;
    bool quit;
    bool running;

    amds_ui_mode_t mode;
    amds_ocl_ctx_t ocl;
    bool ocl_ready;
    char status[256];
} amds_tui_state_t;

static const char *ui_mode_name(amds_ui_mode_t m) {
    switch (m) {
        case AMDS_UI_MONITOR: return "MONITOR";
        case AMDS_UI_VRAM: return "VRAM";
        case AMDS_UI_CORE: return "CORE";
        case AMDS_UI_FULL: return "FULL";
        default: return "?";
    }
}

static void draw_header(WINDOW *w, const amds_tui_state_t *st) {
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 1, 2, "AMDS %s", amds_version_string());
    mvwprintw(w, 1, 18, "Mode=%s Running=%s OpenCL=%s",
              ui_mode_name(st->mode),
              st->running ? "yes" : "no",
              st->ocl_ready ? "ready" : "no");
    mvwprintw(w, 1, 58, "Keys: q quit | arrows move | Enter activate | r refresh");
    wrefresh(w);
}

static void draw_monitor(WINDOW *w, amds_gpu_t *gpus, int gpu_count, int selected) {
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Monitoring ");

    int y = 1;
    for (int i = 0; i < gpu_count; i++) {
        amds_gpu_t *g = &gpus[i];

        if (i == selected) wattron(w, A_REVERSE);
        mvwprintw(w, y++, 2, "GPU%d %s drv=%s family=%s board=%s",
                  g->index, g->drm_name, amds_driver_name(g->driver),
                  amds_family_name(g->family), g->board_name[0] ? g->board_name : "-");
        if (i == selected) wattroff(w, A_REVERSE);

        mvwprintw(w, y++, 4, "PCI=%s DEV=%s SUB=%s MEM=%s BUS=%db CHIPS=%d LOGBANKS=%d",
                  g->pci_slot[0] ? g->pci_slot : "-",
                  g->pci_device_id[0] ? g->pci_device_id : "-",
                  g->subsystem_id[0] ? g->subsystem_id : "-",
                  g->memory_type[0] ? g->memory_type : "-",
                  g->memory_bus_bits,
                  g->memory_chips,
                  g->logical_banks);

        mvwprintw(w, y++, 4, "SCLK=%7.0f MHz  MCLK=%7.0f MHz  VDDC=%7.0f mV  EDGE=%5.1f C  HOT=%5.1f C",
                  g->metrics.sclk_mhz, g->metrics.mclk_mhz, g->metrics.vddc_mv,
                  g->metrics.temp_edge_c, g->metrics.temp_hotspot_c);

        mvwprintw(w, y++, 4, "PWR=%7.1f / %-7.1f W  GPU=%5.0f%%  MEM=%5.0f%%  VRAM=%6.2f / %-6.2f GiB",
                  g->metrics.power_w, g->metrics.power_cap_w,
                  g->metrics.gpu_busy_pct, g->metrics.mem_busy_pct,
                  g->metrics.vram_used / 1073741824.0,
                  g->metrics.vram_total / 1073741824.0);

        mvwprintw(w, y++, 4, "RAS: available=%s CE=%llu UE=%llu BAD_PAGES=%llu",
                  g->ras.available ? "yes" : "no",
                  (unsigned long long)g->ras.corrected,
                  (unsigned long long)g->ras.uncorrected,
                  (unsigned long long)g->ras.bad_pages);

        y++;
        if (y >= getmaxy(w) - 3) break;
    }

    wrefresh(w);
}

static void draw_menu(WINDOW *w, const amds_tui_state_t *st) {
    const char *items[] = {
        "Mode: Monitor",
        "Mode: VRAM",
        "Mode: Core",
        "Mode: Full",
        "Start / Stop",
        "Refresh now",
        "Quit"
    };

    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 0, 2, " Control ");

    for (int i = 0; i < 7; i++) {
        if (i == st->selected_menu) wattron(w, A_REVERSE);
        mvwprintw(w, 1 + i, 2, "%s", items[i]);
        if (i == st->selected_menu) wattroff(w, A_REVERSE);
    }

    wrefresh(w);
}

static void draw_footer(WINDOW *w, amds_gpu_t *g, const amds_tui_state_t *st) {
    werase(w);
    box(w, 0, 0);
    mvwprintw(w, 1, 2, "Selected GPU%d: %s / %s / %s / %s",
              g->index, g->drm_name, amds_driver_name(g->driver),
              amds_family_name(g->family), g->board_name[0] ? g->board_name : "-");
    mvwprintw(w, 2, 2, "Status: %s", st->status[0] ? st->status : "idle");
    mvwprintw(w, 3, 2, "Polaris decoder: burst-slot remap. Other families: logical heuristic decoder.");
    wrefresh(w);
}

static void log_stage(amds_logger_t *lg, amds_gpu_t *g, const char *stage, const char *extra) {
    char line[1024];
    snprintf(line, sizeof(line),
             "[%ld] [%s] [GPU%d %s %s EDGE=%.1f HOT=%.1f PWR=%.1f GPU=%.0f%% MEM=%.0f%%] %s",
             (long)time(NULL),
             stage,
             g->index,
             g->drm_name,
             amds_family_name(g->family),
             g->metrics.temp_edge_c,
             g->metrics.temp_hotspot_c,
             g->metrics.power_w,
             g->metrics.gpu_busy_pct,
             g->metrics.mem_busy_pct,
             extra ? extra : "");
    amds_log_text(lg, line);
}

static void sleep_ms(unsigned ms) {
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;

    while (nanosleep(&ts, &ts) == -1) {
    }
}

static void refresh_all(amds_gpu_t *gpus, int gpu_count) {
    for (int i = 0; i < gpu_count; i++) {
        amds_poll_metrics(&gpus[i]);
        amds_poll_ras(&gpus[i]);
    }
}

static int run_selected_test(amds_tui_state_t *st, amds_gpu_t *g) {
    amds_logger_t lg;
    memset(&lg, 0, sizeof(lg));
    if (amds_logger_init(&lg, "./amds_diag.log") < 0) {
        snprintf(st->status, sizeof(st->status), "failed to open log");
        return -1;
    }

    if ((st->mode == AMDS_UI_VRAM || st->mode == AMDS_UI_CORE || st->mode == AMDS_UI_FULL) && !st->ocl_ready) {
        if (amds_ocl_init(&st->ocl) == 0) {
            st->ocl_ready = true;
        } else {
            snprintf(st->status, sizeof(st->status), "OpenCL init failed");
            amds_logger_close(&lg);
            return -1;
        }
    }

    amds_poll_metrics(g);
    amds_poll_ras(g);

    if (st->mode == AMDS_UI_MONITOR) {
        log_stage(&lg, g, "MONITOR", "manual refresh");
        snprintf(st->status, sizeof(st->status), "monitor refresh done");
    } else if (st->mode == AMDS_UI_VRAM) {
        log_stage(&lg, g, "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(g, &st->ocl, &lg);
        log_stage(&lg, g, "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(g, &st->ocl, &lg);
        log_stage(&lg, g, "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(g, &st->ocl, &lg);
        snprintf(st->status, sizeof(st->status), "VRAM tests finished");
    } else if (st->mode == AMDS_UI_CORE) {
        log_stage(&lg, g, "CORE_FP32_BEGIN", "");
        amds_core_stress_fp32(g, &st->ocl, &lg);
        if (st->ocl.has_fp64) {
            log_stage(&lg, g, "CORE_FP64_BEGIN", "");
            amds_core_stress_fp64(g, &st->ocl, &lg);
        } else {
            log_stage(&lg, g, "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
        snprintf(st->status, sizeof(st->status), "CORE tests finished");
    } else if (st->mode == AMDS_UI_FULL) {
        log_stage(&lg, g, "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(g, &st->ocl, &lg);
        log_stage(&lg, g, "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(g, &st->ocl, &lg);
        log_stage(&lg, g, "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(g, &st->ocl, &lg);
        log_stage(&lg, g, "CORE_FP32_BEGIN", "");
        amds_core_stress_fp32(g, &st->ocl, &lg);
        if (st->ocl.has_fp64) {
            log_stage(&lg, g, "CORE_FP64_BEGIN", "");
            amds_core_stress_fp64(g, &st->ocl, &lg);
        } else {
            log_stage(&lg, g, "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
        snprintf(st->status, sizeof(st->status), "FULL test finished");
    }

    amds_poll_metrics(g);
    amds_poll_ras(g);
    amds_logger_close(&lg);
    return 0;
}

static void activate_menu(amds_tui_state_t *st, amds_gpu_t *gpus, int gpu_count) {
    (void)gpu_count;

    switch (st->selected_menu) {
        case 0:
            st->mode = AMDS_UI_MONITOR;
            snprintf(st->status, sizeof(st->status), "mode set to monitor");
            break;
        case 1:
            st->mode = AMDS_UI_VRAM;
            snprintf(st->status, sizeof(st->status), "mode set to vram");
            break;
        case 2:
            st->mode = AMDS_UI_CORE;
            snprintf(st->status, sizeof(st->status), "mode set to core");
            break;
        case 3:
            st->mode = AMDS_UI_FULL;
            snprintf(st->status, sizeof(st->status), "mode set to full");
            break;
        case 4:
            st->running = !st->running;
            snprintf(st->status, sizeof(st->status), "running=%s", st->running ? "yes" : "no");
            break;
        case 5:
            refresh_all(gpus, gpu_count);
            snprintf(st->status, sizeof(st->status), "manual refresh done");
            break;
        case 6:
            st->quit = true;
            break;
        default:
            break;
    }
}

int amds_run_tui(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    (void)cfg;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    amds_tui_state_t st;
    memset(&st, 0, sizeof(st));
    st.header = newwin(3, cols, 0, 0);
    st.monitor = newwin(rows - 12, cols - 28, 3, 0);
    st.menu = newwin(rows - 12, 28, 3, cols - 28);
    st.footer = newwin(9, cols, rows - 9, 0);
    st.selected_gpu = 0;
    st.selected_menu = 0;
    st.mode = AMDS_UI_MONITOR;
    st.quit = false;
    st.running = false;
    st.ocl_ready = false;
    snprintf(st.status, sizeof(st.status), "ready");

    time_t last_poll = 0;

    while (!st.quit) {
        int ch = getch();
        switch (ch) {
            case 'q':
            case 'Q':
                st.quit = true;
                break;
            case KEY_UP:
                if (st.selected_menu > 0) st.selected_menu--;
                break;
            case KEY_DOWN:
                if (st.selected_menu < 6) st.selected_menu++;
                break;
            case KEY_LEFT:
                if (st.selected_gpu > 0) st.selected_gpu--;
                break;
            case KEY_RIGHT:
                if (st.selected_gpu < gpu_count - 1) st.selected_gpu++;
                break;
            case '\n':
            case '\r':
            case KEY_ENTER:
                activate_menu(&st, gpus, gpu_count);
                break;
            case 'r':
            case 'R':
                refresh_all(gpus, gpu_count);
                snprintf(st.status, sizeof(st.status), "manual refresh done");
                break;
            default:
                break;
        }

        time_t now = time(NULL);
        if (now != last_poll) {
            last_poll = now;
            refresh_all(gpus, gpu_count);
        }

        if (st.running) {
            run_selected_test(&st, &gpus[st.selected_gpu]);
            st.running = false;
        }

        draw_header(st.header, &st);
        draw_monitor(st.monitor, gpus, gpu_count, st.selected_gpu);
        draw_menu(st.menu, &st);
        draw_footer(st.footer, &gpus[st.selected_gpu], &st);

        sleep_ms(50);
    }

    if (st.ocl_ready) amds_ocl_close(&st.ocl);

    delwin(st.header);
    delwin(st.monitor);
    delwin(st.menu);
    delwin(st.footer);
    endwin();
    return 0;
}