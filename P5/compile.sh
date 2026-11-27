#!/bin/bash
gcc -O2 -Wall src/P5_parallel.c src/parsePGM.c -o computeHistogram -pthread
