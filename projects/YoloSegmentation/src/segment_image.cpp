#include "core/cvi_tdl_types_mem_internal.h"
#include "core/utils/vpss_helper.h"
#include "cvi_tdl.h"
#include "cvi_tdl_media.h"
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <sstream>
#include <stdio.h>
#include <stdlib.h>
#include <string>
#include <sys/time.h>
#include <time.h>
#include <vector>
#include <opencv2/imgcodecs.hpp>
#include <opencv2/imgproc.hpp>
#include <chrono>
#include <filesystem>

namespace fs = std::filesystem;
CVI_S32 init_param(const cvitdl_handle_t tdl_handle) {
    InputPreParam preprocess_cfg = CVI_TDL_GetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG);

    for (int i = 0; i < 3; i++) {
        printf("asign val %d \n", i);
        preprocess_cfg.factor[i] = 0.003922;
        preprocess_cfg.mean[i] = 0.0;
    }
    preprocess_cfg.format = PIXEL_FORMAT_RGB_888_PLANAR;

    printf("setup yolov8 param \n");
    CVI_S32 ret = CVI_TDL_SetPreParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, preprocess_cfg);
    if (ret != CVI_SUCCESS) {
        printf("Can not set yolov8 preprocess parameters %#x\n", ret);
        return ret;
    }
    cvtdl_det_algo_param_t yolov8_param = CVI_TDL_GetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG);
    yolov8_param.cls = 1;

    printf("setup yolov8 algorithm param \n");
    ret = CVI_TDL_SetDetectionAlgoParam(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, yolov8_param);
    if (ret != CVI_SUCCESS) {
        printf("Can not set yolov8 algorithm parameters %#x\n", ret);
        return ret;
    }

    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, 0.5);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, 0.5);

    printf("yolov8 algorithm parameters setup success!\n");
    return ret;
}

int main(int argc, char *argv[]) {
    int vpssgrp_width = 1920;
    int vpssgrp_height = 1080;
    CVI_S32 ret = MMF_INIT_HELPER2(vpssgrp_width, vpssgrp_height, PIXEL_FORMAT_RGB_888, 1, vpssgrp_width,
                                   vpssgrp_height, PIXEL_FORMAT_RGB_888, 1);
    if (ret != CVI_TDL_SUCCESS) {
        printf("Init sys failed with %#x!\n", ret);
        return ret;
    }

    cvitdl_handle_t tdl_handle = NULL;
    ret = CVI_TDL_CreateHandle(&tdl_handle);
    if (ret != CVI_SUCCESS) {
        printf("Create tdl handle failed with %#x!\n", ret);
        return ret;
    }

    std::string strf1(argv[2]);
    // change param of yolov8_detection
    ret = init_param(tdl_handle);

    printf("---------------------openmodel-----------------------");
    ret = CVI_TDL_OpenModel(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, argv[1]);
    CVI_TDL_SetModelThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, 0.5);
    CVI_TDL_SetModelNmsThreshold(tdl_handle, CVI_TDL_SUPPORTED_MODEL_YOLOV8_SEG, 0.6);
    if (ret != CVI_SUCCESS) {
        printf("open model failed with %#x!\n", ret);
        return ret;
    }
    printf("---------------------to do inference-----------------------\n");

    imgprocess_t img_handle;
    CVI_TDL_Create_ImageProcessor(&img_handle);

    VIDEO_FRAME_INFO_S bg;
    ret = CVI_TDL_ReadImage(img_handle, strf1.c_str(), &bg, PIXEL_FORMAT_RGB_888);
    if (ret != CVI_SUCCESS) {
        printf("open img failed with %#x!\n", ret);
        return ret;
    } else {
        printf("image read,width:%d\n", bg.stVFrame.u32Width);
        printf("image read,hidth:%d\n", bg.stVFrame.u32Height);
    }


    cvtdl_object_t obj_meta = {0};

    std::chrono::steady_clock::time_point begin = std::chrono::steady_clock::now();
    CVI_TDL_YoloV8_Seg(tdl_handle, &bg, &obj_meta);
    CVI_TDL_Set_MaskOutlinePoint(&bg, &obj_meta);
    std::chrono::steady_clock::time_point end = std::chrono::steady_clock::now();
    double fps = 1 / std::chrono::duration<double>(end - begin).count();
    printf("Inference FPS: %f\n", fps);

    bg.stVFrame.pu8VirAddr[0] =
    (CVI_U8 *)CVI_SYS_Mmap(bg.stVFrame.u64PhyAddr[0], bg.stVFrame.u32Length[0]);
    cv::Mat img_rgb(bg.stVFrame.u32Height, bg.stVFrame.u32Width, CV_8UC3,
                    bg.stVFrame.pu8VirAddr[0], bg.stVFrame.u32Stride[0]);
    
    cv::cvtColor(img_rgb, img_rgb, cv::COLOR_RGB2BGR);
    std::cout << "objnum:" << obj_meta.size << std::endl;

    for (uint32_t i = 0; i < obj_meta.size; i++) {
        auto *mp = obj_meta.info[i].mask_properity;
        if (mp && mp->mask_point_size >= 3) {
            std::vector<cv::Point> poly;
            poly.reserve(mp->mask_point_size);

            for (uint32_t j = 0; j < mp->mask_point_size; j++) {
                int x = mp->mask_point[2 * j];
                int y = mp->mask_point[2 * j + 1];
                poly.emplace_back(x, y);
            }

            cv::Mat overlay = img_rgb.clone();
            const std::vector<std::vector<cv::Point>> polys{poly};
            cv::fillPoly(overlay, polys, cv::Scalar(0, 255, 0));

            double alpha = 0.35;
            cv::addWeighted(overlay, alpha, img_rgb, 1.0 - alpha, 0.0, img_rgb);
            cv::polylines(img_rgb, polys, true, cv::Scalar(0, 255, 0), 1, cv::LINE_AA);
            auto &b = obj_meta.info[i].bbox;
            cv::rectangle(img_rgb, cv::Rect((int)b.x1, (int)b.y1, (int)(b.x2 - b.x1), (int)(b.y2 - b.y1)), cv::Scalar(255, 0, 0), 1);

            std::ostringstream ss;
            ss << "cls " << obj_meta.info[i].classes << " ";

            cv::putText(img_rgb, ss.str(), cv::Point((int)b.x1, std::max(0, (int)b.y1 - 5)),
                        cv::FONT_HERSHEY_SIMPLEX, 0.7, cv::Scalar(0, 255, 0), 2, cv::LINE_AA);
        }
        
    }
    cv::imwrite("res.png", img_rgb);
    CVI_SYS_Munmap((void *)bg.stVFrame.pu8VirAddr[0], bg.stVFrame.u32Length[0]);

    

    CVI_TDL_Free(&obj_meta);
    CVI_TDL_ReleaseImage(img_handle, &bg);
    CVI_TDL_DestroyHandle(tdl_handle);
    CVI_TDL_Destroy_ImageProcessor(img_handle);
    return ret;
}
