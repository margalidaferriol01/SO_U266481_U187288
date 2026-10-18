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

// 1. EXECUCIÓ EN MODE SINGLE
void executar_single(char *cmd_line) {
    // Convertim la cadena de text (ex: "ls -l /tmp") en un vector d'arguments
    // args[0] = "ls", args[1] = "-l", args[2] = "/tmp", args[3] = NULL
    char **args = split_command(cmd_line);
    // Comprovam si el split ha fallat o si la línia estava buida.
    if (args == NULL || args[0] == NULL) {
        if (args != NULL) free(args);
        return; // No hi ha cap comanda vàlida per executar.
    }

    // Duplicam el procés actual per crear un procés fill.
    pid_t pid = fork();

    if (pid == 0) {
        // --- PROCÉS FILL ---
        // Reemplaçam la imatge d'aquest procés per l'executable de la comanda.
        // Si té èxit, execvp MAI retorna, ja que el procés passa a ser la nova comanda.
        execvp(args[0], args);
        // Si arribam a aquesta línia, vol dir que execvp ha fallat.
        perror("Error en execvp");
        exit(EXIT_FAILURE);// Tancam el fill amb codi d'error.
    } 
    else if (pid > 0) {
        // --- PROCÉS PARE ---
        // En mode SINGLE, el pare s'ha de bloquejar fins que el fill acabi.
        waitpid(pid, NULL, 0);
        // Un pic el fill ha acabat, alliberam el vector d'arguments que crea split_command.
        free(args); 
    } 
    else {
        // --- ERROR EN FORK ---
        // Si el sistema no ha pogut crear el procés fill, imprimim un missatge d'error i alliberam la memòria.
        perror("Error en fork");
        free(args);
    }
}

// 2. EXECUCIÓ EN MODE CONCURRENT 
void executar_concurrent(char *cmd_line) {
    // Analitzam la comanda en un vector d'arguments.
    char **args = split_command(cmd_line);
    if (args == NULL || args[0] == NULL) {
        if (args != NULL) free(args);
        return;
    }

    //Crearem un procés fill per executar la comanda de manera concurrent.
    pid_t pid = fork();

    if (pid == 0) {
        // --- PROCÉS FILL ---
        // Executam la comanda exactament igual que a SINGLE.
        execvp(args[0], args);
        perror("Error en execvp");
        exit(EXIT_FAILURE);
    } 
    else if (pid > 0) {
        // --- PROCÉS PARE ---
        // A diferència de SINGLE, NO feim waitpid().
        // El pare allibera la memòria dels arguments i continua immediatament el bucle principal.
        // Això permet que el fill s'executi en segon pla paral·lelament al shell.
        free(args);
    } 
    else {
        // Error en la creació del procés fill. Mostram un missatge d'error i alliberam la memòria.
        perror("Error en fork");
        free(args);
    }
}

// 3. EXECUCIÓ EN MODE PIPED
void executar_piped(char *cmd1_line, char *cmd2_line) {
    // Analitzam les dues comandes de manera independent.
    char **args1 = split_command(cmd1_line);
    char **args2 = split_command(cmd2_line);

    // Validam que totes dues comandes s'hagin pogut dividir bé.
    if (args1 == NULL || args1[0] == NULL || args2 == NULL || args2[0] == NULL) {
        if (args1 != NULL) free(args1);
        if (args2 != NULL) free(args2);
        return;
    }

    // Cream la canonada (pipe). 
    // pipefd[0] serà de LECTURA i pipefd[1]  d'ESCRIPTURA.
    int pipefd[2];
    if (pipe(pipefd) == -1) {
        perror("Error en pipe");
        free(args1);
        free(args2);
        return;
    }

    // --- PRIMER FILL: Executa cmd1 (Productor de dades) ---
    pid_t pid1 = fork();
    if (pid1 == 0) {
        // Redirigim la stdout cap a l'extrem d'escriptura del pipe.
        dup2(pipefd[1], STDOUT_FILENO);

        // Tancam els descriptors originals del pipe perquè el fill ja no els necessita directament.
        close(pipefd[0]);
        close(pipefd[1]);

        // Executam la primera comanda (la seva sortida anirà al pipe).
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

    // --- SEGON FILL: Executa cmd2 (Consumidor de dades) ---
    pid_t pid2 = fork();

    if (pid2 == 0) {
        // Redirigim l'entrada estàndard (STDIN) cap a l'extrem de lectura del pipe.
        dup2(pipefd[0], STDIN_FILENO);

        // Tancam els descriptors originals del pipe perquè el fill ja no els necessita directament.
        close(pipefd[0]);
        close(pipefd[1]);

        // Executam la segona comanda (llegirà com a entrada el que escrigui cmd1)
        execvp(args2[0], args2);
        perror("Error en execvp (cmd2)");
        exit(EXIT_FAILURE);
    } 
    else if (pid2 < 0) {
        perror("Error en el segon fork");
    }

    // --- PROCÉS PARE ---
    // El pare HA DE tancar els seus dos extrems del pipe.
    // Si no ho fa, el segon fill mai no rebrà el final de fitxer (EOF) i es quedarà bloquejat per sempre.
    close(pipefd[0]);
    close(pipefd[1]);

    // Esperam que finalitzin els dos processos fills per no deixar processos a l'aire.
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    // Alliberam la memòria dinàmica dels dos vectors d'arguments.
    free(args1);
    free(args2);
}

// 4. BUCLE PRINCIPAL
int main(void) {
    char mode[MAX_LINE];
    char cmd1[MAX_LINE];
    char cmd2[MAX_LINE];

    // Inicialitzam el buffer circular necessari per a la lectura d'entrada.
    if (buffer_init(&cbuf, BUFFER_SIZE) != 0) {
        perror("Error inicialitzant el buffer");
        return EXIT_FAILURE;
    }

    // Bucle principal del shell (espera comandes contínuament).
    while (1) {
        // Llegim la línia que indica el MODE (SINGLE, CONCURRENT, PIPED o EXIT)
        if (read_line(mode, sizeof(mode)) < 0) {
            break; // Si es detecta final de fitxer (EOF) o error de lectura, sortim.
        }

        // Si el mode és EXIT, acabam el bucle per tancar shell.
        if (strcmp(mode, "EXIT") == 0) {
            break;
        }

        // Gestió del mode SINGLE
        if (strcmp(mode, "SINGLE") == 0) {
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                executar_single(cmd1);
            }
        } 
        // Gestió del mode CONCURRENT
        else if (strcmp(mode, "CONCURRENT") == 0) {
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                executar_concurrent(cmd1);
            }
        } 
        // Gestió del mode PIPED
        else if (strcmp(mode, "PIPED") == 0) {
            // El mode PIPED requereix llegir dues línies de comanda consecutives.
            if (read_line(cmd1, sizeof(cmd1)) >= 0) {
                if (read_line(cmd2, sizeof(cmd2)) >= 0) {
                    executar_piped(cmd1, cmd2);
                }
            }
        }
    }

    // Alliberam la memòria del buffer circular abans de finalitzar el programa.
    buffer_deallocate(&cbuf);
    return 0;
}