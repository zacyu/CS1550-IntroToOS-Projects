/*
 * Project 2: Syscalls
 * CS 1550 - Fall 2017
 * Author: Zac Yu (zhy46@pitt.edu)
 */

#include <linux/unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>

#define MAKE_SEM(sem, val) struct cs1550_sem *sem = \
          mmap(NULL, sizeof(struct cs1550_sem), PROT_READ | PROT_WRITE,\
               MAP_SHARED | MAP_ANONYMOUS, 0, 0);\
          sem->value = val, sem->pl_size = sem->pl_head = 0, sem->pl = NULL
#define SEM_DOWN(sem) syscall(__NR_cs1550_down, sem)
#define SEM_UP(sem) syscall(__NR_cs1550_up, sem)
#define ASSERT_POSITIVITY(val) if (val < 1) {\
          fprintf(stderr, "Argument %s must be a positive integer.\n", #val);\
          return EXIT_FAILURE;\
        }


struct cs1550_sem {
  int value;
  int pl_size;
  int pl_head;
  void **pl;
};

/**
 * Construct the corresponding string index of an non-negative integer with the
 * following pattern (similar to the column name rule of Microsoft Excel):
 * 0 - A, 1 - B, ..., 25 - Z, 26 - AA, 27 - AB, ...
 */
char *get_alphabetical_index(const unsigned val) {
  char *str;
  int digit = 1;
  int i;
  unsigned curr = val;
  unsigned long pow = 26;
  while (curr >= pow) {
    curr -= pow;
    pow *= 26;
    digit++;
  }
  str = (char *)mmap(NULL, digit + 1, PROT_READ | PROT_WRITE,
                     MAP_SHARED | MAP_ANONYMOUS, 0, 0);
  for (i = 0; i < digit; ++i) {
    *(str + digit - i - 1) = 'A' + (curr % 26);
    curr /= 26;
  }
  *(str + digit) = '\0';
  return str;
}

int main(int argc, char *argv[]) {
  int consumer_num, producer_num;  // Command-line arguments.
  int buffer_size;
  unsigned int *buffer_ptr;  // Shared unsigned integer buffer.
  unsigned i;
  setbuf(stdout, NULL);  // Disable standard output buffering.
  // Check if arguments are valid.
  if (argc != 4) {
    fprintf(stderr, "Usage: prodcons consumer_num producer_num buffer_size\n");
    return EXIT_FAILURE;
  }
  consumer_num = atoi(argv[1]);
  producer_num = atoi(argv[2]);
  buffer_size = atoi(argv[3]);
  ASSERT_POSITIVITY(consumer_num);
  ASSERT_POSITIVITY(producer_num);
  ASSERT_POSITIVITY(buffer_size);
  buffer_ptr = mmap(NULL, (buffer_size + 3) * sizeof(unsigned int), PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_ANONYMOUS, 0, 0);
  // Declare and initialize semaphores in shared memory.
  MAKE_SEM(empty, buffer_size);
  MAKE_SEM(full, 0);
  MAKE_SEM(mutex, 1);
  unsigned int *consumer_buffer_idx = buffer_ptr + buffer_size;
  unsigned int *producer_buffer_idx = buffer_ptr + buffer_size + 1;
  unsigned int *next_pancake_idx = buffer_ptr + buffer_size + 2;
  *consumer_buffer_idx = *producer_buffer_idx = *next_pancake_idx = 0;
  // Start consumers.
  for (i = 0; i < consumer_num; ++i) {
    char *customer_id = get_alphabetical_index(i);
    if (fork() == 0) {  // Child process.
      while (1) {
        SEM_DOWN(full);
        SEM_DOWN(mutex);
        printf("Customer %s Consumed: Pancake%u\n", customer_id,
               *(buffer_ptr + *consumer_buffer_idx));
        *consumer_buffer_idx = (*consumer_buffer_idx + 1) % buffer_size;
        SEM_UP(mutex);
        SEM_UP(empty);
      }
    }
  }
  // Start producers.
  for (i = 0; i < producer_num; ++i) {
    char *chef_id = get_alphabetical_index(i);
    if (fork() == 0) {  // Child process.
      while (1) {
        SEM_DOWN(empty);
        SEM_DOWN(mutex);
        *(buffer_ptr + *producer_buffer_idx) = (*next_pancake_idx)++;
        printf("Chef %s Produced: Pancake%u\n", chef_id,
               *(buffer_ptr + *producer_buffer_idx));
        *producer_buffer_idx = (*producer_buffer_idx + 1) % buffer_size;
        SEM_UP(mutex);
        SEM_UP(full);
      }
    }
  }
  // Block main process.
  fprintf(stderr, "Child process %i terminated unexpectedly.\n", wait(NULL));
  return EXIT_FAILURE;
}
