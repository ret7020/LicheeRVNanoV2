#pragma once

#include <stdbool.h>
#include <pthread.h>

#include "vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include "middleware_utils.h"

// Shared flag used by threads and main.
extern volatile bool g_exit_flag;

// Global object metadata shared between threads.
extern cvtdl_object_t g_stObjMeta;

// VENC thread args
typedef struct {
    SAMPLE_TDL_MW_CONTEXT *pstMWContext;
    cvitdl_service_handle_t stServiceHandle;

    const char **class_names;
    int (*class_colors)[3];
    int class_cnt;
} SAMPLE_TDL_VENC_THREAD_ARG_S;

// TDL thread args
typedef struct {
    cvitdl_handle_t stTDLHandle;
} SAMPLE_TDL_TDL_THREAD_ARG_S;

void *run_venc(void *args);
void *run_tdl_thread(void *args);
