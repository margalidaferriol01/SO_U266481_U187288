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
// pid de l'últim procés en mode CONCURRENT (iniciem a -1 si encara no n'hi ha cap) 
static pid_t ultim_concurrent = -1;              

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

// Funció auxiliar per llegir els segons de la línia TIMEOUT. 
// Retorna 1 si ha pogut extreure els segons correctament, 0 en cas contrari.
int parse_timeout_line(char *line, int *seconds) {
    if (strncmp(line, "TIMEOUT", 7) == 0) {
        if (sscanf(line + 7, "%d", seconds) == 1) {
            return 1;
        } else {
            char num_line[MAX_LINE];
            if (extreure_linia(num_line, sizeof(num_line)) >= 0) {
                *seconds = atoi(num_line);
                return 1;
            }
        }
    }
    return 0;
}

// 1. EXECUCIÓ EN MODE SINGLE
void executar_single(char *cmd_line, int timeout_sec) {
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
        pid_t pid_timer = -1;

        // Si hi ha TIMEOUT, creem el procés temporitzador
        if (timeout_sec > 0) {
            pid_timer = fork();
            if (pid_timer == 0) {
                sleep(timeout_sec);
                kill(pid, SIGKILL);
                exit(0);
            }
        }
    
        waitpid(pid, NULL, 0);

        // Netejem el temporitzador quan la comanda acaba
        if (timeout_sec > 0 && pid_timer > 0) {
            kill(pid_timer, SIGKILL);
            waitpid(pid_timer, NULL, 0);
        }

        free(args); 
    } 
    else {
        perror("Error en fork");
        free(args);
    }
}

// 2. EXECUCIÓ EN MODE CONCURRENT 
void executar_concurrent(char *cmd_line, int timeout_sec) {
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
        // A més, guardem el pid del fill per poder fer KILL quan es faci Ctrl+C
        ultim_concurrent = pid;

        if (timeout_sec > 0) {
            pid_t pid_timer = fork();
            if (pid_timer == 0) {
                sleep(timeout_sec);
                kill(pid, SIGKILL);
                exit(0);
            }
        }

        free(args);
    } 
    else {
        // Error en la creació del procés fill. Mostram un missatge d'error i alliberam la memòria.
        perror("Error en fork");
        free(args);
    }
}

// 3. EXECUCIÓ EN MODE PIPED
void executar_piped(char *cmd1_line, char *cmd2_line, int timeout_sec) {
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

    pid_t pid_timer = -1;
    if (timeout_sec > 0) {
        pid_timer = fork();
        if (pid_timer == 0) {
            sleep(timeout_sec);
            kill(pid1, SIGKILL);
            kill(pid2, SIGKILL);
            exit(0);
        }
    }

    // Esperam que finalitzin els dos processos fills per no deixar processos a l'aire.
    waitpid(pid1, NULL, 0);
    waitpid(pid2, NULL, 0);

    if (timeout_sec > 0 && pid_timer > 0) {
        kill(pid_timer, SIGKILL);
        waitpid(pid_timer, NULL, 0);
    }

    // Alliberam la memòria dinàmica dels dos vectors d'arguments.
    free(args1);
    free(args2);
}

// 4. EXECUCIÓ EN MODE TIMEOUT
// La podem comentar perquè ja hem integrat la funcionalitat de timeout dins de SINGLE, CONCURRENT i PIPED.
/* void executar_timeout(int temps, char *cmd_line){
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
}*/

// 5. GESTIÓ DE CTRL+C

// La funció s'executa quan es fa CTRL+C
// Enlloc de tancar el shell, enviem missatge SIGKILL a l'últim concurrent.
void gestor_sigint(int sig){
    if(ultim_concurrent>0){
        kill(ultim_concurrent,SIGKILL);
    }
}

// 6. BUCLE PRINCIPAL
int main(void) {
    char line1[MAX_LINE];
    char line2[MAX_LINE];
    char cmd1[MAX_LINE];
    char cmd2[MAX_LINE];

    // Inicialitzam el buffer circular necessari per a la lectura d'entrada.
    if (buffer_init(&cbuf, BUFFER_SIZE) != 0) {
        perror("Error inicialitzant el buffer");
        return EXIT_FAILURE;
    }

    // Ara, quan arribi SIGINT, s'executa gestor_sigint. 
    signal(SIGINT, gestor_sigint);

    // Bucle principal del shell (espera comandes contínuament).
    while (1) {

        // Inicialitzem el temporitzador a 0 a cada iteració
        int timeout_sec = 0;

        if (extreure_linia(line1, sizeof(line1)) < 0) {
            break;
        }

        // Si el mode és EXIT, acabam el bucle per tancar shell.
        if (strcmp(line1, "EXIT") == 0) {
            break;
        }

        // Cas 1: Instrucció TIMEOUT 
       if (strncmp(line1, "TIMEOUT", 7) == 0) {
            parse_timeout_line(line1, &timeout_sec);
            if (extreure_linia(cmd1, sizeof(cmd1)) >= 0) {
                executar_single(cmd1, timeout_sec);
            }
            continue;
        }

        // Cas 2: Modes amb opció de modificador TIMEOUT (SINGLE, CONCURRENT, PIPED)
        char mode[MAX_LINE];
        strcpy(mode, line1);

        if (extreure_linia(line2, sizeof(line2)) < 0) {
            break;
        }

        if (strncmp(line2, "TIMEOUT", 7) == 0) {
            parse_timeout_line(line2, &timeout_sec);
            if (extreure_linia(cmd1, sizeof(cmd1)) < 0) {
                break;
            }
        } else {
            strcpy(cmd1, line2);
        }

        if (strcmp(mode, "SINGLE") == 0) {
            executar_single(cmd1, timeout_sec);
        } 
        else if (strcmp(mode, "CONCURRENT") == 0) {
            executar_concurrent(cmd1, timeout_sec);
        } 
        else if (strcmp(mode, "PIPED") == 0) {
            if (extreure_linia(cmd2, sizeof(cmd2)) >= 0) {
                executar_piped(cmd1, cmd2, timeout_sec);
            }
        }
    }

    buffer_deallocate(&cbuf);
    return 0;
}