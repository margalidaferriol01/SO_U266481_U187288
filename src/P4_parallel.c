#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include "parsePGM.h"

#define HIST_SIZE 256
#define BUFF_SIZE 1024

// Estructura per passar les dades a cada fil 
typedef struct {
    const char *filename;
    long offset;
    long bytesToRead;
    unsigned long local_hist[HIST_SIZE];
} ThreadInfo;

// Funció que executarà cada fil 
void *worker_thread(void *arg);

int main(int argc, char *argv[]) {
    // Miram que ens passin els 3 arguments: imatge, sortida i nombre de fils
    if (argc != 4) {
        fprintf(stderr, "Ús: %s <imatge.pgm> <sortida.csv> <num_fils>\n", argv[0]);
        return 1;
    }

    const char *input_file = argv[1];
    const char *output_file = argv[2];
    int num_threads = atoi(argv[3]);

    if (num_threads <= 0) {
        fprintf(stderr, "El nombre de fils ha de ser més gran que 0.\n");
        return 1;
    }

    // Llegim la capçalera PGM per saber quant ocupa i la mida de la imatge
    int width = 0, height = 0, maxval = 0;
    long header_bytes = parse_pgm_header(input_file, &width, &height, &maxval);
    if (header_bytes < 0) {
        fprintf(stderr, "Error llegint la capçalera de %s\n", input_file);
        return 1;
    }

    long total_bytes = (long)width * height;
    long chunk = total_bytes / num_threads;
    long remainder = total_bytes % num_threads;

    // Reservam memòria pels fils i ses seves estructures
    pthread_t *threads = malloc(num_threads * sizeof(pthread_t));
    ThreadInfo *t_info = malloc(num_threads * sizeof(ThreadInfo));
    if (!threads || !t_info) {
        perror("Error reservant memòria");
        return 1;
    }

    // Repartim es bytes entre els fils i els cream 
    for (int i = 0; i < num_threads; i++) {
        t_info[i].filename = input_file;
        t_info[i].offset = header_bytes + (i * chunk);
        t_info[i].bytesToRead = chunk + (i == num_threads - 1 ? remainder : 0);
        memset(t_info[i].local_hist, 0, sizeof(t_info[i].local_hist));

        if (pthread_create(&threads[i], NULL, worker_thread, &t_info[i]) != 0) {
            perror("Error creant el fil");
            return 1;
        }
    }

    // Esperam que acabin tots els fils i sumam els seus histogrames locals
    unsigned long global_hist[HIST_SIZE] = {0};

    for (int i = 0; i < num_threads; i++) {
        pthread_join(threads[i], NULL);
        for (int bin = 0; bin < HIST_SIZE; bin++) {
            global_hist[bin] += t_info[i].local_hist[bin];
        }
    }

    // Escrivim el resultat final dins el fitxer de sortida
    FILE *out = fopen(output_file, "w");
    if (!out) {
        perror("Error obrint el fitxer de sortida");
        free(threads);
        free(t_info);
        return 1;
    }

    for (int bin = 0; bin < HIST_SIZE; bin++) {
        fprintf(out, "%d,%lu\n", bin, global_hist[bin]);
    }

    fclose(out);
    free(threads);
    free(t_info);
    return 0;
}


void *worker_thread(void *arg) {
    ThreadInfo *info = (ThreadInfo *)arg;

    // Cada fil obre el fitxer pel seu compte, així no compartim el punter de lectura
    int fd = open(info->filename, O_RDONLY);
    if (fd < 0) {
        perror("Error obrint la imatge");
        return NULL;
    }

    // Ens coloquem a l'inici del nostre tros
    lseek(fd, info->offset, SEEK_SET);

    unsigned char buffer[BUFF_SIZE];
    long pendents = info->bytesToRead;

    // Llegim de 1024 en 1024 fins acabar el nostre tros
    while (pendents > 0) {
        int a_llegir = BUFF_SIZE;
        if (pendents < BUFF_SIZE) {
            a_llegir = pendents; 
        }

        int llegits = read(fd, buffer, a_llegir);
        if (llegits <= 0) {
            fprintf(stderr, "Error: el fitxer s'ha acabat abans d'hora\n");
            break;
        }

        // Sumem cada pixel al nostre histograma (que nomes toca aquest fil)
        for (int i = 0; i < llegits; i++) {
            info->local_hist[buffer[i]]++;
        }

        pendents = pendents - llegits;
    }

    close(fd);
    return NULL;
}