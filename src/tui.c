#include "../include/amds.h"
#include <string.h>
#include <stdlib.h>
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
        case AMDS_UI_VRAM:    return "VRAM";
        case AMDS_UI_CORE:    return "CORE";
        case AMDS_UI_FULL:    return "FULL";
        default:              return "?";
    }
}

static void draw_header(WINDOW *w, const amds_tui_state_t *st) {
    int max_x = getmaxx(w);
    werase(w);
    box(w, 0, 0);
    
    wattron(w, A_BOLD);
    mvwprintw(w, 1, 2, "AMDS %s", amds_version_string());
    wattroff(w, A_BOLD);
    
    mvwprintw(w, 1, 20, "Mode: ");
    wattron(w, COLOR_PAIR(1) | A_BOLD);
    wprintw(w, "%-7s", ui_mode_name(st->mode));
    wattroff(w, COLOR_PAIR(1) | A_BOLD);
    
    wprintw(w, "  Running: ");
    if (st->running) {
        wattron(w, COLOR_PAIR(2) | A_BOLD);
        wprintw(w, "YES");
        wattroff(w, COLOR_PAIR(2) | A_BOLD);
    } else {
        wattron(w, COLOR_PAIR(3));
        wprintw(w, "NO ");
        wattroff(w, COLOR_PAIR(3));
    }
    
    wprintw(w, "  OpenCL: ");
    wattron(w, st->ocl_ready ? COLOR_PAIR(2) : COLOR_PAIR(3));
    wprintw(w, "%s", st->ocl_ready ? "READY" : "OFF  ");
    wattroff(w, st->ocl_ready ? COLOR_PAIR(2) : COLOR_PAIR(3));

    const char *help = "q: Quit | Arrows: Navigate | Enter: Select | r: Refresh";
    int help_len = strlen(help);
    if (max_x - help_len - 2 > 65) {
        mvwprintw(w, 1, max_x - help_len - 2, "%s", help);
    }
    
    wrefresh(w);
}

static void draw_monitor(WINDOW *w, amds_gpu_t *gpus, int gpu_count, int selected) {
    werase(w);
    box(w, 0, 0);
    
    wattron(w, A_BOLD | COLOR_PAIR(1));
    mvwprintw(w, 0, 2, " MONITORING [%d GPU%s] ", gpu_count, gpu_count > 1 ? "s" : "");
    wattroff(w, A_BOLD | COLOR_PAIR(1));

    int max_y = getmaxy(w);
    int y = 1;
    
    for (int i = 0; i < gpu_count; i++) {
        amds_gpu_t *g = &gpus[i];
        
        if (y >= max_y - 6) break;

        if (i == selected) {
            wattron(w, COLOR_PAIR(1) | A_BOLD);
            mvwprintw(w, y, 2, ">>");
            wattroff(w, COLOR_PAIR(1) | A_BOLD);
            wattron(w, A_BOLD);
        } else {
            mvwprintw(w, y, 2, "  ");
        }
        
        wprintw(w, " GPU [%d] %s", g->index, g->board_name[0] ? g->board_name : g->drm_name);
        wattroff(w, A_BOLD);
        y++;

        mvwprintw(w, y++, 4, "Driver: %-10s Family: %-12s PCI: %-16s",
                  amds_driver_name(g->driver), amds_family_name(g->family),
                  g->pci_slot[0] ? g->pci_slot : "-");
                  
        mvwprintw(w, y++, 4, "Memory: %-10s Bus: %-11db Chips: %-11d Banks: %-11d",
                  g->memory_type[0] ? g->memory_type : "-", g->memory_bus_bits,
                  g->memory_chips, g->logical_banks);

        mvwprintw(w, y, 4, "Clocks: ");
        wattron(w, COLOR_PAIR(4));
        wprintw(w, "SCLK: %4.0f MHz  MCLK: %4.0f MHz", g->metrics.sclk_mhz, g->metrics.mclk_mhz);
        wattroff(w, COLOR_PAIR(4));
        wprintw(w, "  |  VDDC: ");
        wattron(w, COLOR_PAIR(4));
        wprintw(w, "%4.0f mV", g->metrics.vddc_mv);
        wattroff(w, COLOR_PAIR(4));
        y++;

        mvwprintw(w, y, 4, "Temps:  ");
        wattron(w, g->metrics.temp_edge_c > 75 ? COLOR_PAIR(3) : COLOR_PAIR(2));
        wprintw(w, "Edge: %5.1f C", g->metrics.temp_edge_c);
        wattroff(w, g->metrics.temp_edge_c > 75 ? COLOR_PAIR(3) : COLOR_PAIR(2));
        wprintw(w, "  ");
        wattron(w, g->metrics.temp_hotspot_c > 85 ? COLOR_PAIR(3) : COLOR_PAIR(2));
        wprintw(w, "Hotspot: %5.1f C", g->metrics.temp_hotspot_c);
        wattroff(w, g->metrics.temp_hotspot_c > 85 ? COLOR_PAIR(3) : COLOR_PAIR(2));
        y++;

        mvwprintw(w, y, 4, "Usage:  ");
        wattron(w, COLOR_PAIR(4));
        wprintw(w, "GPU: %3.0f%%  MEM: %3.0f%%", g->metrics.gpu_busy_pct, g->metrics.mem_busy_pct);
        wattroff(w, COLOR_PAIR(4));
        wprintw(w, "      VRAM: ");
        wattron(w, COLOR_PAIR(1));
        wprintw(w, "%.2f", g->metrics.vram_used / 1073741824.0);
        wattroff(w, COLOR_PAIR(1));
        wprintw(w, " / %.2f GiB", g->metrics.vram_total / 1073741824.0);
        y++;

        mvwprintw(w, y, 4, "Power:  ");
        wattron(w, COLOR_PAIR(4));
        wprintw(w, "%5.1f W", g->metrics.power_w);
        wattroff(w, COLOR_PAIR(4));
        wprintw(w, " / %5.1f W", g->metrics.power_cap_w);
        y++;

        mvwprintw(w, y++, 4, "RAS:    ECC: %-3s  CE: %-5llu  UE: %-5llu  Bad Pages: %-5llu",
                  g->ras.available ? "ON" : "OFF",
                  (unsigned long long)g->ras.corrected,
                  (unsigned long long)g->ras.uncorrected,
                  (unsigned long long)g->ras.bad_pages);

        if (i < gpu_count - 1) {
            mvwhline(w, y++, 2, ACS_HLINE, getmaxx(w) - 4);
        }
        y++;
    }

    wrefresh(w);
}

static void draw_menu(WINDOW *w, const amds_tui_state_t *st) {
    const char *items[] = {
        " Mode: Monitor ",
        " Mode: VRAM    ",
        " Mode: Core    ",
        " Mode: Full    ",
        " Start / Stop  ",
        " Refresh Now   ",
        " Exit          "
    };

    werase(w);
    box(w, 0, 0);
    
    wattron(w, A_BOLD | COLOR_PAIR(1));
    mvwprintw(w, 0, 2, " ACTIONS ");
    wattroff(w, A_BOLD | COLOR_PAIR(1));

    int max_x = getmaxx(w);

    for (int i = 0; i < 7; i++) {
        if (i == st->selected_menu) {
            wattron(w, COLOR_PAIR(1) | A_REVERSE | A_BOLD);
            mvwprintw(w, 2 + i * 2, 2, " %-*s", max_x - 5, items[i]);
            wattroff(w, COLOR_PAIR(1) | A_REVERSE | A_BOLD);
        } else {
            mvwprintw(w, 2 + i * 2, 2, "  %-*s", max_x - 5, items[i]);
        }
    }

    wrefresh(w);
}

static void draw_footer(WINDOW *w, amds_gpu_t *g, const amds_tui_state_t *st) {
    werase(w);
    box(w, 0, 0);
    
    wattron(w, A_BOLD | COLOR_PAIR(1));
    mvwprintw(w, 0, 2, " SYSTEM STATUS ");
    wattroff(w, A_BOLD | COLOR_PAIR(1));

    mvwprintw(w, 1, 2, "Active Target: ");
    wattron(w, A_BOLD);
    wprintw(w, "GPU [%d] (%s)", g->index, g->drm_name);
    wattroff(w, A_BOLD);

    mvwprintw(w, 2, 2, "Status:        ");
    if (strstr(st->status, "failed") || strstr(st->status, "failed")) {
        wattron(w, COLOR_PAIR(3) | A_BOLD);
    } else if (strstr(st->status, "finished") || strstr(st->status, "done")) {
        wattron(w, COLOR_PAIR(2) | A_BOLD);
    } else {
        wattron(w, COLOR_PAIR(4));
    }
    wprintw(w, "%s", st->status[0] ? st->status : "idle");
    wattroff(w, COLOR_PAIR(1) | COLOR_PAIR(2) | COLOR_PAIR(3) | COLOR_PAIR(4) | A_BOLD);

    mvwprintw(w, 4, 2, "Decoder Core:  ");
    wattron(w, COLOR_PAIR(4));
    wprintw(w, "Polaris (burst-slot remap) | Other families (heuristic generic mode)");
    wattroff(w, COLOR_PAIR(4));

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
    amds_logger_t *lg = g_amds_logger;
    bool own_lg = false;

    if (!lg) {
        lg = calloc(1, sizeof(amds_logger_t));
        if (amds_logger_init(lg, "./amds_diag.log") < 0) {
            snprintf(st->status, sizeof(st->status), "failed to open log file");
            free(lg);
            return -1;
        }
        own_lg = true;
    }

    if (g_amds_logger) amds_log_printf(g_amds_logger, "[TUI] starting test sequence for GPU%d", g->index);

    if ((st->mode == AMDS_UI_VRAM || st->mode == AMDS_UI_CORE || st->mode == AMDS_UI_FULL) && !st->ocl_ready) {
        if (amds_ocl_init(&st->ocl) == 0) {
            st->ocl_ready = true;
        } else {
            snprintf(st->status, sizeof(st->status), "OpenCL runtime initialization failed");
            if (own_lg) { amds_logger_close(lg); free(lg); }
            return -1;
        }
    }

    amds_poll_metrics(g);
    amds_poll_ras(g);

    if (st->mode == AMDS_UI_MONITOR) {
        log_stage(lg, g, "MONITOR", "manual refresh");
        snprintf(st->status, sizeof(st->status), "monitor refresh done");
    } else if (st->mode == AMDS_UI_VRAM) {
        log_stage(lg, g, "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_MOVING_INV_BEGIN", "");
        amds_vram_test_moving_inversions(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_RANDOM_NOISE_BEGIN", "");
        amds_vram_test_random_noise(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(g, &st->ocl, lg);
        snprintf(st->status, sizeof(st->status), "VRAM tests finished");
    } else if (st->mode == AMDS_UI_CORE) {
        log_stage(lg, g, "CORE_FP32_BEGIN", "");
        amds_core_stress_fp32(g, &st->ocl, lg);
        if (st->ocl.has_fp64) {
            log_stage(lg, g, "CORE_FP64_BEGIN", "");
            amds_core_stress_fp64(g, &st->ocl, lg);
        } else {
            log_stage(lg, g, "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
        snprintf(st->status, sizeof(st->status), "CORE tests finished");
    } else if (st->mode == AMDS_UI_FULL) {
        log_stage(lg, g, "VRAM_PATTERN_BEGIN", "");
        amds_vram_test_pattern(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_WALKING_BEGIN", "");
        amds_vram_test_walking(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_MOVING_INV_BEGIN", "");
        amds_vram_test_moving_inversions(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_RANDOM_NOISE_BEGIN", "");
        amds_vram_test_random_noise(g, &st->ocl, lg);
        log_stage(lg, g, "VRAM_PRNG_BEGIN", "");
        amds_vram_test_prng(g, &st->ocl, lg);
        log_stage(lg, g, "CORE_FP32_BEGIN", "");
        amds_core_stress_fp32(g, &st->ocl, lg);
        if (st->ocl.has_fp64) {
            log_stage(lg, g, "CORE_FP64_BEGIN", "");
            amds_core_stress_fp64(g, &st->ocl, lg);
        } else {
            log_stage(lg, g, "CORE_FP64_SKIP", "cl_khr_fp64 unavailable");
        }
        snprintf(st->status, sizeof(st->status), "FULL test finished");
    }

    amds_poll_metrics(g);
    amds_poll_ras(g);
    
    if (g_amds_logger) amds_log_printf(g_amds_logger, "[TUI] test sequence finished for GPU%d", g->index);
    if (own_lg) { amds_logger_close(lg); free(lg); }
    return 0;
}


static void activate_menu(amds_tui_state_t *st, amds_gpu_t *gpus, int gpu_count) {
    switch (st->selected_menu) {
        case 0:
            st->mode = AMDS_UI_MONITOR;
            snprintf(st->status, sizeof(st->status), "mode set to monitor");
            break;
        case 1:
            st->mode = AMDS_UI_VRAM;
            snprintf(st->status, sizeof(st->status), "mode set to vram testing");
            break;
        case 2:
            st->mode = AMDS_UI_CORE;
            snprintf(st->status, sizeof(st->status), "mode set to core stress");
            break;
        case 3:
            st->mode = AMDS_UI_FULL;
            snprintf(st->status, sizeof(st->status), "mode set to full diagnostics");
            break;
        case 4:
            st->running = !st->running;
            snprintf(st->status, sizeof(st->status), "execution toggled: running=%s", st->running ? "yes" : "no");
            break;
        case 5:
            refresh_all(gpus, gpu_count);
            snprintf(st->status, sizeof(st->status), "manual data refresh complete");
            break;
        case 6:
            st->quit = true;
            break;
        default:
            break;
    }
}

static void init_tui_windows(amds_tui_state_t *st, int rows, int cols) {
    if (st->header) delwin(st->header);
    if (st->monitor) delwin(st->monitor);
    if (st->menu) delwin(st->menu);
    if (st->footer) delwin(st->footer);

    int header_h = 3;
    int footer_h = 6;
    int main_h = rows - header_h - footer_h;
    int menu_w = 24;
    int monitor_w = cols - menu_w;

    st->header = newwin(header_h, cols, 0, 0);
    st->monitor = newwin(main_h, monitor_w, header_h, 0);
    st->menu = newwin(main_h, menu_w, header_h, monitor_w);
    st->footer = newwin(footer_h, cols, header_h + main_h, 0);
}

int amds_run_tui(const amds_config_t *cfg, amds_gpu_t *gpus, int gpu_count) {
    (void)cfg;

    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    nodelay(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        use_default_colors();
        init_pair(1, COLOR_CYAN, -1);
        init_pair(2, COLOR_GREEN, -1);
        init_pair(3, COLOR_RED, -1);
        init_pair(4, COLOR_YELLOW, -1);
    }

    int rows, cols;
    getmaxyx(stdscr, rows, cols);

    amds_tui_state_t st;
    memset(&st, 0, sizeof(st));
    
    init_tui_windows(&st, rows, cols);

    st.selected_gpu = 0;
    st.selected_menu = 0;
    st.mode = AMDS_UI_MONITOR;
    st.quit = false;
    st.running = false;
    st.ocl_ready = false;
    snprintf(st.status, sizeof(st.status), "initialized and ready");

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
            case KEY_RESIZE:
                getmaxyx(stdscr, rows, cols);
                if (rows > 15 && cols > 60) {
                    init_tui_windows(&st, rows, cols);
                }
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