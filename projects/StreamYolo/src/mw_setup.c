#include <stdio.h>
#include <signal.h>

#include "mw_setup.h"
#include "vi_vo_utils.h"
#include "sample_utils.h"
#include "threads.h"  // for g_exit_flag

CVI_S32 get_middleware_config(SAMPLE_TDL_MW_CONFIG_S *pstMWConfig) {
    CVI_S32 s32Ret;

    s32Ret = SAMPLE_TDL_Get_VI_Config(&pstMWConfig->stViConfig);
    if (s32Ret != CVI_SUCCESS || pstMWConfig->stViConfig.s32WorkingViNum <= 0) {
        printf("Failed to get senor infomation from ini file (/mnt/data/sensor_cfg.ini).\n");
        return -1;
    }

    PIC_SIZE_E enPicSize;
    s32Ret = SAMPLE_COMM_VI_GetSizeBySensor(
        pstMWConfig->stViConfig.astViInfo[0].stSnsInfo.enSnsType, &enPicSize);
    if (s32Ret != CVI_SUCCESS) {
        printf("Cannot get senor size\n");
        return s32Ret;
    }

    SIZE_S stSensorSize;
    s32Ret = SAMPLE_COMM_SYS_GetPicSize(enPicSize, &stSensorSize);
    if (s32Ret != CVI_SUCCESS) {
        printf("Cannot get senor size\n");
        return s32Ret;
    }

    SIZE_S stVencSize = {
        .u32Width = 1280,
        .u32Height = 720,
    };

    pstMWConfig->stVBPoolConfig.u32VBPoolCount = 3;

    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].enFormat = VI_PIXEL_FORMAT;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32BlkCount = 3;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Height = stSensorSize.u32Height;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32Width = stSensorSize.u32Width;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].bBind = true;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32VpssChnBinding = VPSS_CHN0;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[0].u32VpssGrpBinding = (VPSS_GRP)0;

    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].enFormat = VI_PIXEL_FORMAT;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32BlkCount = 3;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32Height = stVencSize.u32Height;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32Width = stVencSize.u32Width;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].bBind = true;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32VpssChnBinding = VPSS_CHN1;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[1].u32VpssGrpBinding = (VPSS_GRP)0;

    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].enFormat = PIXEL_FORMAT_RGB_888_PLANAR;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32BlkCount = 1;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32Height = 1280;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].u32Width = 720;
    pstMWConfig->stVBPoolConfig.astVBPoolSetup[2].bBind = false;

    pstMWConfig->stVPSSPoolConfig.u32VpssGrpCount = 1;
#ifndef CV186X
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[0] = VPSS_INPUT_MEM;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.enMode = VPSS_MODE_DUAL;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[0] = 0;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.aenInput[1] = VPSS_INPUT_ISP;
    pstMWConfig->stVPSSPoolConfig.stVpssMode.ViPipe[1] = 0;
#endif

    SAMPLE_TDL_VPSS_CONFIG_S *pstVpssConfig =
        &pstMWConfig->stVPSSPoolConfig.astVpssConfig[0];
    pstVpssConfig->bBindVI = true;

    VPSS_GRP_DEFAULT_HELPER2(&pstVpssConfig->stVpssGrpAttr,
                             stSensorSize.u32Width,
                             stSensorSize.u32Height,
                             VI_PIXEL_FORMAT,
                             1);

    pstVpssConfig->u32ChnCount = 2;
    pstVpssConfig->u32ChnBindVI = VPSS_CHN0;

    VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[0],
                            stVencSize.u32Width,
                            stVencSize.u32Height,
                            VI_PIXEL_FORMAT,
                            true);
    pstVpssConfig->astVpssChnAttr[0].bFlip = CVI_TRUE;

    VPSS_CHN_DEFAULT_HELPER(&pstVpssConfig->astVpssChnAttr[1],
                            1280,
                            720,
                            VI_PIXEL_FORMAT,
                            true);
    
    pstVpssConfig->astVpssChnAttr[1].bFlip = CVI_TRUE;

    SAMPLE_TDL_Get_Input_Config(&pstMWConfig->stVencConfig.stChnInputCfg);
    pstMWConfig->stVencConfig.u32FrameWidth = stVencSize.u32Width;
    pstMWConfig->stVencConfig.u32FrameHeight = stVencSize.u32Height;

    SAMPLE_TDL_Get_RTSP_Config(&pstMWConfig->stRTSPConfig.stRTSPConfig);

    return s32Ret;
}

void SampleHandleSig(CVI_S32 signo) {
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    printf("handle signal, signo: %d\n", signo);
    if (SIGINT == signo || SIGTERM == signo) {
        g_exit_flag = true;
    }
}
