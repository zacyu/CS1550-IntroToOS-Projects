/*
 * Project 1: Graphics Library
 * CS 1550 - Fall 2017
 * Author: Zac Yu (zhy46@pitt.edu)
 */

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include "graphics.h"

#define CLEAR_SCREEN_SEQ "\033[2J"

typedef enum { false, true } bool;

static bool initialized = false;
static int fb;
static void* fb_mem;
static int fb_size;
static int line_count;
static int line_length;

void init_graphics() {
  struct fb_fix_screeninfo fsinfo;
  struct fb_var_screeninfo vsinfo;
  struct termios tios;

  fb = open("/dev/fb0", O_RDWR);
  if (fb == -1) return;
  if (ioctl(fb, FBIOGET_FSCREENINFO, &fsinfo) == -1) return;
  if (ioctl(fb, FBIOGET_VSCREENINFO, &vsinfo) == -1) return;
  line_count = vsinfo.yres_virtual;
  line_length = fsinfo.line_length / 2;
  fb_size = line_count * line_length * 2;
  fb_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
  if (fb_mem == MAP_FAILED) return;
  write(STDOUT_FILENO, CLEAR_SCREEN_SEQ, 4);
  if (ioctl(STDIN_FILENO, TCGETS, &tios) == -1) return;
  tios.c_lflag &= ~(ICANON | ECHO);
  if (ioctl(STDIN_FILENO, TCSETS, &tios) == -1) return;
  initialized = true;
}

void exit_graphics() {
  struct termios tios;

  if (!initialized) return;
  if (munmap(fb_mem, fb_size) == -1) return;
  if (close(fb) == -1) return;
  if (ioctl(STDIN_FILENO, TCGETS, &tios) == -1) return;
  tios.c_lflag |= (ICANON | ECHO);
  if (ioctl(STDIN_FILENO, TCSETS, &tios) == -1) return;
  initialized = false;
}

char getkey() {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(STDIN_FILENO, &fds);
  char key;
  if (select(STDIN_FILENO + 1, &fds, NULL, NULL, NULL) > 0 &&
      read(STDIN_FILENO, &key, 1) > 0) {
    return key;
  }
  return 0;
}

void sleep_ms(long ms) {
  struct timespec sleep_time = {ms / 1000, (ms % 1000) * 1000000};
  nanosleep(&sleep_time, NULL);
}

void clear_screen(void* img) {
  size_t i;
  for (i = 0; i < fb_size; ++i) {
    *((char*)(img) + i) = 0;
  }
}

void draw_pixel(void* img, int x, int y, color_t color) {
  if (!initialized) return;
  if (x < 0 || y < 0 || x >= line_length || y >= line_count) {
    // Illegal location.
    return;
  }
  int offset = y * line_length + x;
  *((color_t*)(img) + offset) = color;
}

int cmp_to_zero(int n) {
  if (n > 0) return 1;
  if (n < 0) return -1;
  return 0;
}

void draw_line(void* img, int x1, int y1, int x2, int y2, color_t c) {
  if (!initialized) return;
  int x = x1;
  int y = y1;
  int w = x2 - x1;
  int h = y2 - y1;
  int dx1 = cmp_to_zero(w);
  int dy1 = cmp_to_zero(h);
  int dx2 = dx1;
  int dy2 = 0;
  int longest = w > 0 ? w : -w;
  int shortest = h > 0 ? h : -h;
  if (longest <= shortest) {
    int tmp = longest;
    longest = shortest;
    shortest = tmp;
    dy2 = dy1;
    dx2 = 0;
  }
  int numerator = longest >> 1;
  int i;
  for (i = 0; i <= longest; ++i) {
    draw_pixel(img, x, y, c);
    numerator += shortest;
    if (numerator >= longest) {
      numerator -= longest;
      x += dx1;
      y += dy1;
    } else {
      x += dx2;
      y += dy2;
    }
  }
}

void* new_offscreen_buffer() {
  if (!initialized) return NULL;
  void* ob_mem = mmap(NULL, fb_size, PROT_READ | PROT_WRITE,
                MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
  if (ob_mem == MAP_FAILED) return NULL;
  return ob_mem;
}

void blit(void *src) {
  if (!initialized) return;
  size_t i;
  for (i = 0; i < fb_size; ++i) {
    *((char*)(fb_mem) + i) = *((char*)(src) + i);
  }
}
