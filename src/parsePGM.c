#include "parsePGM.h"

#define BUFF_SIZE 1024

/* Read one byte and count it */
static int read_byte(int fd, char *c, int *count)
{
    int r = read(fd, c, 1);
    if (r != 1)
        return -1;
    (*count)++;
    return 0;
}

/* Read next non-whitespace, non-comment character */
static int read_nonspace(int fd, char *out, int *count)
{
    char c;

    while (1) {
        if (read_byte(fd, &c, count) < 0)
            return -1;

        if (isspace((unsigned char)c))
            continue;

        if (c == '#') {
            /* Skip comment line */
            do {
                if (read_byte(fd, &c, count) < 0)
                    return -1;
            } while (c != '\n');
            continue;
        }

        *out = c;
        return 0;
    }
}

/* Read an integer token */
static int read_int(int fd, int *value, int *count)
{
    char c;
    int v = 0;

    if (read_nonspace(fd, &c, count) < 0)
        return -1;

    if (!isdigit((unsigned char)c))
        return -1;

    do {
        v = v * 10 + (c - '0');
        if (read_byte(fd, &c, count) < 0)
            break;
    } while (isdigit((unsigned char)c));

    *value = v;
    return 0;
}


/*
 * Parses a P5 PGM header.
 * Returns header length in bytes on success, -1 on error.
 */
int parse_pgm_header(const char * path, int *width, int *height, int *maxval)
{   
    int fd = open(path, O_RDONLY);
    int bytes = 0;
    char c1, c2;

    if (!width || !height || !maxval)
        return -1;

    /* Magic number */
    if (read_nonspace(fd, &c1, &bytes) < 0)
        return -1;
    if (read_byte(fd, &c2, &bytes) < 0)
        return -1;

    if (c1 != 'P' || c2 != '5')
        return -1;

    /* Header fields */
    if (read_int(fd, width,  &bytes) < 0) return -1;
    if (read_int(fd, height, &bytes) < 0) return -1;
    if (read_int(fd, maxval, &bytes) < 0) return -1;

    /* Validation */
    if (*width <= 0 || *height <= 0 ||
        *maxval <= 0 || *maxval > 65535)
        return -1;

    return bytes;
}
