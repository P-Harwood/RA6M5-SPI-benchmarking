
#ifndef LOG_H_
#define LOG_H_

#include <stdint.h>
#include <string.h>

#include "../thread_details.h"


#define log_name_size 8


typedef enum log_type_t {Live, SnapShot} log_type_t;


typedef struct status_log_data_t {
    uint32_t errors;
    uint32_t actions;
    uint32_t overruns;
}status_log_data_t;

typedef struct status_logs_t {
    char log_name[log_name_size];
    status_log_data_t data;
} status_logs_t;



extern volatile status_logs_t *thread_logs[thread_log_count];
extern volatile status_logs_t snapshot_logs[thread_log_count];


inline void clear_status_log(status_logs_t* to_wipe){
    
    to_wipe->data.errors     = 0;
    to_wipe->data.actions    = 0;
    to_wipe->data.overruns   = 0;
}

inline void status_log_dump(status_logs_t to_print){

    printk( "\n ==== === Thread Performance Log === ===\n" \
            " Thread Name:      %.*s\n"
            " Thread Errors:    %d \n"
            " Thread actions:    %d \n"
            " Thread Overruns:    %d \n\n",
            log_name_size, to_print.log_name,
            to_print.data.errors,
            to_print.data.actions,
            to_print.data.overruns);
}

inline void dump_log(log_type_t log_type){
    if (log_type == Live){
        for(int index = 0; index < thread_log_count; index++){
                status_log_dump(*(thread_logs[index]));
        }
    }else if((log_type) == SnapShot){
        for(int index = 0; index < thread_log_count; index++){
                status_log_dump(snapshot_logs[index]);
            }
    }

}

static inline void update_snapshots(void){
    for(int index = 0 ; index < thread_log_count; index++){
        snapshot_logs[index] = *(thread_logs[index]);
    }
}

static inline void clear_logs(void)
{
    /* Clear the snapshot logs*/
    memset(snapshot_logs, 0, sizeof(snapshot_logs));
    
    /* Clear the thread data, and leave the name*/
    for(int index = 0; index < thread_log_count; index++){
        memset(&(thread_logs[index]->data), 0, sizeof(status_log_data_t));
    }
}


#endif