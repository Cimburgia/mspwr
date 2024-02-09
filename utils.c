#include "utils.h"

static char *std_complex_freq_chankeys[] = {"ECPU", "PCPU"};
static char *std_core_freq_chankeys[] = {"ECPU", "PCPU"};
static char *std_complex_pwr_chankeys[] = {"ECPU", "PCPU"};
static char *std_core_pwr_chankeys[] = {"ECPU", "PCPU"};

static char *promax_complex_freq_chankeys[] = {"ECPU", "PCPU", "PCPU1"};
static char *promax_core_freq_chankeys[] = {"ECPU0", "PCPU0", "PCPU1"};
static char *promax_complex_pwr_chankeys[] = {"EACC_CPU", "PACC0_CPU", "PACC1_CPU"};
static char *promax_core_pwr_chankeys[] = {"EACC_CPU", "PACC0_CPU", "PACC1_CPU"};

static char *ultra_complex_freq_chankeys[] = {"DIE_0_ECPU", "DIE_1_ECPU", "DIE_0_PCPU", "DIE_0_PCPU1", "DIE_1_PCPU", "DIE_1_PCPU1"};
static char *ultra_core_freq_chankeys[] = {"DIE_0_ECPU_CPU", "DIE_1_ECPU_CPU", "DIE_0_PCPU_CPU", "DIE_0_PCPU1_CPU", "DIE_1_PCPU_CPU", "DIE_1_PCPU1_CPU"};
static char *ultra_complex_pwr_chankeys[] = {"DIE_0_EACC_CPU", "DIE_1_EACC_CPU", "DIE_0_PACC0_CPU", "DIE_0_PACC1_CPU", "DIE_1_PACC0_CPU", "DIE_1_PACC1_CPU"};
static char *ultra_core_pwr_chankeys[] = {"DIE_0_EACC_CPU", "DIE_1_EACC_CPU", "DIE_0_PACC0_CPU", "DIE_0_PACC1_CPU", "DIE_1_PACC0_CPU", "DIE_1_PACC1_CPU"};

static unit_data *unit;

#define TIME_BETWEEN_MEASUREMENTS 500000000L // 50 millisecond, max is 1 second
#define N_SECONDS .5
/**
 * Initialize the channel subscriptions so we can continue to sample over them throughout the
 * experiments. 
*/
void init_unit_data(){
    // Allocate memory for unit data
    unit = malloc(sizeof(unit_data));

    // Gather silicon name
    size_t len = 32;
    char* cputype = malloc(len);
    sysctlbyname("machdep.cpu.brand_string", cputype, &len, NULL, 0);
    for (int i = 0; i < strlen(cputype); i++) {
        cputype[i] = tolower(cputype[i]);
    }
    unit->silicon_name = cputype;

    // Get number of cores based on silicon for ecpu and pcpu
    get_core_nums();

    // Get DVFS frequency states
    get_dvfs_table();

    // Assign channel keys from name based on silicon type
    if (strstr(unit->silicon_name, "pro") || strstr(unit->silicon_name, "max")){
        unit->complex_freq_chankeys = promax_complex_freq_chankeys;
        unit->core_freq_chankeys = promax_core_freq_chankeys;
        unit->complex_pwr_chankeys = promax_complex_pwr_chankeys;
        unit->core_pwr_chankeys = promax_core_pwr_chankeys;
    } else if (strstr(unit->silicon_name, "ultra")){
        unit->complex_freq_chankeys = ultra_complex_freq_chankeys;
        unit->core_freq_chankeys = ultra_core_freq_chankeys;
        unit->complex_pwr_chankeys = ultra_complex_pwr_chankeys;
        unit->core_pwr_chankeys = ultra_core_pwr_chankeys;
    } else{
        unit->complex_freq_chankeys = std_complex_freq_chankeys;
        unit->core_freq_chankeys = std_core_freq_chankeys;
        unit->complex_pwr_chankeys = std_complex_pwr_chankeys;
        unit->core_pwr_chankeys = std_core_pwr_chankeys;
    }
    get_labels();

    //Initialize channels
    unit->cpu_chann = IOReportCopyChannelsInGroup(CFSTR("CPU Stats"), 0, 0, 0, 0);
    unit->energy_chann = IOReportCopyChannelsInGroup(CFSTR("Energy Model"), 0, 0, 0, 0);

    // Create subscription
    unit->cpu_sub  = IOReportCreateSubscription(NULL, unit->cpu_chann, &unit->cpu_sub_chann, 0, 0);
    unit->pwr_sub  = IOReportCreateSubscription(NULL, unit->energy_chann, &unit->pwr_sub_chann, 0, 0);
    CFRelease(unit->cpu_chann);
    CFRelease(unit->energy_chann);
}
/**
 *  
*/
sample_deltas *sample() {
    // Take samples
    CFDictionaryRef cpusamp_a  = IOReportCreateSamples(unit->cpu_sub, unit->cpu_sub_chann, NULL);
    nanosleep((const struct timespec[]){{0, TIME_BETWEEN_MEASUREMENTS}}, NULL);
    CFDictionaryRef cpusamp_b  = IOReportCreateSamples(unit->cpu_sub, unit->cpu_sub_chann, NULL);
    CFDictionaryRef pwrsamp_a  = IOReportCreateSamples(unit->pwr_sub, unit->pwr_sub_chann, NULL);
    nanosleep((const struct timespec[]){{0, TIME_BETWEEN_MEASUREMENTS}}, NULL);
    CFDictionaryRef pwrsamp_b  = IOReportCreateSamples(unit->pwr_sub, unit->pwr_sub_chann, NULL);

    // Compute deltas
    CFDictionaryRef cpu_delta  = IOReportCreateSamplesDelta(cpusamp_a, cpusamp_b, NULL);
    CFDictionaryRef pwr_delta  = IOReportCreateSamplesDelta(pwrsamp_a, pwrsamp_b, NULL);

    // Done with these
    CFRelease(cpusamp_a);
    CFRelease(cpusamp_b);
    CFRelease(pwrsamp_a);
    CFRelease(pwrsamp_b);

    sample_deltas *deltas = (sample_deltas *) malloc(sizeof(sample_deltas));
    deltas->cpu_delta = cpu_delta;
    deltas->pwr_delta = pwr_delta;
    return deltas;
}

/*
 * This updates cpu_data with all core/complex information
*/
void get_state_residencies(CFDictionaryRef cpu_delta, cpu_data *data){
    __block int i = 0;
    data->residencies = malloc(unit->n_samples*sizeof(uint64_t*));
    data->num_dvfs_states = malloc(unit->n_samples*sizeof(int));

    IOReportIterate(cpu_delta, ^int(IOReportSampleRef sample) {
        // Check sample group and only expand if CPU Stats
        CFStringRef group = IOReportChannelGetGroup(sample);
        if (CFStringCompare(group, CFSTR("CPU Stats"), 0) == kCFCompareEqualTo){
            // Get subgroup (core or complex) and chann_name (core/complex id)
            CFStringRef subgroup    = IOReportChannelGetSubGroup(sample);
            CFStringRef chann_name  = IOReportChannelGetChannelName(sample);
            // Only take these
            if (CFStringCompare(subgroup, CFSTR("CPU Core Performance States"), 0) == kCFCompareEqualTo || 
                CFStringCompare(subgroup, CFSTR("CPU Complex Performance States"), 0) == kCFCompareEqualTo){
                if (i < unit->n_samples){
                    CFStringRef id_str = CFStringCreateWithCString(NULL, unit->frequency_labels[i], kCFStringEncodingUTF8);
                    if (CFStringHasPrefix(chann_name, id_str)){
                        int sample_state_ct = IOReportStateGetCount(sample);
                        int res_idx = 0;
                        // Not the best, but get actual state count by comparing sample count since count from sample includes IDLE and DOWN...
                        int sample_ct = (unit->samples_per_state[1] > sample_state_ct) ? unit->samples_per_state[0] : unit->samples_per_state[1];
                        data->residencies[i] = malloc(sample_ct*sizeof(uint64_t));
                        data->num_dvfs_states[i] = sample_ct;
                        for (int ii = 0; ii < sample_state_ct; ii++) {
                            CFStringRef idx_name    = IOReportStateGetNameForIndex(sample, ii);
                            uint64_t residency = IOReportStateGetResidency(sample, ii);
                            // Dont collect idle and down for now
                            if (CFStringCompare(idx_name, CFSTR("IDLE"), 0) != kCFCompareEqualTo &&
                                CFStringCompare(idx_name, CFSTR("DOWN"), 0) != kCFCompareEqualTo){
                                data->residencies[i][res_idx] = residency;
                                res_idx++;
                            }
                        }
                        i++;
                    }
                }
            }
        }
        return kIOReportIterOk;
    });
}

/*
    Averages the active frequency of CPU core of complex based on residencies 
    at DVFS states from get_state_residencies(). Seperates E and P cores/complexes
    DVFS table is hardcoded and will change depending on the system
*/
void get_frequency(CFDictionaryRef cpu_delta, cpu_data *data){
    // Caller responsible to deallocate
    data->frequencies = malloc(unit->n_samples * sizeof(uint64_t));

    // Take sample and fill in residency table
    get_state_residencies(cpu_delta, data);
    
    // Loop through residency table and average for each complex/core
    for (int i = 0; i < unit->n_samples; i++){
        // Get DVFS states
        uint64_t num_states = data->num_dvfs_states[i];
        uint64_t sum = 0;
        float freq = 0;
        // Check which freq states to get
        int freq_idx = (num_states > data->num_dvfs_states[0]) ? 1 : 0;
        for (int ii = 0; ii < num_states; ii++){
            sum += data->residencies[i][ii];
        }
        
        // Take average
        for (int ii = 0; ii < num_states; ii++){
            float percent = (float)data->residencies[i][ii]/sum;
            freq += (percent*unit->frequency_states[freq_idx][ii]);
        }
        // Save to data struct
        data->frequencies[i] = freq;
    }
    if (cpu_delta != NULL) CFRelease(cpu_delta);
}

/*
 * Takes a sample and a core id and returns cpu power
 * 
*/
void get_power(CFDictionaryRef pwr_sample, cpu_data *data){
    // Init power array
    data->pwr = malloc(unit->n_samples * sizeof(float));
    __block int i = 0;
    
    // Iterate through cpu energy core samples
    IOReportIterate(pwr_sample, ^int(IOReportSampleRef sample) {
        CFStringRef chann_name  = IOReportChannelGetChannelName(sample);
        CFStringRef group       = IOReportChannelGetGroup(sample);
        long        value       = IOReportSimpleGetIntegerValue(sample, 0);
        //CFStringRef units =  IOReportChannelGetUnitLabel(sample);
       
        if (CFStringCompare(group, CFSTR("Energy Model"), 0) == kCFCompareEqualTo) {
            if (i < unit->n_samples){
                CFStringRef core_id_str = CFStringCreateWithCString(NULL, unit->power_labels[i], kCFStringEncodingUTF8);
                if (CFStringCompare(chann_name, core_id_str, 0) == kCFCompareEqualTo){
                    data->pwr[i] = ((float)value) / N_SECONDS;
                    i++;
                }
            }
        } 
        return kIOReportIterOk;
    });
    //if (pwr_sample != NULL) CFRelease(pwr_sample);
}

void get_sram_power(CFDictionaryRef pwr_sample, cpu_data *data){
    // First screen for m2, m1 does not have sram power measurements
    if (!strstr(unit->silicon_name, "m2")){
        error(1, "Hardware version does not support SRAM measurements");
    }
     // Init power array
    data->sram_pwr = malloc(unit->n_samples * sizeof(float));
    __block int i = 0;
    
    // Iterate through cpu energy core samples
    IOReportIterate(pwr_sample, ^int(IOReportSampleRef sample) {
        CFStringRef chann_name  = IOReportChannelGetChannelName(sample);
        CFStringRef group       = IOReportChannelGetGroup(sample);
        long        value       = IOReportSimpleGetIntegerValue(sample, 0);
        //CFStringRef units =  IOReportChannelGetUnitLabel(sample);
       
        if (CFStringCompare(group, CFSTR("Energy Model"), 0) == kCFCompareEqualTo) {
            if (i < unit->n_samples){
                CFStringRef core_id_str = CFStringCreateWithCString(NULL, unit->sram_power_labels[i], kCFStringEncodingUTF8);
                if (CFStringCompare(chann_name, core_id_str, 0) == kCFCompareEqualTo){
                    data->sram_pwr[i] = ((float)value) / N_SECONDS;
                    i++;
                }
            }
        } 
        return kIOReportIterOk;
    });
    if (pwr_sample != NULL) CFRelease(pwr_sample);
}

/**
 * Returns the number of cores per cluster. Each silicon type should have
 * 1-2 clusters of E cores and 1-4 clusters of P cores
*/
void get_core_nums(){    
    io_registry_entry_t entry;
    io_iterator_t iter;
    CFMutableDictionaryRef service;
    int total_samples = 0;

    // Check OS version
    mach_port_t port;
    if (__builtin_available(macOS 12.0, *))
        port = kIOMainPortDefault;
    else
        port = kIOMasterPortDefault;

    if (!(service = IOServiceMatching("AppleARMIODevice")))
        error(1, "Failed to find AppleARMIODevice service in IORegistry");
    if (!(IOServiceGetMatchingServices(port, service, &iter) == kIOReturnSuccess))
        error(1, "Failed to access AppleARMIODevice service in IORegistry");

    const void* data = nil;
    // Iterate to clusters sample
    while ((entry = IOIteratorNext(iter)) != IO_OBJECT_NULL){    
        const void* data = IORegistryEntryCreateCFProperty(entry, CFSTR("clusters"), kCFAllocatorDefault, 0);
        if (data != nil) {
            const unsigned char* databytes = CFDataGetBytePtr(data);
            // Byte size of CFData
            int len = CFDataGetLength(data);
            // Get number of data points
            unit->n_clusters = len/4;
            unit->per_cluster_n_cores = malloc(len/4*sizeof(int));
            // Every 4th index is cluster core count data
            for (int ii = 0; ii < len; ii += 4) {
                // Save to unit data
                int n_cores = *(int*)(databytes + ii);
                unit->per_cluster_n_cores[ii/4] = n_cores;
                // Add in number of cores per complex
                total_samples += n_cores;
                // Add extra for complex sample
                total_samples++;
            }
            // Save to unit data
            unit->n_samples = total_samples;
            break;
        }
        
    }
    IOObjectRelease(entry);
    IOObjectRelease(iter);
}

/**
 * Returns the frequency states for both the E core (voltage-states1-sram) and P cores (voltage-states5-sram)
 * Higher-end silicon will also use voltage-states13-sram
*/
void get_dvfs_table(){
    io_registry_entry_t entry;
    io_iterator_t iter;
    CFMutableDictionaryRef service;

    // Check OS version
    mach_port_t port;
    if (__builtin_available(macOS 12.0, *))
        port = kIOMainPortDefault;
    else
        port = kIOMasterPortDefault;

    const void* data = nil;
    if (!(service = IOServiceMatching("AppleARMIODevice")))
        error(1, "Failed to find AppleARMIODevice service in IORegistry");
    if (!(IOServiceGetMatchingServices(port, service, &iter) == kIOReturnSuccess))
        error(1, "Failed to access AppleARMIODevice service in IORegistry");
    
    // Iterate through entries until voltage states are found, then save them for E and P cores
    while ((entry = IOIteratorNext(iter)) != IO_OBJECT_NULL){
        if (IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states1-sram"), kCFAllocatorDefault, 0) != nil) {
            for (int i = 0; i < 2; i++){
                const void* data = nil;
                // This will return a CFData object for E core or P core
                switch(i) {
                    case 0: data = IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states1-sram"), kCFAllocatorDefault, 0); break;
                    case 1: data = IORegistryEntryCreateCFProperty(entry, CFSTR("voltage-states5-sram"), kCFAllocatorDefault, 0); break;
                }
                const UInt8 *databytes = CFDataGetBytePtr(data);
                // Byte size of CFData
                int len = CFDataGetLength(data);
                unit->samples_per_state[i] = (len /8);
                uint32_t *f_states = malloc((len/8) * sizeof(uint32_t));
                // Extract each state
                for (int ii = 0; ii < len - 4; ii += 8) {
                    f_states[ii / 8] = *(uint32_t*)(databytes + ii) * 1e-6;
                }
                unit->frequency_states[i] = f_states;
            }
            break;
        }
    }
    IOObjectRelease(entry);
    IOObjectRelease(iter);
}

void get_labels(){
    char **pwr_sample_labels = malloc(unit->n_samples*sizeof(char*));
    char **freq_sample_labels = malloc(unit->n_samples*sizeof(char*));
    char **sram_sample_labels = malloc(unit->n_samples*sizeof(char*));
    int idx = 0;
    for (int i = 0; i < unit->n_clusters; i++) {
        for (int ii = 0; ii < unit->per_cluster_n_cores[i]; ii++) {
            int pwr_len = strlen(unit->core_pwr_chankeys[i]);
            int freq_len = strlen(unit->core_freq_chankeys[i]);
            int ii_len = snprintf(NULL, 0, "%d", ii);

            char *pwr_core_lab = malloc(pwr_len + ii_len + 1);
            char *freq_core_lab = malloc(freq_len + ii_len + 1);
            char *sram_core_lab = malloc(freq_len + ii_len + 6);

            strcpy(pwr_core_lab, unit->core_pwr_chankeys[i]);
            strcpy(sram_core_lab, unit->core_pwr_chankeys[i]);
            strcpy(freq_core_lab, unit->core_freq_chankeys[i]);

            sprintf(pwr_core_lab + pwr_len, "%d", ii);
            sprintf(sram_core_lab + pwr_len, "%d_SRAM", ii);
            sprintf(freq_core_lab + freq_len, "%d", ii);

            pwr_sample_labels[idx] = pwr_core_lab;
            sram_sample_labels[idx] = sram_core_lab;
            freq_sample_labels[idx] = freq_core_lab;
            idx++;
        }
    }
    // Get labels for complex
    for (int i = 0; i < unit->n_clusters; i++){
        int pwr_len = strlen(unit->complex_pwr_chankeys[i]);
        int freq_len = strlen(unit->complex_freq_chankeys[i]);

        char *pwr_complex_lab = malloc(pwr_len + 1);
        char *freq_complex_lab = malloc(freq_len + 1);
        char *sram_core_lab = malloc(pwr_len + 6);

        strcpy(pwr_complex_lab, unit->complex_pwr_chankeys[i]);
        strcpy(freq_complex_lab, unit->complex_freq_chankeys[i]);
        strcpy(sram_core_lab, unit->complex_pwr_chankeys[i]);

        sprintf(sram_core_lab + pwr_len, "_SRAM");

        pwr_sample_labels[idx] = pwr_complex_lab;
        freq_sample_labels[idx] = freq_complex_lab;
        sram_sample_labels[idx] = sram_core_lab;
        idx++;
    }
    unit->power_labels = pwr_sample_labels;
    unit->frequency_labels = freq_sample_labels;
    unit->sram_power_labels = sram_sample_labels;
}

/**
 * Function to report errors during sample collection and init
*/
void error(int exitcode, const char* format, ...) {
    va_list args;
    fprintf(stderr, "\e[1m%s:\033[0;31m error:\033[0m\e[0m ", getprogname());
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fprintf(stderr, "\n");
    exit(exitcode);
}

/**
 * Destructor for the unit data. Call at end of program after sampling is finished.
*/
void unit_data_destroy(unit_data *data){
    free(data->silicon_name);
    free(data->per_cluster_n_cores);
    
    free(data->frequency_states[0]);
    free(data->frequency_states[1]);

    for (int i = 0; i < data->n_samples; i++){
        free(data->frequency_labels[i]);
        free(data->power_labels[i]);
    }
    free(data->frequency_labels);
    free(data->power_labels);
    free(data);
}

/**
 *  Destructor for cpu sample data. Call after data is saved
*/
void cpu_data_destroy(cpu_data *data){
    for (int i = 0; i < unit->n_samples; i++){
        free(data->residencies[i]);
    }
    free(data->residencies);
    free(data->frequencies);
    free(data->pwr);
    if (data->sram_pwr != NULL){
        free(data->sram_pwr);
    }

    free(data);
}

int main(int argc, char* argv[]) {
    init_unit_data();
    cpu_data *data = malloc(sizeof(cpu_data));
    sample_deltas *deltas = sample();
    get_power(deltas->pwr_delta, data);
    get_sram_power(deltas->pwr_delta, data);
    get_frequency(deltas->cpu_delta, data);

    // Print silicon name
    printf("%s\n", unit->silicon_name);

    // Test all - Print
    int nsamp = unit->n_samples;

    // Print labels
    printf("Freq Labs\n");
    for(int i = 0; i < nsamp; i++){
        printf("%s\n", unit->frequency_labels[i]);
    }
    printf("\n");

    printf("Pwr Labs\n");
    for(int i = 0; i < nsamp; i++){
        printf("%s\n", unit->power_labels[i]);
    }
    printf("\n");

    printf("SRAM Labs\n");
    for(int i = 0; i < nsamp; i++){
        printf("%s\n", unit->sram_power_labels[i]);
    }
    printf("\n");

    // Print freq states
    printf("Freq States\n");
    for(int i = 0; i < 2; i++){
        printf("%d\n", unit->samples_per_state[i]);
        for (int ii = 0; ii < unit->samples_per_state[i]; ii++){
            printf("%u\n", unit->frequency_states[i][ii]);
        }
    }
    printf("\n");

    // Print power in mJ
    printf("Pwr (mJ)\n");
    for(int i = 0; i < nsamp; i++){
        printf("%f\n", data->pwr[i]);
    }
    printf("\n");

    // Print sram power in mJ
    printf("SRAM Pwr (mJ)\n");
    for(int i = 0; i < nsamp; i++){
        printf("%f\n", data->sram_pwr[i]);
    }
    printf("\n");

    // Print freq
    printf("Freq\n");
    for(int i = 0; i < nsamp; i++){
        printf("%lld\n", data->frequencies[i]);
    }
    printf("\n");
    cpu_data_destroy(data);
    unit_data_destroy(unit);
}
