#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>

#include "splitCommand.h"
#include "circularBuffer.h"

#define MAX_LINE 256
#define BUFFER_SIZE 1024

static CircularBuffer cbuf;                 // buffer circular on acumulem les dades
static unsigned char linear[BUFFER_SIZE];   // buffer lineal auxiliar per al read()
static int reached_eof = 0;                 // 1 quan read() ja no retorna més dades

int read_line(char *out, int max) {
    while (1) {
        // 1. Mirem si al buffer ja hi ha una línia completa
        int n = buffer_size_next_element(&cbuf, '\n', reached_eof);

        // Cas límit: buffer ple i cap '\n' -> la traiem igualment
        if (n == -1 && buffer_free_bytes(&cbuf) == 0)
            n = buffer_used_bytes(&cbuf);

        if (n > 0) {
            // 2. Hi ha una línia: la traiem byte a byte amb pop
            int j = 0;
            for (int i = 0; i < n; i++) {
                unsigned char c = buffer_pop(&cbuf);
                if (j < max - 1)
                    out[j++] = (char)c;
            }
            out[j] = '\0';
            out[strcspn(out, "\r\n")] = '\0';   // traiem el salt de línia
            return (int)strlen(out);
        }

        // 3. No hi ha línia i ja som a EOF -> s'ha acabat
        if (reached_eof)
            return -1;

        // 4. Llegim un bloc nou al buffer lineal
        int free_bytes = buffer_free_bytes(&cbuf);
        ssize_t r = read(STDIN_FILENO, linear, free_bytes);
        if (r <= 0) {
            reached_eof = 1;
        } 
        else {
            // 5. Passem les dades al buffer circular
            for (ssize_t i = 0; i < r; i++)
                buffer_push(&cbuf, linear[i]);
        }
    }
}

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

    if (buffer_init(&cbuf, BUFFER_SIZE) != 0) {
        perror("Error inicialitzant el buffer");
        return EXIT_FAILURE;
    }

    while (1) {
        if (read_line(mode, sizeof(mode)) < 0) {
            break;
        }

        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        if (strcmp(mode, "SINGLE") == 0) {
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                executar_single(cmd1);
            }
        } 
        else if (strcmp(mode, "CONCURRENT") == 0) {
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                executar_concurrent(cmd1);
            }
        } 
        else if (strcmp(mode, "PIPED") == 0) {
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                if (read_line(cmd2, sizeof(cmd2)) >= 0) {
                    executar_piped(cmd1, cmd2);
                }
            }
        }
    }

    buffer_deallocate(&cbuf);
    return 0;
}