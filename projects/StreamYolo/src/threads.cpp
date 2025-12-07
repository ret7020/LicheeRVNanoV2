#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "threads.h"
#include "sample_utils.h"
#include "vi_vo_utils.h"
#include "rtsp.h"
#include <sample_comm.h>
#include <core/utils/vpss_helper.h>

volatile bool g_exit_flag = false;

cvtdl_object_t g_stObjMeta = {0};
MUTEXAUTOLOCK_INIT(ResultMutex);

void *run_venc(void *args) {
    printf("Enter encoder thread\n");
    SAMPLE_TDL_VENC_THREAD_ARG_S *pstArgs = (SAMPLE_TDL_VENC_THREAD_ARG_S *)args;

    const char **class_names = pstArgs->class_names;
    int (*class_colors)[3]   = pstArgs->class_colors;
    int class_cnt            = pstArgs->class_cnt;

    VIDEO_FRAME_INFO_S stFrame;
    CVI_S32 s32Ret;

    while (!g_exit_flag) {
        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN0, &stFrame, 2000);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_VPSS_GetChnFrame chn0 failed with %#x\n", s32Ret);
            g_exit_flag = true;
            break;
        }

        cvtdl_object_t stObjMeta = {0};

        {
            MutexAutoLock(ResultMutex, lock);
            CVI_TDL_CopyObjectMeta(&g_stObjMeta, &stObjMeta);
        }

        for (uint32_t oid = 0; oid < stObjMeta.size; oid++) {
            int cls = stObjMeta.info[oid].classes;
            if (cls < 0 || cls >= class_cnt)
                continue;

            char name[256];
            sprintf(name, "%s: %.2f",
                    class_names[cls],
                    stObjMeta.info[oid].bbox.score);
            memcpy(stObjMeta.info[oid].name, name,
                   sizeof(stObjMeta.info[oid].name));
        }

        cvtdl_service_brush_t *brushes = new cvtdl_service_brush_t[stObjMeta.size];
        if (stObjMeta.size > 0) {
            if (!brushes) {
                printf("Failed to allocate brushes\n");
                CVI_TDL_Free(&stObjMeta);
                CVI_VPSS_ReleaseChnFrame(0, VPSS_CHN0, &stFrame);
                g_exit_flag = true;
                break;
            }

            for (size_t i = 0; i < stObjMeta.size; ++i) {
                int cls = stObjMeta.info[i].classes;
                if (cls < 0 || cls >= class_cnt)
                    continue;

                brushes[i].color.r = class_colors[cls][0];
                brushes[i].color.g = class_colors[cls][1];
                brushes[i].color.b = class_colors[cls][2];
                brushes[i].size = 4;
            }
        }

        CVI_S32 s32DrawRet = CVI_TDL_Service_ObjectDrawRect2(
            pstArgs->stServiceHandle, &stObjMeta, &stFrame, true, brushes);

        delete[] brushes;

        CVI_S32 s32SendRet = CVI_SUCCESS;
        if (s32DrawRet == CVI_TDL_SUCCESS) {
            s32SendRet = SAMPLE_TDL_Send_Frame_RTSP(&stFrame, pstArgs->pstMWContext);
        }

        CVI_TDL_Free(&stObjMeta);
        CVI_VPSS_ReleaseChnFrame(0, VPSS_CHN0, &stFrame);

        if (s32DrawRet != CVI_TDL_SUCCESS || s32SendRet != CVI_SUCCESS) {
            g_exit_flag = true;
        }
    }

    printf("Exit encoder thread\n");
    pthread_exit(NULL);
}

void *run_tdl_thread(void *args) {
    printf("Enter TDL thread\n");
    SAMPLE_TDL_TDL_THREAD_ARG_S *pstTDLArgs = (SAMPLE_TDL_TDL_THREAD_ARG_S *)args;

    VIDEO_FRAME_INFO_S stFrame;
    CVI_S32 s32Ret;
    uint32_t counter = 0;

    while (!g_exit_flag) {
        cvtdl_object_t stObjMeta = {0};
        bool frame_acquired = false;
        CVI_S32 tdlRet = CVI_TDL_SUCCESS;

        s32Ret = CVI_VPSS_GetChnFrame(0, VPSS_CHN1, &stFrame, 2000);
        if (s32Ret != CVI_SUCCESS) {
            printf("CVI_VPSS_GetChnFrame failed with %#x\n", s32Ret);
        } else {
            frame_acquired = true;

            struct timeval t0, t1;
            gettimeofday(&t0, NULL);

            tdlRet = CVI_TDL_Detection(
                pstTDLArgs->stTDLHandle,
                &stFrame,
                CVI_TDL_SUPPORTED_MODEL_YOLOV8_DETECTION,
                &stObjMeta);
            gettimeofday(&t1, NULL);            

            if (tdlRet != CVI_TDL_SUCCESS) {
                printf("inference failed!, ret=%x\n", tdlRet);
            } else {
                // Only for debug
                unsigned long execution_time =
                    ((t1.tv_sec - t0.tv_sec) * 1000000 +
                     t1.tv_usec - t0.tv_usec);

                if (counter++ % 5 == 0) { 
                    printf("obj count: %d, take %.2f ms, width:%u\n",
                           stObjMeta.size,
                           (float)execution_time / 1000,
                           stFrame.stVFrame.u32Width);
                }

                MutexAutoLock(ResultMutex, lock);
                CVI_TDL_CopyObjectMeta(&stObjMeta, &g_stObjMeta);
            }
        }

        if (frame_acquired) {
            CVI_VPSS_ReleaseChnFrame(0, VPSS_CHN1, &stFrame);
        }
        CVI_TDL_Free(&stObjMeta);

        if (s32Ret != CVI_SUCCESS || tdlRet != CVI_TDL_SUCCESS) {
            g_exit_flag = true;
        }
    }

    printf("Exit TDL thread\n");
    pthread_exit(NULL);
}
