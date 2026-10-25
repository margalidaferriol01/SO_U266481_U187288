#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <signal.h>

#include "splitCommand.h"
#include "circularBuffer.h"

#define MAX_LINE 256
#define BUFFER_SIZE 1024

static CircularBuffer cbuf;                 
static unsigned char linear[BUFFER_SIZE];  
static int fi = 0;                 

int extreure_linia(char *out, int max) {

    while (1) {
        // Mira si hi ha una línia completa al buffer.
        // Si hi ha línia completa retorna la mida.
        // Si no hi ha cap línia completa retorna -1.
        int mida = buffer_size_next_element(&cbuf, '\n', fi);

        // Cas 1: No hi ha cap \n i el buffer és ple.
        // En aquest cas, agafa tot el que hi ha.
        if (mida == -1 && buffer_free_bytes(&cbuf) == 0)
            mida = buffer_used_bytes(&cbuf);

        // Cas 2: Hi ha una línia completa per treure del buffer. 
        if (mida > 0) {
            // Traiem la línia del buffer byte a byte i la copiem a out.
            int j = 0;
            for (int i = 0; i < mida; i++) {
                unsigned char c = buffer_pop(&cbuf);
                // Només ho copiem a out si hi ha espai.
                if (j < max - 1)
                    out[j++] = (char)c;
            }
            out[j] = '\0';
            out[strcspn(out, "\r\n")] = '\0';
            return (int)strlen(out);
        }

        // Cas 3: No hi ha línia al buffer i hem arribat al final de l'entrada.
        // Hem acabat de llegir.
        if (fi==1)
            return -1;

        // Cas 4: La línia està a mitges. No hi ha línia completa però pot arribar-ne més.
        // Llegim un bloc nou de l'entrada estàndar al buffer lineal.
        // Només llegim els bytes que caben al buffer circular.
        int llegits = read(0, linear, buffer_free_bytes(&cbuf));

        if (llegits <= 0) {
            // Si read retorna 0 o -1 marquem que hem arribat al final.
            fi = 1;
        } else {
            // Guardem els bytes llegits dins del buffer circular.
            for (int i = 0; i < llegits; i++) {
                buffer_push(&cbuf, linear[i]);
            }
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

// 4. EXECUCIÓ EN MODE TIMEOUT

void executar_timeout(int temps, char *cmd_line){
    // Convertim la cadena de text en un vector d'arguments
    char **args = split_command(cmd_line);
    // Comprovam si el split ha fallat o si la línia estava buida
    if (args == NULL || args[0] == NULL) {
        if (args != NULL) free(args);
        return;
    }

    // -- PRIMER FILL: executa la comanda.
    pid_t pid = fork();
    if (pid == 0) {
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    }

    // -- SEGON FILL: espera el temps i després mata la comanda.
    // Dorm els segons passats a la funció i després envia SIGKILL.
    pid_t pid_timer = fork();
    if (pid_timer == 0) {
        sleep(temps);
        kill(pid, SIGKILL);
        exit(0);
    }

    // El pare espera que acabi la comanda.
    waitpid(pid, NULL, 0);

    // Quan la comanda acaba, mata el temporitzador. 
    kill(pid_timer, SIGKILL);
    waitpid(pid_timer, NULL, 0);

    // Alliberem la memòria del verctor d'arguments.
    free(args);
}

// 5. BUCLE PRINCIPAL
int main(void) {
    char mode[MAX_LINE];
    char cmd1[MAX_LINE];
    char cmd2[MAX_LINE];

    if (buffer_init(&cbuf, BUFFER_SIZE) != 0) {
        perror("Error inicialitzant el buffer");
        return EXIT_FAILURE;
    }

    while (1) {
        if (extreure_linia(mode, sizeof(mode)) < 0) {
            break;
        }

        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        if (strcmp(mode, "SINGLE") == 0) {
            if (extreure_linia(cmd1, sizeof(cmd1)) >= 0) {
                executar_single(cmd1);
            }
        } 
        else if (strcmp(mode, "CONCURRENT") == 0) {
            if (extreure_linia(cmd1, sizeof(cmd1)) >= 0) {
                executar_concurrent(cmd1);
            }
        } 
        else if (strcmp(mode, "PIPED") == 0) {
            if (extreure_linia(cmd1, sizeof(cmd1)) >= 0) {
                if (extreure_linia(cmd2, sizeof(cmd2)) >= 0) {
                    executar_piped(cmd1, cmd2);
                }
            }
        }
        // Gestió del mode TIMEOUT
        else if (strcmp(mode, "TIMEOUT") == 0) {
            if (extreure_linia(cmd1, sizeof(cmd1)) >= 0) {
                if (extreure_linia(cmd2, sizeof(cmd2)) >= 0) {
                    int temps = atoi(cmd1);
                    executar_timeout(temps, cmd2);
                }
            }
        }
    }

    // Alliberem la memòria del buffer circular.
    buffer_deallocate(&cbuf);
    return 0;
}