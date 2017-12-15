#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int main() {
  struct termios tios;
  if (ioctl(STDIN_FILENO, TCGETS, &tios) == -1) return 1;
  tios.c_lflag |= (ICANON | ECHO);
  if (ioctl(STDIN_FILENO, TCSETS, &tios) == -1) return 1;
  return 0;
}

