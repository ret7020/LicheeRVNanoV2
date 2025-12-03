#pragma once

#include <pthread.h>
#include <stdio.h>
#include <cvi_comm.h>
#include <rtsp.h>
#include <sample_comm.h>
#include "sample_utils.h"
#include "vi_vo_utils.h"
#include "middleware_utils.h"
#include "vpss_helper.h"

typedef struct {
    SAMPLE_TDL_MW_CONTEXT *pstMWContext;
    cvitdl_service_handle_t stServiceHandle;

    const char **class_names;   // pointer to array of strings
    int (*class_colors)[3];     // pointer to [][3] ints
    int class_cnt;
} SAMPLE_TDL_VENC_THREAD_ARG_S;

void *run_venc(void *args);