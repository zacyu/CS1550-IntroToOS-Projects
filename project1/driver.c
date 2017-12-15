/*
 * Project 1: Graphics Library
 * CS 1550 - Fall 2017
 * Author: Zac Yu (zhy46@pitt.edu)
 */

#include <math.h>
#include <stdbool.h>
#include <string.h>
#include <unistd.h>

#include "graphics.h"


int main() {
  const color_t kColors[6] = {
    RGB(29, 0, 0),
    RGB(31, 35, 0),
    RGB(31, 59, 1),
    RGB(0, 32, 5),
    RGB(1, 10, 31),
    RGB(15, 2, 17)
  };
  const int kLineNum = 180;
  const int kSizeAdjustStep = 5;
  const char kWelcomeText[] =
      "\033[1;1HGraphics Library Driver\n"
      "Author: Zac Yu (zhy46@)\n"
      "Controls [w/a/s/d]  Animation [e]  Reset [r]  Quit [q]\n"
      "Press any key to continue...";

  init_graphics();
  write(STDOUT_FILENO, kWelcomeText, strlen(kWelcomeText));
  getkey();
  write(STDOUT_FILENO, "\033[1;1H \033[1;1H", 13);
  void* fb = new_offscreen_buffer();

  bool animation_mode = false;
  int i;
  int radius = 200;
  int o_x = 320;
  int o_y = 240;
  int offset = 0;
  char op;

  while (true) {
    offset = (offset + kLineNum) % kLineNum;
    for (i = 0; i < kLineNum; ++i) {
      double x_end_offset = round((double) radius * sin(2 * M_PI * (double) i /
                                      (double) kLineNum));
      double y_end_offset = round((double) radius * cos(2 * M_PI * (double) i /
                                      (double) kLineNum));
      draw_line(fb, o_x, o_y, o_x + x_end_offset, o_y + y_end_offset,
                kColors[((i + offset) % kLineNum) / (kLineNum / 6)]);
    }
    blit(fb);
    if (animation_mode) {
      if (radius > 1) {
        radius--;
        offset++;
        sleep_ms(50);
      } else {
        animation_mode = false;
        clear_screen(fb);
        radius = 200;
        offset = 0;
        if (getkey() == 'q') break;
      }
    } else {
      op = getkey();
      if (op == 'w') {
        radius += kSizeAdjustStep;
      } else if (op == 's') {
        radius -= kSizeAdjustStep;
      } else if (op == 'a') {
        offset--;
      } else if (op == 'd') {
        offset++;
      } else if (op == 'e') {
        clear_screen(fb);
        if (radius < 50) {
          radius = 50;
        }
        animation_mode = true;
      } else if (op == 'r') {
        radius = 200;
        offset = 0;
      } else if (op == 'q') {
        clear_screen(fb);
        blit(fb);
        break;
      }
      clear_screen(fb);
    }
  }

  exit_graphics();
  return 0;
}
