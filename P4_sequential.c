#include <pthread.h>
#include <unistd.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "parsePGM.h"

#define BUFF_SIZE 1024


int main(int argc, char* argv[]) {
    if (argc != 3) {
        printf("Incorrect usage. Args needed: histogram pathToImage pathToHistogramOut\n");
        _exit(1);
    }

    // Read the header
    int width, height;
    int maxval;
    int nBytesHeader = parse_pgm_header(argv[1], &width, &height, &maxval);
    if (maxval > 255){
        perror("Expecting 1 byte ints\n");
        _exit(1);
    }
    int nPixels = width * height;

    int nBytesRead;
    unsigned int* histogram = malloc((maxval + 1) * sizeof(unsigned int));
    for (int i = 0; i <= maxval; i++) {
        histogram[i] = 0;
    }

    // Compute histogram. This is what you will need to do in threads.
    unsigned char* buffer = malloc(BUFF_SIZE);
    int fd = open(argv[1], O_RDONLY);
    lseek(fd, nBytesHeader, SEEK_SET); // Move the cursor to the start of the data segment
                                       // For the 
    for (int i = 0; i < nPixels; i += BUFF_SIZE) {
        nBytesRead = read(fd, buffer, BUFF_SIZE);
        if (nBytesRead <= 0) {
            printf("Unexpected error: the file ended before all pixels where read.\n");
            _exit(1);
        }
        for (int i = 0; i < nBytesRead; i++) {
            histogram[buffer[i]]++;
        }
    }
    close(fd);


    // Write histogram, so it can be loaded in python with np.loadtxt
    int fd_out = open(argv[2], O_WRONLY | O_CREAT, 0644);
    for (int i = 0; i <= maxval; i++) {
        char s[80];
        sprintf(s, "%d,%d\n", i, histogram[i]);
        write(fd_out, s, strlen(s));
    }
    close(fd_out);

}