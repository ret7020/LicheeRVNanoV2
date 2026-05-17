#include "jpeg_hw.h"
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <cvi_buffer.h>
#include <cvi_sys.h>
#include "middleware_utils.h"

static inline double now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return ts.tv_sec * 1000.0 + ts.tv_nsec / 1.0e6;
}

static PIXEL_FORMAT_E sampling_to_vdec_fmt(int sf)
{
    switch (sf) {
    case 0:  return PIXEL_FORMAT_YUV_PLANAR_444;
    case 1:
    case 2:  return PIXEL_FORMAT_YUV_PLANAR_422;
    case 3:  return PIXEL_FORMAT_YUV_PLANAR_420;
    case 4:  return PIXEL_FORMAT_YUV_400;
    default: return PIXEL_FORMAT_YUV_PLANAR_420;
    }
}

int parse_jpeg_header(const unsigned char *data, size_t size, jpeg_info_t *info)
{
    memset(info, 0, sizeof(*info));
    info->sampling_factor = -1;
    if (!data || size < 4) return -1;
    if (data[0] != 0xFF || data[1] != 0xD8) return -1;

    const unsigned char *p = data, *end = data + size;
    while (p + 1 < end) {
        unsigned char m0 = p[0], m1 = p[1];
        p += 2;
        if (m0 != 0xFF) break;
        if (m1 == 0xD8 || m1 == 0xD9) continue;
        if (m1 != 0xC0 && m1 != 0xC2) {
            if (p + 2 > end) break;
            p += ((unsigned int)p[0] << 8) | p[1];
            continue;
        }
        if (p + 2 > end) break;
        unsigned int seglen = ((unsigned int)p[0] << 8) | p[1];
        if (p + seglen > end) break;
        if (p[2] != 8) break;

        info->height     = ((int)p[3] << 8) | p[4];
        info->width      = ((int)p[5] << 8) | p[6];
        info->components = p[7];
        if (!info->height || !info->width) break;

        p += 8;
        unsigned char ph[3] = {0}, pv[3] = {0};
        for (int c = 0; c < info->components && p + 3 <= end; c++, p += 3) {
            ph[c] = (p[1] >> 4) & 0xF;
            pv[c] =  p[1]       & 0xF;
        }

        if (info->components == 3 &&
            ph[1] == 1 && pv[1] == 1 && ph[2] == 1 && pv[2] == 1) {
            if      (ph[0] == 1 && pv[0] == 1) info->sampling_factor = 0;
            else if (ph[0] == 2 && pv[0] == 1) info->sampling_factor = 1;
            else if (ph[0] == 1 && pv[0] == 2) info->sampling_factor = 2;
            else if (ph[0] == 2 && pv[0] == 2) info->sampling_factor = 3;
        } else if (info->components == 1 && ph[0] == 1 && pv[0] == 1) {
            info->sampling_factor = 4;
        }

        if (info->sampling_factor == -1) break;
        info->progressive = (m1 == 0xC2) ? 1 : 0;
        return 0;
    }
    return -1;
}

int jpeg_hw_init(JpegHwCtx *ctx,
                 const unsigned char *jpeg_buf, size_t jpeg_len,
                 int out_width, int out_height)
{
    memset(ctx, 0, sizeof(*ctx));
    ctx->vdChn  = VDEC_CHN_ID;
    ctx->vpssGrp = VPSS_GRP_ID;
    ctx->vpssChn = VPSS_CHN_ID;
    ctx->vbYuv  = VB_INVALID_POOLID;
    ctx->vbEnc  = VB_INVALID_POOLID;

    jpeg_info_t jinfo;
    if (parse_jpeg_header(jpeg_buf, jpeg_len, &jinfo) != 0) {
        fprintf(stderr, "parse_jpeg_header failed\n"); return -1;
    }
    if (jinfo.progressive) {
        fprintf(stderr, "progressive JPEG not supported by HW\n"); return -1;
    }
    if (jinfo.sampling_factor == 2) {
        fprintf(stderr, "YUV422V JPEG not supported by HW\n"); return -1;
    }

    ctx->src_width    = jinfo.width;
    ctx->src_height   = jinfo.height;
    ctx->out_width    = out_width;
    ctx->out_height   = out_height;
    ctx->vdec_fmt     = sampling_to_vdec_fmt(jinfo.sampling_factor);
    ctx->vpss_out_fmt = PIXEL_FORMAT_YUV_PLANAR_420;

    CVI_U32 yuv_size = VDEC_GetPicBufferSize(PT_JPEG,
                           ctx->src_width, ctx->src_height,
                           ctx->vdec_fmt, DATA_BITWIDTH_8, COMPRESS_MODE_NONE);
    CVI_U32 enc_in_size = COMMON_GetPicBufferSize(ctx->out_width, ctx->out_height,
                              ctx->vpss_out_fmt, DATA_BITWIDTH_8,
                              COMPRESS_MODE_NONE, DEFAULT_ALIGN);

    /* VB pool for VDEC output */
    {
        VB_POOL_CONFIG_S p = {0};
        p.u32BlkSize   = yuv_size;
        p.u32BlkCnt    = 3;
        p.enRemapMode  = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "usbjpeg-vdec");
        ctx->vbYuv = CVI_VB_CreatePool(&p);
        if (ctx->vbYuv == VB_INVALID_POOLID) {
            fprintf(stderr, "CVI_VB_CreatePool(YUV) failed\n"); return -1;
        }
    }
    /* VB pool for VPSS / encoder input */
    {
        VB_POOL_CONFIG_S p = {0};
        p.u32BlkSize   = enc_in_size;
        p.u32BlkCnt    = 3;
        p.enRemapMode  = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "usbjpeg-vpss");
        ctx->vbEnc = CVI_VB_CreatePool(&p);
        if (ctx->vbEnc == VB_INVALID_POOLID) {
            fprintf(stderr, "CVI_VB_CreatePool(VPSS) failed\n"); return -1;
        }
    }

    /* VDEC channel */
    {
        VDEC_CHN_ATTR_S a = {0};
        a.enType          = PT_JPEG;
        a.enMode          = VIDEO_MODE_FRAME;
        a.u32PicWidth     = ctx->src_width;
        a.u32PicHeight    = ctx->src_height;
        a.u32StreamBufSize = ALIGN((CVI_U32)(ctx->src_width * ctx->src_height), 0x4000);
        a.u32FrameBufSize  = yuv_size;
        a.u32FrameBufCnt   = 3;
        if (CVI_VDEC_CreateChn(ctx->vdChn, &a) != CVI_SUCCESS) {
            fprintf(stderr, "CVI_VDEC_CreateChn failed\n"); return -1;
        }
        ctx->vdec_created = 1;
    }
    {
        VDEC_MOD_PARAM_S mp;
        if (CVI_VDEC_GetModParam(&mp) == CVI_SUCCESS) {
            mp.enVdecVBSource = VB_SOURCE_USER;
            CVI_VDEC_SetModParam(&mp);
        }
    }
    {
        VDEC_CHN_PARAM_S cp;
        if (CVI_VDEC_GetChnParam(ctx->vdChn, &cp) == CVI_SUCCESS) {
            cp.enPixelFormat     = ctx->vdec_fmt;
            cp.u32DisplayFrameNum = 0;
            CVI_VDEC_SetChnParam(ctx->vdChn, &cp);
        }
    }
    {
        VDEC_CHN_POOL_S pool;
        pool.hPicVbPool = ctx->vbYuv;
        pool.hTmvVbPool = VB_INVALID_POOLID;
        if (CVI_VDEC_AttachVbPool(ctx->vdChn, &pool) != CVI_SUCCESS) {
            fprintf(stderr, "CVI_VDEC_AttachVbPool failed\n"); return -1;
        }
        ctx->vdec_pool_attached = 1;
    }

    /* VPSS group */
    {
        VPSS_GRP_ATTR_S ga = {0};
        ga.u32MaxW              = ctx->src_width;
        ga.u32MaxH              = ctx->src_height;
        ga.enPixelFormat        = ctx->vdec_fmt;
        ga.stFrameRate.s32SrcFrameRate = -1;
        ga.stFrameRate.s32DstFrameRate = -1;
        ga.u8VpssDev            = 0;
        if (CVI_VPSS_CreateGrp(ctx->vpssGrp, &ga) != CVI_SUCCESS) {
            fprintf(stderr, "CVI_VPSS_CreateGrp failed\n"); return -1;
        }
        ctx->vpss_created = 1;
    }
    {
        VPSS_CHN_ATTR_S ca = {0};
        ca.u32Width        = ctx->out_width;
        ca.u32Height       = ctx->out_height;
        ca.enVideoFormat   = VIDEO_FORMAT_LINEAR;
        ca.enPixelFormat   = ctx->vpss_out_fmt;
        ca.u32Depth        = 3;
        ca.stFrameRate.s32SrcFrameRate = -1;
        ca.stFrameRate.s32DstFrameRate = -1;
        ca.bMirror         = CVI_FALSE;
        ca.bFlip           = CVI_FALSE;
        ca.stAspectRatio.enMode = ASPECT_RATIO_NONE;
        if (CVI_VPSS_SetChnAttr(ctx->vpssGrp, ctx->vpssChn, &ca) != CVI_SUCCESS) {
            fprintf(stderr, "CVI_VPSS_SetChnAttr failed\n"); return -1;
        }
    }
    if (CVI_VPSS_EnableChn(ctx->vpssGrp, ctx->vpssChn) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VPSS_EnableChn failed\n"); return -1;
    }
    ctx->vpss_chn_enabled = 1;

    CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->vpssChn);
    if (CVI_VPSS_AttachVbPool(ctx->vpssGrp, ctx->vpssChn, ctx->vbEnc) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VPSS_AttachVbPool failed\n"); return -1;
    }
    ctx->vpss_pool_attached = 1;

    if (CVI_VDEC_StartRecvStream(ctx->vdChn) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VDEC_StartRecvStream failed\n"); return -1;
    }
    ctx->vdec_recv_started = 1;

    if (CVI_VPSS_StartGrp(ctx->vpssGrp) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VPSS_StartGrp failed\n"); return -1;
    }
    ctx->vpss_grp_started = 1;
    ctx->started = 1;
    return 0;
}

void jpeg_hw_deinit(JpegHwCtx *ctx)
{
    if (ctx->vpss_grp_started)   CVI_VPSS_StopGrp(ctx->vpssGrp);
    if (ctx->vdec_recv_started)  CVI_VDEC_StopRecvStream(ctx->vdChn);
    if (ctx->vpss_pool_attached) CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->vpssChn);
    if (ctx->vpss_chn_enabled)   CVI_VPSS_DisableChn(ctx->vpssGrp, ctx->vpssChn);
    if (ctx->vpss_created)       CVI_VPSS_DestroyGrp(ctx->vpssGrp);
    if (ctx->vdec_pool_attached) CVI_VDEC_DetachVbPool(ctx->vdChn);
    if (ctx->vdec_created) {
        CVI_VDEC_ResetChn(ctx->vdChn);
        CVI_VDEC_DestroyChn(ctx->vdChn);
    }
    if (ctx->vbYuv != VB_INVALID_POOLID) CVI_VB_DestroyPool(ctx->vbYuv);
    if (ctx->vbEnc != VB_INVALID_POOLID) CVI_VB_DestroyPool(ctx->vbEnc);
    memset(ctx, 0, sizeof(*ctx));
}

double jpeg_decode_to_vpss_frame(JpegHwCtx *ctx,
                                 const unsigned char *jpeg_buf, size_t jpeg_len,
                                 VIDEO_FRAME_INFO_S *outFrame)
{
    VDEC_STREAM_S stream = {0};
    stream.pu8Addr       = (CVI_U8 *)jpeg_buf;
    stream.u32Len        = (CVI_U32)jpeg_len;
    stream.bEndOfFrame   = CVI_TRUE;
    stream.bEndOfStream  = CVI_FALSE;
    stream.bDisplay      = 1;

    double t0 = now_ms();

    if (CVI_VDEC_SendStream(ctx->vdChn, &stream, -1) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VDEC_SendStream failed\n"); return -1.0;
    }

    VIDEO_FRAME_INFO_S stDecFrame;
    if (CVI_VDEC_GetFrame(ctx->vdChn, &stDecFrame, -1) != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VDEC_GetFrame failed\n"); return -1.0;
    }

    CVI_S32 ret = CVI_VPSS_SendFrame(ctx->vpssGrp, &stDecFrame, -1);
    CVI_VDEC_ReleaseFrame(ctx->vdChn, &stDecFrame);
    if (ret != CVI_SUCCESS) {
        fprintf(stderr, "CVI_VPSS_SendFrame failed 0x%x\n", ret); return -1.0;
    }

    ret = CVI_VPSS_GetChnFrame(ctx->vpssGrp, ctx->vpssChn, outFrame, 5000);
    if (ret != CVI_SUCCESS) {
        if (ret == CVI_ERR_VPSS_BUF_EMPTY) return -2.0;
        fprintf(stderr, "CVI_VPSS_GetChnFrame failed 0x%x\n", ret); return -1.0;
    }

    return now_ms() - t0;
}

// Dual jpeg output channels support functions

int jpeg_dual_init(JpegDualCtx *ctx,
                    const unsigned char *jpeg_buf,
                    size_t jpeg_len,
                    int out_width,
                    int out_height,
                    int vdec_chn_id, int vpss_chn_rtsp, int vpss_chn_tdl)
{
    memset(ctx, 0, sizeof(*ctx));

    /* Ask MPI for a free VPSS group instead of forcing 0 */
    VPSS_GRP freeGrp = CVI_VPSS_GetAvailableGrp();
    if (freeGrp < 0) {
        fprintf(stderr, "CVI_VPSS_GetAvailableGrp failed\n"); return -1;
        return -1;
    }

    ctx->vdChn   = vdec_chn_id;
    ctx->vpssGrp = freeGrp;
    ctx->chRtsp  = vpss_chn_rtsp;     // typically 0
    ctx->chTdl   = vpss_chn_tdl;      // typically 1
    ctx->vbYuv   = VB_INVALID_POOLID;
    ctx->vbRtsp  = VB_INVALID_POOLID;
    ctx->vbTdl   = VB_INVALID_POOLID;
    
    jpeg_info_t jinfo;
    if (parse_jpeg_header(jpeg_buf, jpeg_len, &jinfo) != 0) {
        fprintf(stderr, "parse_jpeg_header failed\n"); return -1;
        return -1;
    }
    if (jinfo.progressive) {
        fprintf(stderr, "progressive JPEG not supported\n"); return -1;
        return -1;
    }
    if (jinfo.sampling_factor == 2) {
        fprintf(stderr, "YUV422V JPEG not supported\n"); return -1;
        return -1;
    }

    ctx->src_width  = jinfo.width;
    ctx->src_height = jinfo.height;
    ctx->out_width  = out_width;
    ctx->out_height = out_height;
    ctx->vdec_fmt   = sampling_to_vdec_fmt(jinfo.sampling_factor);

    CVI_U32 yuv_size = VDEC_GetPicBufferSize(
        PT_JPEG, ctx->src_width, ctx->src_height,
        ctx->vdec_fmt, DATA_BITWIDTH_8, COMPRESS_MODE_NONE);

    CVI_U32 rtsp_size = COMMON_GetPicBufferSize(
        ctx->out_width, ctx->out_height,
        PIXEL_FORMAT_YUV_PLANAR_420,
        DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);

    CVI_U32 tdl_size = COMMON_GetPicBufferSize(
        ctx->out_width, ctx->out_height,
        PIXEL_FORMAT_RGB_888,
        DATA_BITWIDTH_8, COMPRESS_MODE_NONE, DEFAULT_ALIGN);

    {
        VB_POOL_CONFIG_S p;
        memset(&p, 0, sizeof(p));
        p.u32BlkSize = yuv_size;
        p.u32BlkCnt = 3;
        p.enRemapMode = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "dual-vdec");
        ctx->vbYuv = CVI_VB_CreatePool(&p);
        if (ctx->vbYuv == VB_INVALID_POOLID) return -1;
    }
    {
        VB_POOL_CONFIG_S p;
        memset(&p, 0, sizeof(p));
        p.u32BlkSize = rtsp_size;
        p.u32BlkCnt = 3;
        p.enRemapMode = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "dual-rtsp");
        ctx->vbRtsp = CVI_VB_CreatePool(&p);
        if (ctx->vbRtsp == VB_INVALID_POOLID) return -1;
    }
    {
        VB_POOL_CONFIG_S p;
        memset(&p, 0, sizeof(p));
        p.u32BlkSize = tdl_size;
        p.u32BlkCnt = 3;
        p.enRemapMode = VB_REMAP_MODE_NONE;
        snprintf(p.acName, MAX_VB_POOL_NAME_LEN, "dual-tdl");
        ctx->vbTdl = CVI_VB_CreatePool(&p);
        if (ctx->vbTdl == VB_INVALID_POOLID) return -1;
    }

    {
        VDEC_CHN_ATTR_S a;
        memset(&a, 0, sizeof(a));
        a.enType = PT_JPEG;
        a.enMode = VIDEO_MODE_FRAME;
        a.u32PicWidth = ctx->src_width;
        a.u32PicHeight = ctx->src_height;
        a.u32StreamBufSize = ALIGN((CVI_U32)(ctx->src_width * ctx->src_height), 0x4000);
        a.u32FrameBufSize = yuv_size;
        a.u32FrameBufCnt = 3;
        if (CVI_VDEC_CreateChn(ctx->vdChn, &a) != CVI_SUCCESS) return -1;
        ctx->vdec_created = 1;
    }
    {
        VDEC_MOD_PARAM_S mp;
        if (CVI_VDEC_GetModParam(&mp) == CVI_SUCCESS) {
            mp.enVdecVBSource = VB_SOURCE_USER;
            CVI_VDEC_SetModParam(&mp);
        }
    }
    {
        VDEC_CHN_PARAM_S cp;
        if (CVI_VDEC_GetChnParam(ctx->vdChn, &cp) == CVI_SUCCESS) {
            cp.enPixelFormat = ctx->vdec_fmt;
            cp.u32DisplayFrameNum = 0;
            CVI_VDEC_SetChnParam(ctx->vdChn, &cp);
        }
    }
    {
        VDEC_CHN_POOL_S pool;
        pool.hPicVbPool = ctx->vbYuv;
        pool.hTmvVbPool = VB_INVALID_POOLID;
        if (CVI_VDEC_AttachVbPool(ctx->vdChn, &pool) != CVI_SUCCESS) return -1;
        ctx->vdec_pool_attached = 1;
    }

    {
        VPSS_GRP_ATTR_S ga;
        memset(&ga, 0, sizeof(ga));
        ga.u32MaxW = ctx->src_width;
        ga.u32MaxH = ctx->src_height;
        ga.enPixelFormat = ctx->vdec_fmt;
        ga.stFrameRate.s32SrcFrameRate = -1;
        ga.stFrameRate.s32DstFrameRate = -1;
        ga.u8VpssDev = 0;
        if (CVI_VPSS_CreateGrp(ctx->vpssGrp, &ga) != CVI_SUCCESS) return -1;
        ctx->vpss_created = 1;
    }

    /* CH0 for RTSP / VENC */
    {
        VPSS_CHN_ATTR_S ca;
        memset(&ca, 0, sizeof(ca));
        ca.u32Width = ctx->out_width;
        ca.u32Height = ctx->out_height;
        ca.enVideoFormat = VIDEO_FORMAT_LINEAR;
        ca.enPixelFormat = PIXEL_FORMAT_YUV_PLANAR_420;
        ca.u32Depth = 3;
        ca.stFrameRate.s32SrcFrameRate = -1;
        ca.stFrameRate.s32DstFrameRate = -1;
        ca.bMirror = CVI_FALSE;
        ca.bFlip = CVI_FALSE;
        ca.stAspectRatio.enMode = ASPECT_RATIO_NONE;
        if (CVI_VPSS_SetChnAttr(ctx->vpssGrp, ctx->chRtsp, &ca) != CVI_SUCCESS) return -1;
        if (CVI_VPSS_EnableChn(ctx->vpssGrp, ctx->chRtsp) != CVI_SUCCESS) return -1;
        ctx->vpss_rtsp_enabled = 1;
        CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->chRtsp);
        if (CVI_VPSS_AttachVbPool(ctx->vpssGrp, ctx->chRtsp, ctx->vbRtsp) != CVI_SUCCESS) return -1;
        ctx->vpss_rtsp_pool_attached = 1;
    }

    /* CH1 for TDL */
    {
        VPSS_CHN_ATTR_S ca;
        memset(&ca, 0, sizeof(ca));
        ca.u32Width = ctx->out_width;
        ca.u32Height = ctx->out_height;
        ca.enVideoFormat = VIDEO_FORMAT_LINEAR;
        ca.enPixelFormat = PIXEL_FORMAT_RGB_888;
        ca.u32Depth = 1;
        ca.stFrameRate.s32SrcFrameRate = -1;
        ca.stFrameRate.s32DstFrameRate = -1;
        ca.bMirror = CVI_FALSE;
        ca.bFlip = CVI_FALSE;
        ca.stAspectRatio.enMode = ASPECT_RATIO_NONE;
        if (CVI_VPSS_SetChnAttr(ctx->vpssGrp, ctx->chTdl, &ca) != CVI_SUCCESS) return -1;
        if (CVI_VPSS_EnableChn(ctx->vpssGrp, ctx->chTdl) != CVI_SUCCESS) return -1;
        ctx->vpss_tdl_enabled = 1;
        CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->chTdl);
        if (CVI_VPSS_AttachVbPool(ctx->vpssGrp, ctx->chTdl, ctx->vbTdl) != CVI_SUCCESS) return -1;
        ctx->vpss_tdl_pool_attached = 1;
    }

    if (CVI_VDEC_StartRecvStream(ctx->vdChn) != CVI_SUCCESS) return -1;
    ctx->vdec_recv_started = 1;
    if (CVI_VPSS_StartGrp(ctx->vpssGrp) != CVI_SUCCESS) return -1;
    ctx->vpss_grp_started = 1;

    return 0;
}


void jpeg_dual_deinit(JpegDualCtx *ctx)
{
    if (ctx->vpss_grp_started) CVI_VPSS_StopGrp(ctx->vpssGrp);
    if (ctx->vdec_recv_started) CVI_VDEC_StopRecvStream(ctx->vdChn);

    if (ctx->vpss_rtsp_pool_attached) CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->chRtsp);
    if (ctx->vpss_tdl_pool_attached)  CVI_VPSS_DetachVbPool(ctx->vpssGrp, ctx->chTdl);

    if (ctx->vpss_rtsp_enabled) CVI_VPSS_DisableChn(ctx->vpssGrp, ctx->chRtsp);
    if (ctx->vpss_tdl_enabled)  CVI_VPSS_DisableChn(ctx->vpssGrp, ctx->chTdl);

    if (ctx->vpss_created) CVI_VPSS_DestroyGrp(ctx->vpssGrp);

    if (ctx->vdec_pool_attached) CVI_VDEC_DetachVbPool(ctx->vdChn);
    if (ctx->vdec_created) {
        CVI_VDEC_ResetChn(ctx->vdChn);
        CVI_VDEC_DestroyChn(ctx->vdChn);
    }

    if (ctx->vbYuv  != VB_INVALID_POOLID) CVI_VB_DestroyPool(ctx->vbYuv);
    if (ctx->vbRtsp != VB_INVALID_POOLID) CVI_VB_DestroyPool(ctx->vbRtsp);
    if (ctx->vbTdl  != VB_INVALID_POOLID) CVI_VB_DestroyPool(ctx->vbTdl);
}


double jpeg_push_and_get_both(JpegDualCtx *ctx,
                                     const unsigned char *jpeg_buf,
                                     size_t jpeg_len,
                                     VIDEO_FRAME_INFO_S *rtspFrame,
                                     VIDEO_FRAME_INFO_S *tdlFrame)
{
    VDEC_STREAM_S stream;
    memset(&stream, 0, sizeof(stream));
    stream.pu8Addr = (CVI_U8 *)jpeg_buf;
    stream.u32Len = (CVI_U32)jpeg_len;
    stream.bEndOfFrame = CVI_TRUE;
    stream.bEndOfStream = CVI_FALSE;
    stream.bDisplay = 1;

    VIDEO_FRAME_INFO_S decFrame;
    CVI_S32 ret;
    double t0 = now_ms();

    ret = CVI_VDEC_SendStream(ctx->vdChn, &stream, -1);
    if (ret != CVI_SUCCESS) return -1.0;

    ret = CVI_VDEC_GetFrame(ctx->vdChn, &decFrame, -1);
    if (ret != CVI_SUCCESS) return -1.0;

    ret = CVI_VPSS_SendFrame(ctx->vpssGrp, &decFrame, -1);
    CVI_VDEC_ReleaseFrame(ctx->vdChn, &decFrame);
    if (ret != CVI_SUCCESS) return -1.0;

    ret = CVI_VPSS_GetChnFrame(ctx->vpssGrp, ctx->chRtsp, rtspFrame, 5000);
    if (ret == CVI_ERR_VPSS_BUF_EMPTY) return -2.0;
    if (ret != CVI_SUCCESS) return -1.0;

    ret = CVI_VPSS_GetChnFrame(ctx->vpssGrp, ctx->chTdl, tdlFrame, 5000);
    if (ret != CVI_SUCCESS) {
        CVI_VPSS_ReleaseChnFrame(ctx->vpssGrp, ctx->chRtsp, rtspFrame);
        if (ret == CVI_ERR_VPSS_BUF_EMPTY) return -2.0;
        return -1.0;
    }

    return now_ms() - t0;
}
