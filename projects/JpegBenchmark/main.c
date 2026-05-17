#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <stdatomic.h>
#include <time.h>

#include <cvi_sys.h>
#include <cvi_buffer.h>
#include "jpeg_hw.h"
#include "middleware_utils.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>
#include <cvi_sys.h>
#include <signal.h>


// Check decoder results
static void yuv420p_to_ppm(const char *path,
                            const uint8_t *y_plane,
                            const uint8_t *u_plane,
                            const uint8_t *v_plane,
                            int width, int height,
                            int y_stride, int uv_stride)
{
    FILE *f = fopen(path, "wb");
    if (!f) { perror(path); return; }
    fprintf(f, "P6\n%d %d\n255\n", width, height);

    for (int row = 0; row < height; row++) {
        for (int col = 0; col < width; col++) {
            int Y = y_plane[row * y_stride + col];
            int U = u_plane[(row / 2) * uv_stride + col / 2] - 128;
            int V = v_plane[(row / 2) * uv_stride + col / 2] - 128;

            int R = Y + 1.402   * V;
            int G = Y - 0.34414 * U - 0.71414 * V;
            int B = Y + 1.772   * U;

            uint8_t rgb[3] = {
                (uint8_t)(R < 0 ? 0 : R > 255 ? 255 : R),
                (uint8_t)(G < 0 ? 0 : G > 255 ? 255 : G),
                (uint8_t)(B < 0 ? 0 : B > 255 ? 255 : B),
            };
            fwrite(rgb, 1, 3, f);
        }
    }
    fclose(f);
    printf("Saved %s (%dx%d)\n", path, width, height);
}

void save_vpss_frame_ppm(const char *path, VIDEO_FRAME_INFO_S *frame)
{
    VIDEO_FRAME_S *f = &frame->stVFrame;

    f->pu8VirAddr[0] = CVI_SYS_Mmap(f->u64PhyAddr[0], f->u32Length[0]);
    f->pu8VirAddr[1] = CVI_SYS_Mmap(f->u64PhyAddr[1], f->u32Length[1]);
    f->pu8VirAddr[2] = CVI_SYS_Mmap(f->u64PhyAddr[2], f->u32Length[2]);

    CVI_SYS_IonInvalidateCache(f->u64PhyAddr[0], f->pu8VirAddr[0], f->u32Length[0]);
    CVI_SYS_IonInvalidateCache(f->u64PhyAddr[1], f->pu8VirAddr[1], f->u32Length[1]);
    CVI_SYS_IonInvalidateCache(f->u64PhyAddr[2], f->pu8VirAddr[2], f->u32Length[2]);

    yuv420p_to_ppm(path,
                   (const uint8_t *)f->pu8VirAddr[0],  // Y
                   (const uint8_t *)f->pu8VirAddr[1],  // U (Cb)
                   (const uint8_t *)f->pu8VirAddr[2],  // V (Cr)
                   f->u32Width, f->u32Height,
                   f->u32Stride[0], f->u32Stride[1]);

    CVI_SYS_Munmap(f->pu8VirAddr[0], f->u32Length[0]);
    CVI_SYS_Munmap(f->pu8VirAddr[1], f->u32Length[1]);
    CVI_SYS_Munmap(f->pu8VirAddr[2], f->u32Length[2]);
    f->pu8VirAddr[0] = f->pu8VirAddr[1] = f->pu8VirAddr[2] = NULL;
}

// Limits
#define MAX_THREADS      10
#define DEFAULT_OUT_W    640
#define DEFAULT_OUT_H    640
#define DEFAULT_ITERS    100

static unsigned char *g_jpeg_buf  = NULL;
static size_t         g_jpeg_len  = 0;
static int            g_out_w     = DEFAULT_OUT_W;
static int            g_out_h     = DEFAULT_OUT_H;
static int            g_report_n  = DEFAULT_ITERS;
static atomic_int     g_stop      = 0;

static void on_signal(int sig) { (void)sig; atomic_store(&g_stop, 1); }


static double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static unsigned char *load_file(const char *path, size_t *out_len)
{
    FILE *f = fopen(path, "rb");
    if (!f) { perror(path); return NULL; }
    fseek(f, 0, SEEK_END);
    *out_len = (size_t)ftell(f);
    rewind(f);
    unsigned char *buf = malloc(*out_len);
    if (!buf) { fclose(f); return NULL; }
    fread(buf, 1, *out_len, f);
    fclose(f);
    return buf;
}

typedef struct {
    int thread_id;
    int vdec_chn;
    int vpss_grp;
} ThreadArg;

static void *bench_thread(void *arg)
{
    ThreadArg *ta = (ThreadArg *)arg;
    int tid = ta->thread_id;
    JpegHwCtx ctx;
    memset(&ctx, 0, sizeof(ctx));

    ctx.vdChn   = ta->vdec_chn;
    ctx.vpssGrp = ta->vpss_grp;
    ctx.vpssChn = 0;
    ctx.vbYuv   = VB_INVALID_POOLID;
    ctx.vbEnc   = VB_INVALID_POOLID;

    jpeg_info_t jinfo;
    if (parse_jpeg_header(g_jpeg_buf, g_jpeg_len, &jinfo) != 0) {
        fprintf(stderr, "[T%d] parse_jpeg_header failed\n", tid);
        return NULL;
    }
    if (jinfo.progressive || jinfo.sampling_factor == 2) {
        fprintf(stderr, "[T%d] unsupported JPEG format\n", tid);
        return NULL;
    }

    ctx.src_width    = jinfo.width;
    ctx.src_height   = jinfo.height;
    ctx.out_width    = g_out_w;
    ctx.out_height   = g_out_h;
    ctx.vdec_fmt     = (jinfo.sampling_factor == 0) ? PIXEL_FORMAT_YUV_PLANAR_444 :
        (jinfo.sampling_factor == 3) ? PIXEL_FORMAT_YUV_PLANAR_420 :
        (jinfo.sampling_factor == 4) ? PIXEL_FORMAT_YUV_400       :
                                       PIXEL_FORMAT_YUV_PLANAR_422;
    ctx.vpss_out_fmt = PIXEL_FORMAT_YUV_PLANAR_420;

    CVI_U32 yuv_size = VDEC_GetPicBufferSize(PT_JPEG,
                           ctx.src_width, ctx.src_height,
                           ctx.vdec_fmt, DATA_BITWIDTH_8, COMPRESS_MODE_NONE);
    CVI_U32 enc_size = COMMON_GetPicBufferSize(ctx.out_width, ctx.out_height,
                           ctx.vpss_out_fmt, DATA_BITWIDTH_8,
                           COMPRESS_MODE_NONE, DEFAULT_ALIGN);

    {
        VB_POOL_CONFIG_S p = {0};
        p.u32BlkSize  = yuv_size; p.u32BlkCnt = 3;
        p.enRemapMode = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "bench-vdec-%d", tid);
        ctx.vbYuv = CVI_VB_CreatePool(&p);
        if (ctx.vbYuv == VB_INVALID_POOLID) { fprintf(stderr,"[T%d] VB yuv fail\n",tid); return NULL; }
    }
    {
        VB_POOL_CONFIG_S p = {0};
        p.u32BlkSize  = enc_size; p.u32BlkCnt = 3;
        p.enRemapMode = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "bench-vpss-%d", tid);
        ctx.vbEnc = CVI_VB_CreatePool(&p);
        if (ctx.vbEnc == VB_INVALID_POOLID) { fprintf(stderr,"[T%d] VB enc fail\n",tid); goto cleanup; }
    }

    {
        VDEC_CHN_ATTR_S a = {0};
        a.enType           = PT_JPEG;
        a.enMode           = VIDEO_MODE_FRAME;
        a.u32PicWidth      = ctx.src_width;
        a.u32PicHeight     = ctx.src_height;
        a.u32StreamBufSize = ALIGN((CVI_U32)(ctx.src_width * ctx.src_height), 0x4000);
        a.u32FrameBufSize  = yuv_size;
        a.u32FrameBufCnt   = 3;
        if (CVI_VDEC_CreateChn(ctx.vdChn, &a) != CVI_SUCCESS) {
            fprintf(stderr, "[T%d] CVI_VDEC_CreateChn(%d) failed\n", tid, ctx.vdChn);
            goto cleanup;
        }
        ctx.vdec_created = 1;
    }
    {
        VDEC_CHN_PARAM_S cp;
        if (CVI_VDEC_GetChnParam(ctx.vdChn, &cp) == CVI_SUCCESS) {
            cp.enPixelFormat      = ctx.vdec_fmt;
            cp.u32DisplayFrameNum = 0;
            CVI_VDEC_SetChnParam(ctx.vdChn, &cp);
        }
    }
    {
        VDEC_CHN_POOL_S pool;
        pool.hPicVbPool = ctx.vbYuv;
        pool.hTmvVbPool = VB_INVALID_POOLID;
        if (CVI_VDEC_AttachVbPool(ctx.vdChn, &pool) != CVI_SUCCESS) {
            fprintf(stderr,"[T%d] AttachVbPool failed\n",tid); goto cleanup;
        }
        ctx.vdec_pool_attached = 1;
    }

    {
        VPSS_GRP_ATTR_S ga = {0};
        ga.u32MaxW = ctx.src_width; ga.u32MaxH = ctx.src_height;
        ga.enPixelFormat = ctx.vdec_fmt;
        ga.stFrameRate.s32SrcFrameRate = -1;
        ga.stFrameRate.s32DstFrameRate = -1;
        ga.u8VpssDev = 0;
        if (CVI_VPSS_CreateGrp(ctx.vpssGrp, &ga) != CVI_SUCCESS) {
            fprintf(stderr,"[T%d] CreateGrp(%d) failed\n",tid,ctx.vpssGrp); goto cleanup;
        }
        ctx.vpss_created = 1;
    }
    {
        VPSS_CHN_ATTR_S ca = {0};
        ca.u32Width = ctx.out_width; ca.u32Height = ctx.out_height;
        ca.enVideoFormat   = VIDEO_FORMAT_LINEAR;
        ca.enPixelFormat   = ctx.vpss_out_fmt;
        ca.u32Depth        = 3;
        ca.stFrameRate.s32SrcFrameRate = -1;
        ca.stFrameRate.s32DstFrameRate = -1;
        ca.bMirror = CVI_FALSE; ca.bFlip = CVI_FALSE;
        ca.stAspectRatio.enMode = ASPECT_RATIO_NONE;
        if (CVI_VPSS_SetChnAttr(ctx.vpssGrp, ctx.vpssChn, &ca) != CVI_SUCCESS) {
            fprintf(stderr,"[T%d] SetChnAttr failed\n",tid); goto cleanup;
        }
    }
    if (CVI_VPSS_EnableChn(ctx.vpssGrp, ctx.vpssChn) != CVI_SUCCESS) {
        fprintf(stderr,"[T%d] EnableChn failed\n",tid); goto cleanup;
    }
    ctx.vpss_chn_enabled = 1;

    CVI_VPSS_DetachVbPool(ctx.vpssGrp, ctx.vpssChn);
    if (CVI_VPSS_AttachVbPool(ctx.vpssGrp, ctx.vpssChn, ctx.vbEnc) != CVI_SUCCESS) {
        fprintf(stderr,"[T%d] AttachVbPool(vpss) failed\n",tid); goto cleanup;
    }
    ctx.vpss_pool_attached = 1;

    if (CVI_VDEC_StartRecvStream(ctx.vdChn) != CVI_SUCCESS) {
        fprintf(stderr,"[T%d] StartRecvStream failed\n",tid); goto cleanup;
    }
    ctx.vdec_recv_started = 1;

    if (CVI_VPSS_StartGrp(ctx.vpssGrp) != CVI_SUCCESS) {
        fprintf(stderr,"[T%d] StartGrp failed\n",tid); goto cleanup;
    }
    ctx.vpss_grp_started = 1;

    printf("[T%d] started (vdec_chn=%d vpss_grp=%d src=%dx%d out=%dx%d)\n",
           tid, ctx.vdChn, ctx.vpssGrp,
           ctx.src_width, ctx.src_height, ctx.out_width, ctx.out_height);

    long  frame_count = 0;
    double t_batch_start = now_ms();

    while (!atomic_load(&g_stop)) {
        VIDEO_FRAME_INFO_S outFrame;
        double elapsed = jpeg_decode_to_vpss_frame(&ctx, g_jpeg_buf, g_jpeg_len, &outFrame);

        if (elapsed < 0.0) {
            if ((int)elapsed == -2) continue;  /* BUF_EMPTY: retry */
            fprintf(stderr, "[T%d] decode error\n", tid);
            break;
        }

        // if (frame_count == 0) {
        //     char fname[64];
        //     snprintf(fname, sizeof(fname), "debug_t%d_f%ld.ppm", tid, frame_count);
        //     save_vpss_frame_ppm(fname, &outFrame);
        // }

        CVI_VPSS_ReleaseChnFrame(ctx.vpssGrp, ctx.vpssChn, &outFrame);
        frame_count++;

        if (frame_count % g_report_n == 0) {
            double now    = now_ms();
            double fps    = g_report_n / ((now - t_batch_start) / 1000.0);
            t_batch_start = now;
            printf("[T%d] frames=%-6ld  fps=%.1f  last_decode=%.1f ms\n",
                   tid, frame_count, fps, elapsed);
            fflush(stdout);
        }
    }

cleanup:
    jpeg_hw_deinit(&ctx);
    printf("[T%d] done, total frames=%ld\n", tid, frame_count);
    return NULL;
}


int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "Usage: %s <image.jpg> <num_threads> [iters_per_report]\n", argv[0]);
        return 1;
    }
    const char *jpeg_path = argv[1];
    int num_threads = atoi(argv[2]);
    if (argc >= 4) g_report_n = atoi(argv[3]);

    if (num_threads < 1 || num_threads > MAX_THREADS) {
        fprintf(stderr, "num_threads must be 1..%d\n", MAX_THREADS);
        return 1;
    }

    g_jpeg_buf = load_file(jpeg_path, &g_jpeg_len);
    if (!g_jpeg_buf) return 1;
    printf("Loaded %s (%zu bytes)\n", jpeg_path, g_jpeg_len);

    signal(SIGINT,  on_signal);
    signal(SIGTERM, on_signal);

    VB_CONFIG_S vbConf;
    memset(&vbConf, 0, sizeof(vbConf));
    vbConf.u32MaxPoolCnt = 16;
    CVI_VB_SetConfig(&vbConf);

    if (CVI_SYS_Init() != CVI_SUCCESS) {
        fprintf(stderr, "CVI_SYS_Init failed\n");
        return 1;
    }
    if (CVI_VB_Init() != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VB_Init failed\n");
        CVI_SYS_Exit();
        return 1;
    }

    VDEC_MOD_PARAM_S mp;
    CVI_VDEC_GetModParam(&mp);
    mp.enVdecVBSource = VB_SOURCE_USER;
    CVI_VDEC_SetModParam(&mp);


    pthread_t threads[MAX_THREADS];
    ThreadArg args[MAX_THREADS];

    for (int i = 0; i < num_threads; i++) {
        args[i].thread_id = i;
        args[i].vdec_chn  = i;   /* VDEC channel 0,1,2,3 */
        args[i].vpss_grp  = i;   /* VPSS group   0,1,2,3 */
        pthread_create(&threads[i], NULL, bench_thread, &args[i]);
    }

    for (int i = 0; i < num_threads; i++)
        pthread_join(threads[i], NULL);

    CVI_VB_Exit();
    CVI_SYS_Exit();
    free(g_jpeg_buf);
    return 0;
}