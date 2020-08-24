#include <hypercall.h>

typedef struct hc_time_adjust time_adjust_t;
typedef uint64_t global_time_t;

global_time_t get_global_time_fixed(u64 at_tsc);
global_time_t get_global_time(void);
int do_adjust_time(time_adjust_t *time_adjust);
void time_setup_internals(uint64_t freq);
