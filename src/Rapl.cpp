/*
 Copyright (c) 2021 Temporal Guild Group, Austral University of Chile, Valdivia Chile.
 This file and all powermon software is licensed under the MIT License. 
 Please refer to LICENSE for more details.
 */
#include <cstdio>
#include <string>
#include <sstream>
#include <unistd.h>
#include <fcntl.h>
#include <cmath>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/stat.h>


#include "Rapl.h"

#define MSR_RAPL_POWER_UNIT            0x606

/*
 * Platform specific RAPL Domains.
 * Note that PP1 RAPL Domain is supported on 062A only
 * And DRAM RAPL Domain is supported on 062D only
 */
/* Package RAPL Domain */
#define MSR_PKG_RAPL_POWER_LIMIT       0x610
#define MSR_PKG_ENERGY_STATUS          0x611
#define MSR_PKG_PERF_STATUS            0x13
#define MSR_PKG_POWER_INFO             0x614

/* PP0 RAPL Domain */
#define MSR_PP0_POWER_LIMIT            0x638
#define MSR_PP0_ENERGY_STATUS          0x639
#define MSR_PP0_POLICY                 0x63A
#define MSR_PP0_PERF_STATUS            0x63B

/* PP1 RAPL Domain, may reflect to uncore devices */
#define MSR_PP1_POWER_LIMIT            0x640
#define MSR_PP1_ENERGY_STATUS          0x641
#define MSR_PP1_POLICY                 0x642

/* DRAM RAPL Domain */
#define MSR_DRAM_POWER_LIMIT           0x618
#define MSR_DRAM_ENERGY_STATUS         0x619
#define MSR_DRAM_PERF_STATUS           0x61B
#define MSR_DRAM_POWER_INFO            0x61C

/* RAPL UNIT BITMASK */
#define POWER_UNIT_OFFSET              0
#define POWER_UNIT_MASK                0x0F

#define ENERGY_UNIT_OFFSET             0x08
#define ENERGY_UNIT_MASK               0x1F00

#define TIME_UNIT_OFFSET               0x10
#define TIME_UNIT_MASK                 0xF000

#define SIGNATURE_MASK                 0xFFFF0

/* AMD MSR DEFINES */
#define AMD_MSR_PWR_UNIT 	       0xC0010299
#define AMD_MSR_CORE_ENERGY 	       0xC001029A
#define AMD_MSR_PACKAGE_ENERGY         0xC001029B

#define AMD_TIME_UNIT_MASK             0xF0000
#define AMD_ENERGY_UNIT_MASK           0x1F00
#define AMD_POWER_UNIT_MASK            0xF

// CPU signature codes, useful for filtering uncompatible measures
#define IVYBRIDGE_E                    0x306F0
#define SANDYBRIDGE_E                  0x206D0
#define COFFEE_LAKE                    0x906E0
#define SKYLAKE_SERVER                 0x50650
#define BROADWELL_E                    0x406F0


Rapl::Rapl() {

	vendor = get_vendor();
	n_sockets = get_n_sockets();
	smt = get_smt();
	n_logical_cores = get_n_logical_cores();
	pp1_supported = detect_pp1();

	for (int i=0; i<n_sockets; i++){
		open_msr(i, first_lcoreid[i]);
	}
	/* Read MSR_RAPL_POWER_UNIT Register */
	uint64_t raw_value;
	printf("trying vendor units %u\n", vendor);
	if (vendor == 0){
		raw_value = read_msr(0, MSR_RAPL_POWER_UNIT);
	} else if (vendor == 1){
		raw_value = read_msr(0, AMD_MSR_PWR_UNIT);
	}
	power_units = pow(0.5,	(double) (raw_value & 0xf));
	energy_units = pow(0.5,	(double) ((raw_value >> 8) & 0x1f));
	time_units = pow(0.5,	(double) ((raw_value >> 16) & 0xf));

	/* Read MSR_PKG_POWER_INFO Register */
	if (vendor==0){
		raw_value = read_msr(0, MSR_PKG_POWER_INFO);
		thermal_spec_power = power_units * ((double)(raw_value & 0x7fff));
		minimum_power = power_units * ((double)((raw_value >> 16) & 0x7fff));
		maximum_power = power_units * ((double)((raw_value >> 32) & 0x7fff));
		time_window = time_units * ((double)((raw_value >> 48) & 0x7fff));
	} else if (vendor == 1){
		thermal_spec_power = 0; 
		minimum_power = 0;
		maximum_power = 0;
		time_window = 0;
	
	}
	reset();
}

void Rapl::reset() {

	for (int i=0; i<n_sockets; i++){
		prev_state[i] = new rapl_state_t;
		current_state[i] = new rapl_state_t;
		next_state[i] = new rapl_state_t;
		running_total[i] = new rapl_state_t;

		// sample twice to fill current and previous
		sample(i);
		sample(i);

		// Initialize running_total
		running_total[i]->pkg = 0;
		running_total[i]->pp0 = 0;
		running_total[i]->dram = 0;
		gettimeofday(&(running_total[i]->tsc), NULL);
	}
}
int Rapl::get_n_logical_cores(){
	uint32_t eax, ebx, ecx, edx;
	__asm__("mov $0x0000000B, %%eax;" // set eax to 0x0000000B
		"mov $0x01, %%ecx;" // set ecx to 0x01
		"cpuid;" // execute cpuid
		:"=a"(eax), "=b"(ebx), "=d"(edx), "=c"(ecx) // output operands
		: // no input operand
		);
	printf("Logical cores = %u\n", ebx & 0xFF);
	return ebx & 0xFF; // print the string
}
int Rapl::get_smt(){

	uint32_t eax, ebx, ecx, edx;
	if (vendor==1){
		__asm__("mov $0x8000001E, %%eax;" // set eax to 0x0000000B
			"cpuid;" // execute cpuid
			:"=a"(eax), "=b"(ebx), "=d"(edx), "=c"(ecx) // output operands
			: // no input operand
			);

		return (ebx & 0xFF00) >> 8;
	} else {
		printf("Get hyperthreading not yet implemented for intel. Assuming disabled\n");
		return 0;
	}
}
int Rapl::get_vendor(){
	int v = 0;
	uint32_t eax = 0;
	union {
	    struct {
		uint32_t ebx;
		uint32_t edx;
		uint32_t ecx;
	    };
	    char vendor[13];
	} u;
	__asm__("cpuid;"
		:"=a"(eax), "=b"(u.ebx), "=d"(u.edx), "=c"(u.ecx) // output operands
		:"0"(eax) // input operand
		);
	u.vendor[12] = '\0'; // add the null terminator

	printf("vendor = %s\n", u.vendor); // print the string
	if (strcmp(u.vendor, "AuthenticAMD") == 0) {
	    v = 1;
	} else {
	    v = 0;
	}

	
	return v;

}

bool Rapl::detect_pp1() {
	uint32_t eax_input = 1;
	uint32_t eax;
	__asm__("cpuid;"
			:"=a"(eax)               // EAX into b (output)
			:"0"(eax_input)          // 1 into EAX (input)
			:"%ebx","%ecx","%edx");  // clobbered registers
	
	printf("eax = %X\n", eax);

	uint32_t cpu_signature = eax & SIGNATURE_MASK;
    	#ifdef POWER_DEBUG
        	printf("CPU signature: %x\n", cpu_signature); fflush(stdout);
    	#endif
	if (vendor == 1 || cpu_signature == SANDYBRIDGE_E || cpu_signature == IVYBRIDGE_E || cpu_signature == BROADWELL_E) {
		#ifdef POWER_DEBUG
			printf("PP1 measure not compatible for CPU signature: %x\n", cpu_signature); fflush(stdout);
		#endif
		return false;
	}
	return true;
}

void Rapl::open_msr(int socket, int cpuCore) {
	std::stringstream filename_stream;
	filename_stream << "/dev/cpu/" << cpuCore << "/msr";
	fd[socket] = open(filename_stream.str().c_str(), O_RDONLY);
	if (fd[socket] < 0) {
		if ( errno == ENXIO) {
			fprintf(stderr, "rdmsr: No CPU %d\n", cpuCore);
			exit(2);
		} else if ( errno == EIO) {
			fprintf(stderr, "rdmsr: CPU %d doesn't support MSRs\n", cpuCore);
			exit(3);
		} else {
			perror("rdmsr:open");
			fprintf(stderr, "Trying to open %s\n",
					filename_stream.str().c_str());
			exit(127);
		}
	}
}

uint64_t Rapl::read_msr(int socket, uint32_t msr_offset) {
	uint64_t data;
	if (pread(fd[socket], &data, sizeof(data), msr_offset) != sizeof(data)) {
		perror("read_msr():pread");
		exit(127);
	}
	return data;
}

void Rapl::sample(){
	for (int i=0; i<n_sockets; i++){
		sample(i);
	}
}
void Rapl::sample(int socket) {
	uint32_t max_int = ~((uint32_t) 0);

	if (vendor==0) {
		next_state[socket]->pkg = read_msr(socket, MSR_PKG_ENERGY_STATUS) & max_int;
		next_state[socket]->pp0 = read_msr(socket, MSR_PP0_ENERGY_STATUS) & max_int;
		if (pp1_supported) {
			next_state[socket]->pp1 = read_msr(socket, MSR_PP1_ENERGY_STATUS) & max_int;
			next_state[socket]->dram = 0;
		} else {
			next_state[socket]->pp1 = 0;
			next_state[socket]->dram = read_msr(socket, MSR_DRAM_ENERGY_STATUS) & max_int;
		}
	} else if (vendor==1) {
		uint64_t val = read_msr(socket, AMD_MSR_PACKAGE_ENERGY) & max_int;
		next_state[socket]->pkg = val;
		next_state[socket]->pp0 = 0;
		next_state[socket]->pp1 = 0;
		next_state[socket]->dram = 0;
	}

	gettimeofday(&(next_state[socket]->tsc), NULL);


	// Update running total
	running_total[socket]->pkg += energy_delta(current_state[socket]->pkg, next_state[socket]->pkg);
	running_total[socket]->pp0 += energy_delta(current_state[socket]->pp0, next_state[socket]->pp0);
	running_total[socket]->pp1 += energy_delta(current_state[socket]->pp0, next_state[socket]->pp0);
	running_total[socket]->dram += energy_delta(current_state[socket]->dram, next_state[socket]->dram);

	// Rotate states
	rapl_state_t *pprev_state = prev_state[socket];
	prev_state[socket] = current_state[socket];
	current_state[socket] = next_state[socket];
	next_state[socket] = pprev_state;
}

double Rapl::time_delta(struct timeval *begin, struct timeval *end) {
        return (end->tv_sec - begin->tv_sec)
                + ((end->tv_usec - begin->tv_usec)/1000000.0);
}

double Rapl::power(uint64_t before, uint64_t after, double time_delta) {
	if (time_delta == 0.0f || time_delta == -0.0f) { return 0.0; }
	double energy = energy_units * ((double) energy_delta(before,after));
	return energy / time_delta;
}

uint64_t Rapl::energy_delta(uint64_t before, uint64_t after) {
	uint64_t max_int = ~((uint32_t) 0);
	uint64_t eng_delta = after - before;

	// Check for rollovers
	if (before > after) {
		eng_delta = after + (max_int - before);
	}

	return eng_delta;
}

double Rapl::pkg_current_power() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		double t = time_delta(&(prev_state[i]->tsc), &(current_state[i]->tsc));
		double pp = power(prev_state[i]->pkg, current_state[i]->pkg, t);
		p+=pp;
	}
	return p;
}

double Rapl::pp0_current_power() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		double t = time_delta(&(prev_state[i]->tsc), &(current_state[i]->tsc));
		p+= power(prev_state[i]->pp0, current_state[i]->pp0, t);
	}
	return p;
}

double Rapl::pp1_current_power() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		double t = time_delta(&(prev_state[i]->tsc), &(current_state[i]->tsc));
		p += power(prev_state[i]->pp1, current_state[i]->pp1, t);
	}
}

double Rapl::dram_current_power() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		double t = time_delta(&(prev_state[i]->tsc), &(current_state[i]->tsc));
		p+=power(prev_state[i]->dram, current_state[i]->dram, t);
	}
	return p;
}

double Rapl::pkg_average_power() {
	return pkg_total_energy() / total_time();
}

double Rapl::pp0_average_power() {
	return pp0_total_energy() / total_time();
}

double Rapl::pp1_average_power() {
	return pp1_total_energy() / total_time();
}

double Rapl::dram_average_power() {
	return dram_total_energy() / total_time();
}

double Rapl::pkg_total_energy() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		p += energy_units * ((double) running_total[i]->pkg);
	}
	return p;
}

double Rapl::pp0_total_energy() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		p += energy_units * ((double) running_total[i]->pp0);
	}
	return p;
}

double Rapl::pp1_total_energy() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		p += energy_units * ((double) running_total[i]->pp1);
	}
	return p;
}

double Rapl::dram_total_energy() {
	double p = 0.0;
	for (int i=0; i<n_sockets; i++){
		p += energy_units * ((double) running_total[i]->dram);
	}
	return p;
}

double Rapl::total_time() {
	return time_delta(&(running_total[0]->tsc), &(current_state[0]->tsc));
}

double Rapl::current_time() {
	return time_delta(&(prev_state[0]->tsc), &(current_state[0]->tsc));
}

int Rapl::get_n_sockets(){
    FILE *fp;
    char line[MAX_LINE];
    int cpu[MAX_CPU];
    int i, n, id, count;

    // initialize cpu array to -1
    for (i = 0; i < MAX_CPU; i++) {
        cpu[i] = -1;
    }

    // open the file
    fp = fopen("/sys/devices/system/cpu/online", "r");
    if (fp == NULL) {
        perror("fopen");
        exit(EXIT_FAILURE);
    }

    // read the first line
    if (fgets(line, MAX_LINE, fp) == NULL) {
        perror("fgets");
        exit(EXIT_FAILURE);
    }

    // close the file
    fclose(fp);

    // parse the line and store the online cpu numbers in cpu array
    // This assumes that cpus are listed as "0-3,4-8,3-5"
    n = 0; // number of online cpus
    char *token = strtok(line, ",");
    while (token != NULL) {
        if (strchr(token, '-') != NULL) {
            // range of cpus
            int start, end;
            sscanf(token, "%d-%d", &start, &end);
            for (i = start; i <= end; i++) {
                cpu[n++] = i;
            }
        } else {
            // single cpu
            cpu[n++] = atoi(token);
        }
        token = strtok(NULL, ",");
    }

    // count the number of unique physical ids for online cpus
    count = 0; // number of sockets
    int curr_id = 0;
    for (i=0; i<MAX_SOCKETS; i++){
    	sockets[i] = -1;
	first_lcoreid[i] = -1;
    }
    for (i = 0; i < n; i++) {
        // construct the file name for each cpu
        char filename[MAX_LINE];
        sprintf(filename, "/sys/devices/system/cpu/cpu%d/topology/physical_package_id", cpu[i]);

        // open the file
        fp = fopen(filename, "r");
        if (fp == NULL) {
            perror("fopen");
            exit(EXIT_FAILURE);
        }

        // read the physical id
        if (fscanf(fp, "%d", &id) != 1) {
            perror("fscanf");
            exit(EXIT_FAILURE);
        }

        // close the file
        fclose(fp);
	if (sockets[id] == -1){
		sockets[id] = 1;
		first_lcoreid[id] = i;
	}
    }


    count = 0;
    for (i=0; i<MAX_SOCKETS; i++){
	    if (sockets[i] == 1){
	    	count++;
	    }
    }

    // print the result
    printf("Number of sockets: %d\n", count);

    return count;

}
