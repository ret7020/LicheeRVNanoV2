cmake_minimum_required(VERSION 3.15)

project(app)
set(CMAKE_CXX_STANDARD 11)

SET(CMAKE_C_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
SET(CMAKE_CXX_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-g++")
set(CMAKE_C_FLAGS "-D__CV181X__ -O3 -march=rv64imafd -O3 -DSENSOR_GCORE_GC4653 -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s")
set(CMAKE_CXX_FLAGS "-D__CV181X__ -O3 -march=rv64imafd -O3 -DSENSOR_GCORE_GC4653 -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s")

set(CVI_SDK_ROOT "sdk")
set(CVI_TDL_SDK_DIR      "${CVI_SDK_ROOT}/tdl_sdk")
set(CVI_MW_DIR           "${CVI_SDK_ROOT}/middleware")
set(CVI_OSDRV_DIR        "${CVI_SDK_ROOT}/osdrv")
set(CVI_PREBUILT_DIR     "${CVI_SDK_ROOT}/prebuilt")


include_directories(projects/StreamYolo/include)

include_directories(
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
    
)




set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

add_executable(app projects/StreamYolo/src/main.c 
                    projects/StreamYolo/src/mw_setup.c
                    projects/StreamYolo/src/threads.c
                    sdk/tdl_sdk/sample/middleware_utils.c)

target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/lib)
target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/usr/lib)
target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/usr/lib/3rd)

target_link_libraries(app
-lsample -lcvi_rtsp -lcvikernel -lcvimath -lcviruntime -lz -lm -lcvi_ive 
    -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
    -lcvi_tdl -lcvi_tdl_app  -lsys -lvpss -lcvi_bin -lvi -lvo -lgdc -lrgn -lsns_full -lisp -lvenc -lvdec -lawb -lae -laf -lcvi_bin_isp -lini -lmisc -lisp_algo
    -lpthread -latomic
    
)
