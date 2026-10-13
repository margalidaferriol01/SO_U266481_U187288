#include "splitCommand.h"


/*
 * Splits a command line into an argv-style array suitable for execvp().
 * The returned array is NULL-terminated.
 * The input string is modified in place.
 */
char **split_command(char *line)
{
    
    size_t count = 0;
    char *tmp = line;
    char **argv;

    /* Remove trailing newline if present */
    size_t len = strlen(line);
    if (len > 0 && line[len - 1] == '\n') {
        line[len - 1] = '\0';
    }

    /* First pass: count tokens */
    while (*tmp) {
        while (*tmp == ' ')
            tmp++;
        if (*tmp) {
            count++;
            while (*tmp && *tmp != ' ')
                tmp++;
        }
    }

    /* Allocate argument vector (+1 for NULL terminator) */
    argv = malloc((count + 1) * sizeof(char *));
    if (!argv)
        return NULL;

    /* Second pass: split string */
    size_t i = 0;
    char *token = strtok(line, " ");
    while (token) {
        argv[i++] = token;
        token = strtok(NULL, " ");
    }
    argv[i] = NULL;

    return argv;
}

