#pragma once

#include "middleware_utils.h"
#include "vpss_helper.h"
#include "cvi_comm.h"

#define MODEL_SCALE 0.0039216
#define MODEL_MEAN 0.0
#define MODEL_THRESH 0.2 
#define MODEL_NMS_THRESH 0.2
#define MODEL_SIZE 640

CVI_S32 get_middleware_config(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig);
void SampleHandleSig(CVI_S32 signo);
