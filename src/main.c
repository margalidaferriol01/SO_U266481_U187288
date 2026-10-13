#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "splitCommand.h"
#include "circularBuffer.h"

#define MAX_LINE 256

void executar_single(char *cmd_line) {
    char **args = split_command(cmd_line);
    if (args == NULL || args[0] == NULL) {
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    } else if (pid > 0) {
        waitpid(pid, NULL, 0);
    } else {
        perror("Error en fork");
    }
}

void executar_concurrent(char *cmd_line) {
    char **args = split_command(cmd_line);
    if (args == NULL || args[0] == NULL) {
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    } else if (pid < 0) {
        perror("Error en fork");
    }
}

int main(void) {
    char mode[MAX_LINE];
    char cmd[MAX_LINE];

    while (1) {
        if (fgets(mode, sizeof(mode), stdin) == NULL) {
            break;
        }

        mode[strcspn(mode, "\r\n")] = 0;

        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        if (strcmp(mode, "SINGLE") == 0) {
            if (fgets(cmd, sizeof(cmd), stdin) != NULL) {
                cmd[strcspn(cmd, "\r\n")] = 0;
                executar_single(cmd);
            }
        } else if (strcmp(mode, "CONCURRENT") == 0) {
            if (fgets(cmd, sizeof(cmd), stdin) != NULL) {
                cmd[strcspn(cmd, "\r\n")] = 0;
                executar_concurrent(cmd);
            }
        } else if (strcmp(mode, "PIPED") == 0) {
            /* Part de la Núria */
            (void)0;
        }
    }

    return 0;
}