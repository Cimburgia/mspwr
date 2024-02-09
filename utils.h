#ifndef _UTILS_H
#define _UTILS_H

#include <IOKit/IOKitLib.h>
#include <CoreFoundation/CoreFoundation.h>
#include <sys/types.h>
#include <sys/sysctl.h>
#include <string.h>

enum {
    kIOReportIterOk,
    kIOReportIterFailed,
    kIOReportIterSkipped,
    kIOReportIterStop
};
typedef struct IOReportSubscriptionRef* IOReportSubscriptionRef;
typedef CFDictionaryRef IOReportSampleRef;

extern IOReportSubscriptionRef IOReportCreateSubscription(void* a, CFMutableDictionaryRef desiredChannels, CFMutableDictionaryRef* subbedChannels, uint64_t channel_id, CFTypeRef b);
extern CFDictionaryRef IOReportCreateSamples(IOReportSubscriptionRef iorsub, CFMutableDictionaryRef subbedChannels, CFTypeRef a);
extern CFDictionaryRef IOReportCreateSamplesDelta(CFDictionaryRef prev, CFDictionaryRef current, CFTypeRef a);

extern CFMutableDictionaryRef IOReportCopyChannelsInGroup(CFStringRef, CFStringRef, uint64_t, uint64_t, uint64_t);

typedef int (^ioreportiterateblock)(IOReportSampleRef ch);
extern void IOReportIterate(CFDictionaryRef samples, ioreportiterateblock);

extern int IOReportStateGetCount(CFDictionaryRef);
extern uint64_t IOReportStateGetResidency(CFDictionaryRef, int);
extern uint64_t IOReportArrayGetValueAtIndex(CFDictionaryRef, int);
extern long IOReportSimpleGetIntegerValue(CFDictionaryRef, int);
extern CFStringRef IOReportChannelGetChannelName(CFDictionaryRef);
extern CFStringRef IOReportChannelGetSubGroup(CFDictionaryRef);
extern CFStringRef IOReportStateGetNameForIndex(CFDictionaryRef, int);
extern CFStringRef IOReportChannelGetGroup(CFDictionaryRef);
extern CFStringRef IOReportChannelGetUnitLabel(CFDictionaryRef);

extern void IOReportMergeChannels(CFMutableDictionaryRef, CFMutableDictionaryRef, CFTypeRef);

typedef struct unit_data{
    char *silicon_name;             // Either Standard, ProMax, or Ultra/M1 or M2
    int n_clusters;                 // Total Clusters
    int n_samples;                  // Total Samples include per Complex (2) + Core (8-n) 
    int *per_cluster_n_cores;       // Ecore and Pcore(s)
    
    char **complex_freq_chankeys;  
    char **core_freq_chankeys;
    char **complex_pwr_chankeys; 
    char **core_pwr_chankeys;
    
    IOReportSubscriptionRef cpu_sub;
    CFMutableDictionaryRef cpu_sub_chann;
    CFMutableDictionaryRef cpu_chann;
    IOReportSubscriptionRef pwr_sub;
    CFMutableDictionaryRef pwr_sub_chann;
    CFMutableDictionaryRef energy_chann;

    uint32_t *frequency_states[2];    // DVFS States [Cluster][States]
    int samples_per_state[2];
    char **frequency_labels;
    char **power_labels;
    char **sram_power_labels;
} unit_data;

typedef struct cpu_data{
    int *num_dvfs_states;
    uint64_t **residencies;
    uint64_t *frequencies;
    float *pwr;
    float *sram_pwr;
} cpu_data;

typedef struct sample_deltas{
    CFDictionaryRef cpu_delta;
    CFDictionaryRef pwr_delta;
} sample_deltas;


void init_unit_data();
sample_deltas *sample();
void get_state_residencies(CFDictionaryRef cpu_delta, cpu_data *data);
void get_frequency(CFDictionaryRef cpu_delta, cpu_data *data);
void get_power(CFDictionaryRef pwr_sample, cpu_data *data);
void get_sram_power(CFDictionaryRef pwr_sample, cpu_data *data);
void get_core_nums();
void get_dvfs_table();
void get_labels();
void unit_data_destroy(unit_data *data);
void cpu_data_destroy(cpu_data *data);
void error(int exitcode, const char* format, ...);
#endif
