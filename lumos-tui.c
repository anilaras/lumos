#include <ncurses.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/socket.h>
#include <sys/un.h>

#define SOCKET_PATH "/run/lumos.sock"

typedef struct {
    char key[32];
    char label[32];
    double value;
    double min;
    double max;
    double step;
    int type; // 0=float, 1=int, 2=mode(0=auto/1=manual), 3=webcam(index)
} Parameter;

Parameter params[] = {
    {"mode", "Mode (0=A,1=M)", 0, 0, 1, 1, 2},
    {"manual_brightness", "Manual Brightness", 50, 0, 100, 5, 1},
    {"sensitivity", "Sensitivity", 1.0, 0.1, 5.0, 0.1, 0},
    {"brightness_offset", "Offset", 0, -50, 50, 1, 1},
    {"min_brightness", "Min Brightness", 5, 0, 100, 1, 1},
    {"max_brightness", "Max Brightness", 100, 0, 100, 1, 1},
    {"interval", "Interval (s)", 60, 1, 3600, 5, 1},
    {"camera_dev", "Webcam", 0, 0, 9, 1, 3}
};
int param_count = 8;

// IPC Helper
void send_cmd(const char *cmd, char *resp, size_t resp_len) {
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd == -1) {
        snprintf(resp, resp_len, "ERR Socket");
        return;
    }
    
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);
    
    if (connect(fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        snprintf(resp, resp_len, "ERR Connect");
        close(fd);
        return;
    }
    
    write(fd, cmd, strlen(cmd));
    int n = read(fd, resp, resp_len-1);
    if (n >= 0) resp[n] = '\0';
    else resp[0] = '\0';
    
    close(fd);
}

void load_values() {
    char resp[64];
    for (int i=0; i<param_count; i++) {
        char cmd[64];
        snprintf(cmd, sizeof(cmd), "GET %s", params[i].key);
        send_cmd(cmd, resp, sizeof(resp));
        
        if (strncmp(resp, "ERR", 3) != 0) {
            if (params[i].type == 2) { // Mode
                if (strstr(resp, "manual")) params[i].value = 1;
                else params[i].value = 0;
            } else if (params[i].type == 3) { // Webcam
                int devId = 0;
                sscanf(resp, "/dev/video%d", &devId);
                params[i].value = devId;
            } else {
                params[i].value = atof(resp);
            }
        }
    }
}

void save_value(int idx) {
    char cmd[64];
    if (params[idx].type == 2) {
        snprintf(cmd, sizeof(cmd), "SET %s %d", params[idx].key, (int)params[idx].value);
    }
    else if (params[idx].type == 3) {
        snprintf(cmd, sizeof(cmd), "SET %s /dev/video%d", params[idx].key, (int)params[idx].value);
    }
    else if (params[idx].type == 1) {
        snprintf(cmd, sizeof(cmd), "SET %s %d", params[idx].key, (int)params[idx].value);
        // If setting manual brightness, ensure we switch mode too? The daemon handles this but let's be explicit
        if (strcmp(params[idx].key, "manual_brightness") == 0) {
             // Daemon handles mode switch automatically on 'SET brightness' or 'SET manual_brightness'
        }
    }
    else {
        snprintf(cmd, sizeof(cmd), "SET %s %.2f", params[idx].key, params[idx].value);
    }
        
    char resp[64];
    send_cmd(cmd, resp, sizeof(resp));
}

void persist() {
    char resp[64];
    send_cmd("PERSIST", resp, sizeof(resp));
}

int main() {
    initscr();
    cbreak();
    noecho();
    keypad(stdscr, TRUE);
    curs_set(0);

    if (has_colors()) {
        start_color();
        init_pair(1, COLOR_CYAN, COLOR_BLACK);
        init_pair(2, COLOR_BLACK, COLOR_CYAN); // Highlight
        init_pair(3, COLOR_GREEN, COLOR_BLACK);
        init_pair(4, COLOR_YELLOW, COLOR_BLACK);
    }

    load_values();

    int selection = 0;
    int ch;

    while(1) {
        clear();
        attron(COLOR_PAIR(1) | A_BOLD);
        mvprintw(1, 2, "Lumos TUI Control");
        attroff(COLOR_PAIR(1) | A_BOLD);

        mvprintw(3, 2, "Use UP/DOWN to select, LEFT/RIGHT to adjust");
        mvprintw(4, 2, "'S' to Save (Persist), 'Q' to Quit");

        for (int i=0; i<param_count; i++) {
            if (i == selection) attron(COLOR_PAIR(2));
            
            char val_str[36];
            if (params[i].type == 2) {
                snprintf(val_str, sizeof(val_str), "%s", ((int)params[i].value == 1) ? "MANUAL" : "AUTO");
            }
            else if (params[i].type == 3) {
                snprintf(val_str, sizeof(val_str), "/dev/video%d", (int)params[i].value);
            }
            else if (params[i].type == 1) {
                snprintf(val_str, sizeof(val_str), "%d", (int)params[i].value);
            }
            else {
                snprintf(val_str, sizeof(val_str), "%.2f", params[i].value);
            }

            mvprintw(6+i, 4, "%-22s : %s", params[i].label, val_str);
            
            if (i == selection) attroff(COLOR_PAIR(2));
        }

        refresh();

        ch = getch();
        if (ch == 'q' || ch == 'Q') break;
        else if (ch == KEY_UP) {
            selection--;
            if (selection < 0) selection = param_count - 1;
        }
        else if (ch == KEY_DOWN) {
            selection++;
            if (selection >= param_count) selection = 0;
        }
        else if (ch == KEY_LEFT) {
            params[selection].value -= params[selection].step;
            if (params[selection].value < params[selection].min) 
                params[selection].value = params[selection].min;
            save_value(selection); // Instant update
            
            // If we changed manual brightness, mode might have switched to manual
            if (strcmp(params[selection].key, "manual_brightness") == 0) {
                 params[0].value = 1; // Hack: Force UI to show Manual
            }
        }
        else if (ch == KEY_RIGHT) {
            params[selection].value += params[selection].step;
            if (params[selection].value > params[selection].max) 
                params[selection].value = params[selection].max;
            save_value(selection); // Instant update
            
            if (strcmp(params[selection].key, "manual_brightness") == 0) {
                 params[0].value = 1; // Hack: Force UI to show Manual
            }
        }
        else if (ch == 's' || ch == 'S' || ch == 10) { // Enter or S
            persist();
            attron(COLOR_PAIR(3));
            mvprintw(16, 2, "Configuration Saved!");
            attroff(COLOR_PAIR(3));
            refresh();
            napms(1000);
        }
    }

    endwin();
    return 0;
}
