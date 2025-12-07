#include <signal.h>
#include <stdio.h>
#include <stdlib.h>

#include "cvi_tdl.h"
#include "middleware_utils.h"
#include "mw_setup.h"
#include "threads.h"


int main(int argc, char *argv[]) {
    if (argc < 5) {
        printf("Usage: <model_path> <class_cnt> <thresh> <nms_thresh>\n");
        return -1;
    }

    char *arg_model_path = argv[1];
    int arg_class_cnt = atoi(argv[2]);
    float arg_object_threshold = atof(argv[3]);
    float arg_nms_threshold = atof(argv[4]);

    signal(SIGINT, SampleHandleSig);
    signal(SIGTERM, SampleHandleSig);

    const char *class_names[5] = {"0", "1", "2", "3", "4"};
    int class_colors[5][3] = {
        {0, 255, 255},
        {255, 0, 255},
        {0, 255, 0},
        {255, 255, 0},
        {128, 200, 0}
    };

    SAMPLE_TDL_MW_CONFIG_S stMWConfig = {};
    SAMPLE_TDL_MW_CONTEXT stMWContext = {};
    cvitdl_handle_t stTDLHandle = NULL;
    cvitdl_service_handle_t stServiceHandle = NULL;

    CVI_S32 s32Ret = CVI_SUCCESS;
    int exitCode = 0;
    bool mw_inited = false;


    // Init MW, TDL and RTSP
    do {
        s32Ret = get_middleware_config(&stMWConfig);
        if (s32Ret != CVI_SUCCESS) {
            printf("get middleware configuration failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }

        s32Ret = SAMPLE_TDL_Init_WM(&stMWConfig, &stMWContext);
        if (s32Ret != CVI_SUCCESS) {
            printf("init middleware failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }
        mw_inited = true;

        s32Ret = CVI_TDL_CreateHandle2(&stTDLHandle, 1, 0);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_TDL_CreateHandle2 failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }

        s32Ret = CVI_TDL_SetVBPool(stTDLHandle, 0, 2);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_TDL_SetVBPool failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }

        CVI_TDL_SetVpssTimeout(stTDLHandle, 1000);

        s32Ret = CVI_TDL_Service_CreateHandle(&stServiceHandle, stTDLHandle);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_TDL_Service_CreateHandle failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }

        InputPreParam preprocess_cfg = CVI_TDL_GetPreParam(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
        for (int i = 0; i < 3; i++) {
            preprocess_cfg.factor[i] = MODEL_SCALE;
            preprocess_cfg.mean[i] = MODEL_MEAN;
        }
        preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;

        CVI_S32 ret = CVI_TDL_SetPreParam(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, preprocess_cfg);
        if (ret != CVI_SUCCESS) {
            printf("CVI_TDL_SetPreParam failed! ret=%x\n", ret);
            exitCode = -1;
            break;
        }

        cvtdl_det_algo_param_t yolov8_param =
            CVI_TDL_GetDetectionAlgoParam(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION);
        yolov8_param.cls = arg_class_cnt;

        ret = CVI_TDL_SetDetectionAlgoParam(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, yolov8_param);
        if (ret != CVI_SUCCESS) {
            printf("CVI_TDL_SetDetectionAlgoParam failed! ret=%x\n", ret);
            exitCode = -2;
            break;
        }

        CVI_TDL_SetModelThreshold(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, arg_object_threshold);
        CVI_TDL_SetModelNmsThreshold(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, arg_nms_threshold);

        s32Ret = CVI_TDL_OpenModel(stTDLHandle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION, arg_model_path);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_TDL_OpenModel failed! ret=%x\n", s32Ret);
            exitCode = -1;
            break;
        }

        pthread_t stVencThread, stTDLThread;

        SAMPLE_TDL_VENC_THREAD_ARG_S venc_args = {
            .pstMWContext = &stMWContext,
            .stServiceHandle = stServiceHandle,
            .class_names = class_names,
            .class_colors = class_colors,
            .class_cnt = arg_class_cnt,
        };

        SAMPLE_TDL_TDL_THREAD_ARG_S ai_args = {
            .stTDLHandle = stTDLHandle,
        };
        
        if (pthread_create(&stVencThread, NULL, run_venc, &venc_args) != 0) {
            printf("Failed to create VENC thread\n");
            exitCode = -1;
            break;
        }
        
        if (pthread_create(&stTDLThread, NULL, run_tdl_thread, &ai_args) != 0) {
            printf("Failed to create TDL thread\n");
            exitCode = -1;
            break;
        }

        pthread_join(stTDLThread, NULL);
        pthread_join(stVencThread, NULL);

    } while (0);

    if (stServiceHandle) {
        CVI_TDL_Service_DestroyHandle(stServiceHandle);
    }
    if (stTDLHandle) {
        CVI_TDL_DestroyHandle(stTDLHandle);
    }
    if (mw_inited) {
        SAMPLE_TDL_Destroy_MW(&stMWContext);
    }

    return exitCode;
}
