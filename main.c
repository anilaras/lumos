/*
 * Lumos: Intelligent Auto-Brightness for Linux
 * Author: Anıl Aras
 * License: MIT
 */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <linux/videodev2.h>
#include <errno.h>
#include <dirent.h>
#include <getopt.h>
#include <stdarg.h> 
#include <sys/socket.h>
#include <sys/un.h>
#include <pthread.h>
#include <sys/stat.h>

#define SOCKET_PATH "/run/lumos.sock"


#define DEFAULT_INTERVAL 60
#define CAMERA_DEV "/dev/video0"
#define MIN_BRIGHTNESS_PERCENT 5
#define MAX_BRIGHTNESS_PERCENT 100
#define WARMUP_FRAMES 5
#define WIDTH 640
#define HEIGHT 480

char backlight_path[512] = {0};


typedef struct {
    int min_brightness;
    int max_brightness;
    int interval;
    int brightness_offset;
    float sensitivity;
    int mode; // 0=Auto, 1=Manual
    int manual_brightness;
} Config;

Config config = {
    .min_brightness = MIN_BRIGHTNESS_PERCENT,
    .max_brightness = MAX_BRIGHTNESS_PERCENT,
    .interval = DEFAULT_INTERVAL,
    .brightness_offset = 0,
    .sensitivity = 1.0f,
    .mode = 0,
    .manual_brightness = 50
};

// Global config path for persistence
char *g_config_path = "/etc/lumos.conf";
int verbose = 0;

// Synchronization for instant updates
pthread_cond_t wake_cond = PTHREAD_COND_INITIALIZER;
pthread_mutex_t wake_mutex = PTHREAD_MUTEX_INITIALIZER;


void load_config(const char *config_path) {
    FILE *f = fopen(config_path, "r");
    if (!f) {
        if (verbose) printf("Config file not found: %s (using defaults)\n", config_path);
        return;
    }

    char line[256];
    while (fgets(line, sizeof(line), f)) {
        if (line[0] == '#' || line[0] == '\n') continue;

        char key[128], val_str[128];
        if (sscanf(line, "%127[^=]=%127s", key, val_str) == 2) {
            if (strcmp(key, "min_brightness") == 0) {
                int val = atoi(val_str);
                if (val >= 0 && val <= 100) config.min_brightness = val;
            } else if (strcmp(key, "max_brightness") == 0) {
                int val = atoi(val_str);
                if (val >= 0 && val <= 100) config.max_brightness = val;
            } else if (strcmp(key, "interval") == 0) {
                int val = atoi(val_str);
                if (val > 0) config.interval = val;
            } else if (strcmp(key, "brightness_offset") == 0) {
                config.brightness_offset = atoi(val_str);
            } else if (strcmp(key, "sensitivity") == 0) {
                float val = atof(val_str);
                if (val > 0.0) config.sensitivity = val;
            } else if (strcmp(key, "mode") == 0) {
                if (strcmp(val_str, "manual") == 0 || strcmp(val_str, "1") == 0) config.mode = 1;
                else config.mode = 0;
            } else if (strcmp(key, "manual_brightness") == 0) {
                config.manual_brightness = atoi(val_str);
            }
        }
    }
    fclose(f);
    
    if (config.min_brightness >= config.max_brightness) {
        config.min_brightness = 5;
        config.max_brightness = 100;
        if (verbose) printf("Invalid range in config, reverting to defaults.\n");
    }
}



void save_config() {
    FILE *f = fopen(g_config_path, "w");
    if (!f) {
        if (verbose) perror("Failed to save config");
        return;
    }

    fprintf(f, "# Lumos Configuration File\n\n");
    fprintf(f, "# Minimum brightness percentage (0-100)\n");
    fprintf(f, "min_brightness=%d\n\n", config.min_brightness);
    fprintf(f, "# Maximum brightness percentage (0-100)\n");
    fprintf(f, "max_brightness=%d\n\n", config.max_brightness);
    fprintf(f, "# Update interval in seconds\n");
    fprintf(f, "interval=%d\n\n", config.interval);
    fprintf(f, "# Brightness Offset (Default: 0)\n");
    fprintf(f, "brightness_offset=%d\n\n", config.brightness_offset);
    fprintf(f, "# Brightness Sensitivity (Default: 1.0)\n");
    fprintf(f, "sensitivity=%.2f\n\n", config.sensitivity);
    fprintf(f, "# Mode (auto/manual)\n");
    fprintf(f, "mode=%s\n\n", config.mode ? "manual" : "auto");
    fprintf(f, "# Manual Brightness Value (0-100)\n");
    fprintf(f, "manual_brightness=%d\n", config.manual_brightness);

    fclose(f);
    if (verbose) printf("Configuration saved to %s\n", g_config_path);
}

void handle_client(int client_fd) {
    char buffer[256];
    int n = read(client_fd, buffer, sizeof(buffer) - 1);
    if (n <= 0) return;
    buffer[n] = '\0';

    char cmd[32], key[64], val[64];
    int args = sscanf(buffer, "%31s %63s %63s", cmd, key, val);

    char response[256] = "OK\n";

    if (strcmp(cmd, "GET") == 0 && args >= 2) {
        if (strcmp(key, "min_brightness") == 0) sprintf(response, "%d\n", config.min_brightness);
        else if (strcmp(key, "max_brightness") == 0) sprintf(response, "%d\n", config.max_brightness);
        else if (strcmp(key, "interval") == 0) sprintf(response, "%d\n", config.interval);
        else if (strcmp(key, "brightness_offset") == 0) sprintf(response, "%d\n", config.brightness_offset);
        else if (strcmp(key, "sensitivity") == 0) sprintf(response, "%.2f\n", config.sensitivity);
        else if (strcmp(key, "mode") == 0) sprintf(response, "%s\n", config.mode ? "manual" : "auto");
        else if (strcmp(key, "manual_brightness") == 0) sprintf(response, "%d\n", config.manual_brightness);
        else strcpy(response, "ERR Unknown key\n");
    } 
    else if (strcmp(cmd, "SET") == 0 && args >= 3) {
        if (strcmp(key, "min_brightness") == 0) config.min_brightness = atoi(val);
        else if (strcmp(key, "max_brightness") == 0) config.max_brightness = atoi(val);
        else if (strcmp(key, "interval") == 0) config.interval = atoi(val);
        else if (strcmp(key, "brightness_offset") == 0) config.brightness_offset = atoi(val);
        else if (strcmp(key, "sensitivity") == 0) config.sensitivity = atof(val);
        else if (strcmp(key, "mode") == 0) {
             if (strcmp(val, "manual") == 0 || strcmp(val, "1") == 0) config.mode = 1;
             else config.mode = 0;
        }
        else if (strcmp(key, "brightness") == 0 || strcmp(key, "manual_brightness") == 0) {
            config.manual_brightness = atoi(val);
            config.mode = 1; // Auto-switch to manual
        }
        else strcpy(response, "ERR Unknown key\n");
        
        // Signal main thread to update brightness immediately
        pthread_mutex_lock(&wake_mutex);
        pthread_cond_signal(&wake_cond);
        pthread_mutex_unlock(&wake_mutex);
    } 
    else if (strcmp(cmd, "PERSIST") == 0) {
        save_config();
        strcpy(response, "SAVED\n");
    } 
    else {
        strcpy(response, "ERR Invalid command\n");
    }

    write(client_fd, response, strlen(response));
    close(client_fd);
}

void *socket_thread(void *arg) {
    int server_fd, client_fd;
    struct sockaddr_un addr;

    unlink(SOCKET_PATH);
    if ((server_fd = socket(AF_UNIX, SOCK_STREAM, 0)) == -1) {
        perror("Socket error");
        return NULL;
    }

    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof(addr.sun_path)-1);

    if (bind(server_fd, (struct sockaddr*)&addr, sizeof(addr)) == -1) {
        perror("Bind error");
        return NULL;
    }

    // Allow all users to access the socket (so the applet can talk to us)
    chmod(SOCKET_PATH, 0666);

    if (listen(server_fd, 5) == -1) {
        perror("Listen error");
        return NULL;
    }

    while (1) {
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd == -1) continue;
        handle_client(client_fd);
    }
}

void log_msg(const char *format, ...) {
    if (!verbose) return;
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}


int find_backlight_driver() {
    DIR *d;
    struct dirent *dir;
    d = opendir("/sys/class/backlight");
    if (!d) return 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        snprintf(backlight_path, sizeof(backlight_path), "/sys/class/backlight/%s", dir->d_name);
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

int read_int(const char *filename) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", backlight_path, filename);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

void write_int(const char *filename, int val) {
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s", backlight_path, filename);
    FILE *f = fopen(path, "w");
    if (!f) {
        if (verbose) perror("Failed to write brightness");
        return;
    }
    fprintf(f, "%d", val);
    fclose(f);
}

int capture_luma() {
    int fd = open(CAMERA_DEV, O_RDWR);
    if (fd < 0) {
        if (verbose) perror("Camera open failed");
        return -1;
    }

    struct v4l2_format fmt = {0};
    fmt.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    fmt.fmt.pix.width = WIDTH;
    fmt.fmt.pix.height = HEIGHT;
    fmt.fmt.pix.pixelformat = V4L2_PIX_FMT_YUYV;
    
    if (ioctl(fd, VIDIOC_S_FMT, &fmt) == -1) {
        close(fd); return -1;
    }

    struct v4l2_requestbuffers req = {0};
    req.count = 1;
    req.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    req.memory = V4L2_MEMORY_MMAP;
    ioctl(fd, VIDIOC_REQBUFS, &req);

    struct v4l2_buffer buf = {0};
    buf.type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    buf.memory = V4L2_MEMORY_MMAP;
    buf.index = 0;
    ioctl(fd, VIDIOC_QUERYBUF, &buf);

    void *buffer_start = mmap(NULL, buf.length, PROT_READ | PROT_WRITE, MAP_SHARED, fd, buf.m.offset);
    if (buffer_start == MAP_FAILED) { close(fd); return -1; }

    enum v4l2_buf_type type = V4L2_BUF_TYPE_VIDEO_CAPTURE;
    ioctl(fd, VIDIOC_STREAMON, &type);

    long total_y = 0;
    int pixel_count = 0;

    for (int i = 0; i <= WARMUP_FRAMES; i++) {
        ioctl(fd, VIDIOC_QBUF, &buf);
        ioctl(fd, VIDIOC_DQBUF, &buf);
        
        if (i == WARMUP_FRAMES) {
            unsigned char *data = (unsigned char *)buffer_start;
            for (unsigned int j = 0; j < buf.bytesused; j += 20) {
                total_y += data[j];
                pixel_count++;
            }
        }
    }
    
    ioctl(fd, VIDIOC_STREAMOFF, &type);
    munmap(buffer_start, buf.length);
    close(fd);

    return (pixel_count == 0) ? 0 : (int)(total_y / pixel_count);
}

void print_usage(char *prog_name) {
    printf("Usage: %s [OPTIONS]\n", prog_name);
    printf("Options:\n");
    printf("  -c <path>      Path to config file (default: /etc/lumos.conf)\n");
    printf("  -i <seconds>   Check interval (overrides config)\n");
    printf("  -v             Verbose mode (print logs)\n");
    printf("  -h             Show this help\n");
}

int main(int argc, char *argv[]) {
    char *config_path = "/etc/lumos.conf";
    int interval_override = -1;
    int opt;

    for (int i=1; i<argc; i++) {
        if (strcmp(argv[i], "-c") == 0 && i+1 < argc) {
            config_path = argv[i+1];
        }
    }
    g_config_path = config_path; // Store for persistence handling

    load_config(config_path);

    optind = 1; 
    while ((opt = getopt(argc, argv, "c:i:vh")) != -1) {
        switch (opt) {
            case 'c': /* Already handled above */ break;
            case 'i': interval_override = atoi(optarg); break;
            case 'v': verbose = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    if (interval_override > 0) config.interval = interval_override;

    if (!find_backlight_driver()) {
        fprintf(stderr, "Error: No backlight driver found in /sys/class/backlight/\n");
        return 1;
    }
    
    if (verbose) {
        printf("Lumos started.\n");
        printf("Driver: %s\n", backlight_path);
        printf("Config: %s\n", config_path);
        printf("Interval: %d seconds\n", config.interval);
        printf("Range: %d%% - %d%%\n", config.min_brightness, config.max_brightness);
    }

    // Start IPC thread
    pthread_t tid;
    pthread_create(&tid, NULL, socket_thread, NULL);
    pthread_detach(tid);

    while (1) {
        if (config.mode == 1) {
            // MANUAL MODE
            int max_b = read_int("max_brightness");
            int cur_b = read_int("brightness");
            if (max_b > 0) {
                 int target = (int)((config.manual_brightness / 100.0) * max_b);
                 if (abs(cur_b - target) > (max_b * 0.01)) { // Tighter tolerance for manual
                    if (verbose) printf("Manual: %d%%\n", config.manual_brightness);
                    write_int("brightness", target);
                 }
            }
        } else {
            // AUTO MODE
            int luma = capture_luma();
            
            if (luma >= 0) {
                int max_b = read_int("max_brightness");
                int cur_b = read_int("brightness");
    
                if (max_b > 0) {
                    double percent = (double)luma / 180.0 * 100.0;                
                    percent *= config.sensitivity;
                    percent += config.brightness_offset;
                    if (percent < config.min_brightness) percent = config.min_brightness;
                    if (percent > config.max_brightness) percent = config.max_brightness;
    
                    int target = (int)((percent / 100.0) * max_b);
    
                    if (abs(cur_b - target) > (max_b * 0.05)) {
                        log_msg("Ambient: %d -> Target: %d", luma, target);
                        write_int("brightness", target);
                    }
                }
            } else {
                log_msg("Warning: Failed to capture from camera.");
            }
        }

        // Wait for interval OR wake signal
        struct timespec ts;
        clock_gettime(CLOCK_REALTIME, &ts);
        ts.tv_sec += config.interval;
        
        pthread_mutex_lock(&wake_mutex);
        pthread_cond_timedwait(&wake_cond, &wake_mutex, &ts);
        pthread_mutex_unlock(&wake_mutex);
    }

    return 0;
}