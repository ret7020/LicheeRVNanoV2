cmake_minimum_required(VERSION 3.15)
project(apps)
set(CMAKE_CXX_STANDARD 17)

set(CMAKE_C_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
set(CMAKE_CXX_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-g++")

set(CVI_SDK_ROOT "/home/stephan/Progs/_LicheeRVNanoV2/sdk")
set(CVI_TDL_SDK_DIR      "${CVI_SDK_ROOT}/tdl_sdk")
set(CVI_MW_DIR           "${CVI_SDK_ROOT}/middleware")
set(CVI_OSDRV_DIR        "${CVI_SDK_ROOT}/osdrv")

set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

add_library(common_settings INTERFACE)

target_compile_options(common_settings INTERFACE
    -D__CV181X__ -O3 -march=rv64imafd -DSENSOR_GCORE_GC4653
    -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char
    -Wno-format-truncation -fdiagnostics-color=always -s
)

target_include_directories(common_settings INTERFACE
    projects/StreamYolo/include
    ${CVI_TDL_SDK_DIR}/include
    ${CVI_TDL_SDK_DIR}/sample
    ${CVI_TDL_SDK_DIR}/include/core/utils
    ${CVI_MW_DIR}/include
    ${CVI_MW_DIR}/modules/isp/include/cv181x
    ${CVI_MW_DIR}/component/isp/common
    ${CVI_MW_DIR}/sample/common
    ${CVI_MW_DIR}/v2/sample/common
    ${CVI_MW_DIR}/v2/include
    ${CVI_MW_DIR}/v2/modules/isp/include/cv181x/
    ${CVI_MW_DIR}/modules/ive/include
    ${CVI_MW_DIR}/3rdparty/
    ${CVI_OSDRV_DIR}/interdrv/v2/include/chip/cv181x/uapi
    ${CVI_OSDRV_DIR}/interdrv/include/common/uapi
    ${CVI_SDK_ROOT}/cvi_rtsp/include/cvi_rtsp
    ${CVI_SDK_ROOT}/opencv/include
)

target_link_directories(common_settings INTERFACE
	${CVI_SDK_ROOT}/prebuilt/lib
	${CVI_SDK_ROOT}/prebuilt/usr/lib
	${CVI_SDK_ROOT}/prebuilt/usr/lib/3rd
)

target_link_libraries(common_settings INTERFACE
    -lsample -lcvi_rtsp -lcvikernel -lcvimath -lcviruntime -lz -lm -lcvi_ive
    -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
    -lcvi_tdl -lcvi_tdl_app -lsys -lvpss -lcvi_bin -lvi -lvo -lgdc -lrgn
    -lsns_full -lisp -lvenc -lvdec -lawb -lae -laf -lcvi_bin_isp -lini -lmisc -lisp_algo
    -lpthread -latomic
)

add_executable(yolo_stream
    projects/StreamYolo/src/main.cpp
    projects/StreamYolo/src/mw_setup.cpp
    projects/StreamYolo/src/threads.cpp
    ${CVI_SDK_ROOT}/tdl_sdk/sample/middleware_utils.c
)
target_link_libraries(yolo_stream PRIVATE common_settings)

add_executable(yolo_seg_image
    projects/YoloSegmentation/src/segment_image.cpp
)
target_link_libraries(yolo_seg_image PRIVATE common_settings)

