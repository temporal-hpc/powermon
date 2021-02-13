/*
 Copyright (c) 2021 Temporal Guild Group, Austral University of Chile, Valdivia Chile.
 This file and all powermon software is licensed under the MIT License. 
 Please refer to LICENSE for more details.
 */

#include <cuda.h>
#include <mma.h>
#include <stdio.h>
#include <iostream>
#include <iomanip>
#include <string>
#include <map>
#include <random>
#include <cmath>
#include <omp.h>
#define REAL float
#define TCSIZE 16
#define TCSQ 256
#define PRINTLIMIT 2560
#define WARPSIZE 32
#define DIFF (BSIZE<<3)

#define OMP_REDUCTION_FLOAT "omp-nt" STR(NPROC) "-float"
#define OMP_REDUCTION_DOUBLE "omp-nt" STR(NPROC) "-double"

#define STR_HELPER(x) #x
#define STR(x) STR_HELPER(x)

#include "nvmlPower.hpp"


int main(int argc, char **argv){
    if(argc != 2){
        fprintf(stderr, "\nrun as ./powermon dt-ms\ndt-ms: sample interval in milliseconds\n\n");
        exit(EXIT_FAILURE);
    }
    int ms = atoi(argv[1]); 
    // begin
    printf("Press enter to finalize...\n");
    GPUPowerBegin("gpu", ms);
    CPUPowerBegin("cpu", ms);

    printf("enter para terminar\n"); fflush(stdout);
    getchar();
    // end
    GPUPowerEnd();
    CPUPowerEnd();
    exit(EXIT_SUCCESS);
}
