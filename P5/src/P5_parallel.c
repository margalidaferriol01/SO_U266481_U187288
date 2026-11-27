#include <stdio.h>
#include <unistd.h>
#include <stdlib.h>
#include <pthread.h>
#include "parsePGM.h"

#define HIST_SIZE 256
#define BLOCK_SIZE 1024 * 16 

// fitxer i buffer circular compartit
FILE *img_file;
unsigned char **buffer;
int *sizes;
int buffer_size = 0;

int in = 0;
int out = 0;
int count = 0;
int active_producers = 0;

unsigned long global_hist[HIST_SIZE] = {0};

// mutex i variables de condicio
pthread_mutex_t file_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t buf_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t hist_mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_cond_t not_full = PTHREAD_COND_INITIALIZER;
pthread_cond_t not_empty = PTHREAD_COND_INITIALIZER;

void *producer(void *arg) {
    unsigned char temp[BLOCK_SIZE];

    while (1) {
        // llegim següent bloc de la imatge protegint el fitxer
        pthread_mutex_lock(&file_mutex);
        int n = fread(temp, 1, BLOCK_SIZE, img_file);
        pthread_mutex_unlock(&file_mutex);

        // si ja hem arribat al final sortim
        if (n <= 0) break;

        // posem el bloc al buffer circular
        pthread_mutex_lock(&buf_mutex);
        while (count == buffer_size) {
            pthread_cond_wait(&not_full, &buf_mutex);
        }
        for (int i = 0; i < n; i++) {
            buffer[in][i] = temp[i];
        }
        sizes[in] = n;
        in = (in + 1) % buffer_size;
        count++;
        pthread_cond_signal(&not_empty);
        pthread_mutex_unlock(&buf_mutex);
    }

    // aquest productor ja ha acabat; si es el darrer avisem a tots els consumidors
    pthread_mutex_lock(&buf_mutex);
    active_producers--;
    if (active_producers == 0) {
        pthread_cond_broadcast(&not_empty);
    }
    pthread_mutex_unlock(&buf_mutex);

    return NULL;
}

void *consumer(void *arg) {
    unsigned char temp[BLOCK_SIZE];
    unsigned long local_hist[HIST_SIZE] = {0};

    while (1) {
        // agafem un bloc del buffer si n'hi ha
        pthread_mutex_lock(&buf_mutex);
        while (count == 0 && active_producers > 0) {
            pthread_cond_wait(&not_empty, &buf_mutex);
        }
        // si el buffer es buit i ja no queden productors acabem
        if (count == 0 && active_producers == 0) {
            pthread_mutex_unlock(&buf_mutex);
            break;
        }
        int n = sizes[out];
        for (int i = 0; i < n; i++) {
            temp[i] = buffer[out][i];
        }
        out = (out + 1) % buffer_size;
        count--;
        pthread_cond_signal(&not_full);
        pthread_mutex_unlock(&buf_mutex);

        // calculem l'histograma local fora del mutex per no bloquejar
        for (int i = 0; i < n; i++) {
            local_hist[temp[i]]++;
        }
    }

    // sumem el resultat local al global al final
    pthread_mutex_lock(&hist_mutex);
    for (int i = 0; i < HIST_SIZE; i++) {
        global_hist[i] += local_hist[i];
    }
    pthread_mutex_unlock(&hist_mutex);

    return NULL;
}

int main(int argc, char *argv[]) {
    if (argc != 6) return 1;

    int n_prod = atoi(argv[3]);
    int n_cons = atoi(argv[4]);
    buffer_size = atoi(argv[5]);

    // llegim capcalera i ens saltem els primers bytes per anar directe als pixels
    int width, height, maxval;
    long header = parse_pgm_header(argv[1], &width, &height, &maxval);
    if (header < 0) return 1;

    img_file = fopen(argv[1], "rb");
    fseek(img_file, header, SEEK_SET);

    // reservem memoria pel buffer circular
    active_producers = n_prod;
    buffer = malloc(buffer_size * sizeof(unsigned char *));
    sizes = malloc(buffer_size * sizeof(int));
    for (int i = 0; i < buffer_size; i++) {
        buffer[i] = malloc(BLOCK_SIZE);
    }

    // creem productors i consumidors
    pthread_t prod[n_prod], cons[n_cons];
    for (int i = 0; i < n_prod; i++) pthread_create(&prod[i], NULL, producer, NULL);
    for (int i = 0; i < n_cons; i++) pthread_create(&cons[i], NULL, consumer, NULL);

    // esperem que acabin tots
    for (int i = 0; i < n_prod; i++) pthread_join(prod[i], NULL);
    for (int i = 0; i < n_cons; i++) pthread_join(cons[i], NULL);

    // escrivim l'histograma final
    FILE *out_f = fopen(argv[2], "w");
    for (int i = 0; i <= maxval; i++) {
        fprintf(out_f, "%d,%lu\n", i, global_hist[i]);
    }

    // netegem memoria i fitxers
    fclose(img_file);
    fclose(out_f);
    for (int i = 0; i < buffer_size; i++) free(buffer[i]);
    free(buffer);
    free(sizes);
    return 0;
}