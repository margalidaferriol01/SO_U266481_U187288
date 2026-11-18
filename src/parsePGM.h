#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

int parse_pgm_header(const char * path, int *width, int *height, int *maxval);
