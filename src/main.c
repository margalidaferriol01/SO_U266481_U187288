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
        if (args != NULL) free(args);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    } 
    else if (pid > 0) {
        waitpid(pid, NULL, 0);
        free(args); 
    } 
    else {
        perror("Error en fork");
        free(args);
    }
}

void executar_concurrent(char *cmd_line) {
    char **args = split_command(cmd_line);
    if (args == NULL || args[0] == NULL) {
        if (args != NULL) free(args);
        return;
    }

    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    } 
    else if (pid > 0) {
        free(args);
    } 
    else {
        perror("Error en fork");
        free(args);
    }
}

void executar_piped(char *cmd1_line, char *cmd2_line) {
    char **args1 = split_command(cmd1_line);
    char **args2 = split_command(cmd2_line);

    if (args1 == NULL || args1[0] == NULL || args2 == NULL || args2[0] == NULL) {
        if (args1 != NULL) free(args1);
        if (args2 != NULL) free(args2);
        return;
    }

    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Error en pipe");
        free(args1);
        free(args2);
        return;
    }

    // --- Primer fill  ---
    pid_t pid1 = fork();
    if (pid1 == 0) {
        dup2(pipefd[1], STDOUT_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(args1[0], args1);
        perror("Error en execvp (cmd1)");
        exit(EXIT_FAILURE);
    } 
    else if (pid1 < 0) {
        perror("Error en el primer fork");
        free(args1);
        free(args2);
        return;
    }

    // --- Segon fill ---
    pid_t pid2 = fork();
    if (pid2 == 0) {
        dup2(pipefd[0], STDIN_FILENO);
        close(pipefd[0]);
        close(pipefd[1]);
        execvp(args2[0], args2);
        perror("Error en execvp (cmd2)");
        exit(EXIT_FAILURE);
    } 
    else if (pid2 < 0) {
        perror("Error en el segon fork");
    }

    // --- Procés pare ---
  
    close(pipefd[0]);
    close(pipefd[1]);

    // Esperem que acabin els dos processos
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    // Alliberem la memòria dels arguments
    free(args1);
    free(args2);
}

int main(void) {
    char mode[MAX_LINE];
    char cmd1[MAX_LINE];
    char cmd2[MAX_LINE];

    while (1) {
        if (fgets(mode, sizeof(mode), stdin) == NULL) {
            break;
        }
        mode[strcspn(mode, "\r\n")] = 0;

        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        if (strcmp(mode, "SINGLE") == 0) {
            if (fgets(cmd1, sizeof(cmd1), stdin) != NULL) {
                cmd1[strcspn(cmd1, "\r\n")] = 0;
                executar_single(cmd1);
            }
        } 
        else if (strcmp(mode, "CONCURRENT") == 0) {
            if (fgets(cmd1, sizeof(cmd1), stdin) != NULL) {
                cmd1[strcspn(cmd1, "\r\n")] = 0;
                executar_concurrent(cmd1);
            }
        } 
        else if (strcmp(mode, "PIPED") == 0) {
            if (fgets(cmd1, sizeof(cmd1), stdin) != NULL) {
                cmd1[strcspn(cmd1, "\r\n")] = 0;
                
                if (fgets(cmd2, sizeof(cmd2), stdin) != NULL) {
                    cmd2[strcspn(cmd2, "\r\n")] = 0;
                    executar_piped(cmd1, cmd2);
                }
            }
        }
    }

    return 0;
}