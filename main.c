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

// Varsayılan Ayarlar
#define DEFAULT_INTERVAL 60
#define CAMERA_DEV "/dev/video0"
#define MIN_BRIGHTNESS_PERCENT 5
#define MAX_BRIGHTNESS_PERCENT 100
#define WARMUP_FRAMES 5
#define WIDTH 640
#define HEIGHT 480

// Global Yapılandırma
char backlight_path[256] = {0};
int interval = DEFAULT_INTERVAL;
int verbose = 0;

// Yardımcı: Loglama
void log_msg(const char *format, ...) {
    if (!verbose) return;
    va_list args;
    va_start(args, format);
    vprintf(format, args);
    va_end(args);
    printf("\n");
}

// 1. Sürücü Keşfi (Auto-Discovery)
int find_backlight_driver() {
    DIR *d;
    struct dirent *dir;
    d = opendir("/sys/class/backlight");
    if (!d) return 0;

    while ((dir = readdir(d)) != NULL) {
        if (dir->d_name[0] == '.') continue;
        // İlk bulduğunu al (Genelde intel_backlight veya amdgpu_bl0)
        snprintf(backlight_path, sizeof(backlight_path), "/sys/class/backlight/%s", dir->d_name);
        closedir(d);
        return 1;
    }
    closedir(d);
    return 0;
}

// Dosya Okuma/Yazma Yardımcıları
int read_int(const char *filename) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", backlight_path, filename);
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    int val;
    if (fscanf(f, "%d", &val) != 1) val = -1;
    fclose(f);
    return val;
}

void write_int(const char *filename, int val) {
    char path[512];
    snprintf(path, sizeof(path), "%s/%s", backlight_path, filename);
    FILE *f = fopen(path, "w");
    if (!f) {
        if (verbose) perror("Failed to write brightness");
        return;
    }
    fprintf(f, "%d", val);
    fclose(f);
}

// 2. Kamera İşlemleri (V4L2)
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

    // Warmup + Capture
    for (int i = 0; i <= WARMUP_FRAMES; i++) {
        ioctl(fd, VIDIOC_QBUF, &buf);
        ioctl(fd, VIDIOC_DQBUF, &buf);

        if (i == WARMUP_FRAMES) {
            unsigned char *data = (unsigned char *)buffer_start;
            // YUYV formatında her 2 byte'da bir Y (parlaklık) verisi vardır.
            // Hız için 20 pikselde bir örnek alıyoruz (Subsampling)
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
    printf("  -i <seconds>   Check interval (default: 60)\n");
    printf("  -v             Verbose mode (print logs)\n");
    printf("  -h             Show this help\n");
}

int main(int argc, char *argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "i:vh")) != -1) {
        switch (opt) {
            case 'i': interval = atoi(optarg); break;
            case 'v': verbose = 1; break;
            case 'h': print_usage(argv[0]); return 0;
            default: print_usage(argv[0]); return 1;
        }
    }

    // Sürücü yolunu bul
    if (!find_backlight_driver()) {
        fprintf(stderr, "Error: No backlight driver found in /sys/class/backlight/\n");
        return 1;
    }

    if (verbose) {
        printf("Lumos started.\n");
        printf("Driver: %s\n", backlight_path);
        printf("Interval: %d seconds\n", interval);
    }

    // Ana Döngü
    while (1) {
        int luma = capture_luma();

        if (luma >= 0) {
            int max_b = read_int("max_brightness");
            int cur_b = read_int("brightness");

            if (max_b > 0) {
                // Hesaplama
                double percent = (double)luma / 180.0 * 100.0;
                if (percent < MIN_BRIGHTNESS_PERCENT) percent = MIN_BRIGHTNESS_PERCENT;
                if (percent > MAX_BRIGHTNESS_PERCENT) percent = MAX_BRIGHTNESS_PERCENT;

                int target = (int)((percent / 100.0) * max_b);

                // Histerezis (%5 eşik)
                if (abs(cur_b - target) > (max_b * 0.05)) {
                    log_msg("Ambient: %d -> Target: %d", luma, target);
                    write_int("brightness", target);
                }
            }
        } else {
            log_msg("Warning: Failed to capture from camera.");
        }

        sleep(interval);
    }

    return 0;
}
