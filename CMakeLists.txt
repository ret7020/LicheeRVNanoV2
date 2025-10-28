cmake_minimum_required(VERSION 3.15)

project(app)
set(CMAKE_CXX_STANDARD 11)

SET(CMAKE_C_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-gcc")
SET(CMAKE_CXX_COMPILER "$ENV{COMPILER}/riscv64-unknown-linux-musl-g++")
set(CMAKE_C_FLAGS "-D__CV181X__ -O3 -march=rv64imafd -O3 -DSENSOR_GCORE_GC4653 -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s")
set(CMAKE_CXX_FLAGS "-D__CV181X__ -O3 -march=rv64imafd -O3 -DSENSOR_GCORE_GC4653 -D_MIDDLEWARE_V2_ -DC906 -DUSE_TPU_IVE -fsigned-char -Wno-format-truncation -fdiagnostics-color=always -s")


include_directories(
    sdk/tdl_sdk/include
    sdk/tdl_sdk/sample
    sdk/middleware/include
    sdk/middleware/modules/isp/include/cv181x
    sdk/middleware/component/isp/common
    sdk/middleware/sample/common
    sdk/middleware/v2/sample/common
    sdk/middleware/v2/include
    sdk/middleware/v2/modules/isp/include/cv181x/
    sdk/middleware/modules/ive/include
    sdk/middleware/3rdparty/
    sdk/osdrv/interdrv/v2/include/chip/cv181x/uapi
    sdk/osdrv/interdrv/v2/include/common/uapi
    sdk/cvi_rtsp/include/cvi_rtsp
    
)


set(EXECUTABLE_OUTPUT_PATH ${CMAKE_BINARY_DIR}/bin)
file(MAKE_DIRECTORY ${EXECUTABLE_OUTPUT_PATH})

add_executable(app projects/StreamYolo/main.c sdk/tdl_sdk/sample/middleware_utils.c)

target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/lib)
target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/usr/lib)
target_link_directories(app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/sdk/prebuilt/usr/lib/3rd)

target_link_libraries(app
-lsample -lcvi_rtsp -lcvikernel -lcvimath -lcviruntime -lz -lm -lcvi_ive 
    -lopencv_core -lopencv_imgproc -lopencv_imgcodecs
    -lcvi_tdl -lcvi_tdl_app  -lsys -lvpss -lcvi_bin -lvi -lvo -lgdc -lrgn -lsns_full -lisp -lvenc -lvdec -lawb -lae -laf -lcvi_bin_isp -lini -lmisc -lisp_algo
    -lpthread -latomic
    
)
