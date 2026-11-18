#!/bin/bash
gcc -O2 -Wall src/P4_parallel.c src/parsePGM.c -o computeHistogramParallel -pthread
