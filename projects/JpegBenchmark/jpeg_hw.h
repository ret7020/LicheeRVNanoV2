#pragma once
#include <stdint.h>
#include <stddef.h>
#include <cvi_vb.h>
#include <cvi_vdec.h>
#include <cvi_vpss.h>

#ifdef __cplusplus
extern "C" {
#endif

#define VDEC_CHN_ID 0
#define VPSS_GRP_ID 0
#define VPSS_CHN_ID VPSS_CHN0

typedef struct {
    int width;
    int height;
    int components;
    int sampling_factor; /* 0=444 1=422H 2=422V 3=420 4=gray */
    int progressive;
} jpeg_info_t;

// HW jpg decoder just 
typedef struct {
    VDEC_CHN       vdChn;
    VPSS_GRP       vpssGrp;
    VPSS_CHN       vpssChn;
    VB_POOL        vbYuv;
    VB_POOL        vbEnc;
    int            src_width;
    int            src_height;
    int            out_width;
    int            out_height;
    PIXEL_FORMAT_E vdec_fmt;
    PIXEL_FORMAT_E vpss_out_fmt;
    /* init flags */
    int            vdec_created;
    int            vdec_pool_attached;
    int            vdec_recv_started;
    int            vpss_created;
    int            vpss_chn_enabled;
    int            vpss_pool_attached;
    int            vpss_grp_started;
    int            started;
} JpegHwCtx;

typedef struct {
    VDEC_CHN       vdChn;
    VPSS_GRP       vpssGrp;
    VPSS_CHN       chRtsp;
    VPSS_CHN       chTdl;
    VB_POOL        vbYuv;
    VB_POOL        vbRtsp;
    VB_POOL        vbTdl;
    int            src_width;
    int            src_height;
    int            out_width;
    int            out_height;
    PIXEL_FORMAT_E vdec_fmt;
    int            vdec_created;
    int            vdec_pool_attached;
    int            vdec_recv_started;
    int            vpss_created;
    int            vpss_rtsp_enabled;
    int            vpss_tdl_enabled;
    int            vpss_rtsp_pool_attached;
    int            vpss_tdl_pool_attached;
    int            vpss_grp_started;
} JpegDualCtx;

/* Parse SOF0/SOF2 marker to discover image geometry and chroma sampling. */
int parse_jpeg_header(const unsigned char *data, size_t size, jpeg_info_t *info);

/* Allocate VB pools, create VDEC/VPSS channels.
   Must be called once with the first valid JPEG frame so geometry is known. */
int jpeg_hw_init(JpegHwCtx *ctx,
                  const unsigned char *jpeg_buf, size_t jpeg_len,
                  int out_width, int out_height);

/* Tear everything down in reverse-init order. */
void jpeg_hw_deinit(JpegHwCtx *ctx);

/* Send one JPEG frame through VDEC -> VPSS and return a scaled YUV frame.
   Caller must call CVI_VPSS_ReleaseChnFrame() on outFrame when done.
   Return value: elapsed ms (>=0), -1.0 on hard error, -2.0 on VPSS buf-empty. */
double jpeg_decode_to_vpss_frame(JpegHwCtx *ctx,
                                 const unsigned char *jpeg_buf, size_t jpeg_len,
                                 VIDEO_FRAME_INFO_S *outFrame);

int jpeg_dual_init(JpegDualCtx *ctx,
                    const unsigned char *jpeg_buf,
                    size_t jpeg_len,
                    int out_width,
                    int out_height,
                    int vdec_chn_id, int vpss_chn_rtsp, int vpss_chn_tdl);


void jpeg_dual_deinit(JpegDualCtx *ctx);

double jpeg_push_and_get_both(JpegDualCtx *ctx, const unsigned char *jpeg_buf, size_t jpeg_len, VIDEO_FRAME_INFO_S *rtspFrame, VIDEO_FRAME_INFO_S *tdlFrame);

#ifdef __cplusplus
}
#endif