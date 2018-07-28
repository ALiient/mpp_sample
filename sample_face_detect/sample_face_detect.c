/******************************************************************************
  Copyright (C), 2001-2017, Allwinner Tech. Co., Ltd.
 ******************************************************************************
  File Name     : sample_face_detect.c
  Version       : V6.0
  Author        : Allwinner BU3-XIAN Team
  Created       :
  Last Modified : 2017/11/30
  Description   : mpp component implement
  Function List :
  History       :
******************************************************************************/
#include "sample_face_detect.h"

int64_t g_last_eve_time = 0;
int64_t g_last_vo_time = 0;
int64_t g_exit_flag = 0;

typedef struct _UserConfig {
    VirVi_Params   stVirViParams[VirVi_MAX];
    VO_Params      stVOParams;
    VENC_Params    stVENCParams;
    EVE_Params     stEVEParams;
    OSD_Params     stOSDParams;
    Picture_Params stPictureParam;
    THREAD_DATA    stThreadData[PROC_MAX];
} UserConfig;

// VirVi���ݴ����߳�
static void *EVE_Proc(void* pThreadData)
{
    UserConfig* pUserCfg = (UserConfig*)pThreadData;
    assert(pUserCfg != NULL);

    VirVi_Params *pVirViParam = (VirVi_Params*)&pUserCfg->stVirViParams[VirVi_EVE];
    assert(pVirViParam != NULL);

    EVE_Params *pEVEParams = (EVE_Params*)&pUserCfg->stEVEParams;
    assert(pEVEParams != NULL);

    AW_IMAGE_S* pImage = pEVEParams->pImage;
    assert(pImage != NULL);

    alogd("Start EVE Thread!  mViDev[%d] mViChn[%d]"
          ,pVirViParam->iViDev
          ,pVirViParam->iViChn
         );

    // TODO:д��nv21
    int iYSize = pEVEParams->iPicWidth * pEVEParams->iPicHeight;
    int iUSize = iYSize / 2;  //TODO:д��nv21
    int iVSize = 0;

    AW_AI_EVE_EVENT_RESULT_S eve_result = {0};
    int iFrameIdx      = 0;
    int iFrameInterval = 1000000 / pEVEParams->iFrmRate;
    while (iFrameIdx < pEVEParams->iFrameNum) {
        // ��ȡһ֡YUV����
        VIDEO_FRAME_INFO_S stFrameInfo;
        if (SUCCESS == AW_MPI_VI_GetFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo, 500)) {
            // ��ȡVirVi���ݳɹ�

            alogv("dev = %d  chn = %d  iFrameIdx = %d TimeStamp = %lld"
                  ,pVirViParam->iViDev
                  ,pVirViParam->iViChn
                  ,iFrameIdx
                  ,stFrameInfo.VFrame.mpts
                 );

            // ����֡��֡ͬ��
            if (g_last_eve_time > 0) {
                int64_t time_diff  = stFrameInfo.VFrame.mpts - g_last_eve_time;
                int frame_diff = (time_diff + iFrameInterval/2)/iFrameInterval;
                if (frame_diff == 2) {
                    alogv("EVE cast one frame, iFrameIdx = %d", iFrameIdx);
                    iFrameIdx += (frame_diff - 1); //������֡��ɵ�iFrameIndex��λ
                }
            }

            g_last_eve_time = stFrameInfo.VFrame.mpts;
            int64_t diff = g_last_vo_time - g_last_eve_time;  //VO����EVE��ʱ��
            if (g_last_vo_time != 0 && diff > 1.5 * iFrameInterval) {
                alogv("iFrameIdx = %d, mpts = %lld, Sync EVE and VO Proc!!!", iFrameIdx, stFrameInfo.VFrame.mpts);
                AW_MPI_VI_ReleaseFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo);
                iFrameIdx++;
                continue;
            } else {
                alogv("eve_time = %lld vo_time = %lld diff = %lld", g_last_eve_time, g_last_vo_time, diff);
            }

            // ����EVE����Դ, YUV�ļ���ŵ�ַ
            pImage->mPixelFormat = pEVEParams->ePixFmt;
            pImage->mWidth       = pEVEParams->iPicWidth;
            pImage->mHeight      = pEVEParams->iPicHeight;
            pImage->mStride[0]   = pEVEParams->iPicWidth;
            memcpy(pImage->mpVirAddr[0], stFrameInfo.VFrame.mpVirAddr[0], iYSize);
            memcpy(pImage->mpVirAddr[1], stFrameInfo.VFrame.mpVirAddr[1], iUSize);
            memcpy(pImage->mpVirAddr[2], stFrameInfo.VFrame.mpVirAddr[2], iVSize);
            AW_AI_EVE_Event_SetEveSourceAddress(pEVEParams->pEVEHd, (void *)pImage->mpVirAddr[0]);

            // ����һ֡TUVͼ�񣬲���ȡ���������ʺ���Ƶ����
            AW_STATUS_E eProcessRet = AW_AI_EVE_Event_Process(pEVEParams->pEVEHd, pImage, stFrameInfo.VFrame.mpts, &eve_result);
            if (eProcessRet != AW_STATUS_OK) {
                alogd("FaceDetect lib process yuv data fail!");
                abort();
            }

            // ��ʾ������
            ShowFaceDetectResult(iFrameIdx, &eve_result, 0);

            pthread_mutex_lock(&g_stResult_lock);
            if (AW_STATUS_ERROR != eProcessRet && eve_result.sTarget.s32TargetNum > 0) {
                g_eve_ready = 1;
                memcpy(&g_stResult, &eve_result, sizeof(AW_AI_EVE_EVENT_RESULT_S));
            } else {
                g_eve_ready = 0;
                memset(&g_stResult, 0, sizeof(AW_AI_EVE_EVENT_RESULT_S));
            }
            pthread_mutex_unlock(&g_stResult_lock);

            // �ͷ�YUV����
            AW_MPI_VI_ReleaseFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo);
        } else {
            // ��ȡVirVi����ʧ��
            alogw("dev = %d  chn = %d GetFrame Failed!", pVirViParam->iViDev, pVirViParam->iViChn);
            usleep(20 * 1000);
        }

        iFrameIdx++;
    }
    return NULL;
}

static void *VO_Proc(void* pThreadData)
{
    UserConfig* pUserCfg = (UserConfig*)pThreadData;
    assert(pUserCfg != NULL);

    VirVi_Params *pVirViParam = (VirVi_Params*)&pUserCfg->stVirViParams[VirVi_VO];
    assert(pVirViParam != NULL);

    VO_Params* pVO_Params = (VO_Params*)&pUserCfg->stVOParams;
    assert(pVO_Params != NULL);

    OSD_Params* pOSDParams = (OSD_Params*)&pUserCfg->stOSDParams;
    assert(pOSDParams != NULL);

    alogd("Start VO_Proc Thread!  mViDev[%d] mViChn[%d]"
          ,pVirViParam->iViDev
          ,pVirViParam->iViChn
         );

    int iPixelSize 		= VI_CAPTURE_WIDTH * VI_CAPTURE_HIGHT;
    int iFrameSize 		= iPixelSize * 3 / 2;

    unsigned int u32PhyAddr;
    void *pVoAddr;
    AW_MPI_SYS_MmzAlloc_Cached(&u32PhyAddr, &pVoAddr, iFrameSize);

    VIDEO_FRAME_INFO_S VFrameInfo;
    VFrameInfo.mId                  = 0;
    VFrameInfo.VFrame.mPhyAddr[0]   = u32PhyAddr;
    VFrameInfo.VFrame.mPhyAddr[1]   = u32PhyAddr + iPixelSize;
    VFrameInfo.VFrame.mPhyAddr[2]   = u32PhyAddr + iPixelSize + iPixelSize / 4;
    VFrameInfo.VFrame.mpVirAddr[0]  = pVoAddr;
    VFrameInfo.VFrame.mpVirAddr[1]  = pVoAddr + iPixelSize;
    VFrameInfo.VFrame.mpVirAddr[2]  = pVoAddr + iPixelSize + iPixelSize / 4;
    VFrameInfo.VFrame.mWidth        = VI_CAPTURE_WIDTH;
    VFrameInfo.VFrame.mHeight       = VI_CAPTURE_HIGHT;
    VFrameInfo.VFrame.mField        = VIDEO_FIELD_FRAME;
    VFrameInfo.VFrame.mPixelFormat  = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420; //TODO:д��nv21
    VFrameInfo.VFrame.mVideoFormat  = VIDEO_FORMAT_LINEAR;
    VFrameInfo.VFrame.mCompressMode = COMPRESS_MODE_NONE;
    VFrameInfo.VFrame.mOffsetTop    = 0;
    VFrameInfo.VFrame.mOffsetBottom = VI_CAPTURE_HIGHT;
    VFrameInfo.VFrame.mOffsetLeft   = 0;
    VFrameInfo.VFrame.mOffsetRight  = VI_CAPTURE_WIDTH;

    create_osd(pOSDParams);

    AW_AI_EVE_EVENT_RESULT_S eve_result = {0};
    uint64_t nPts = 0;   /* unit:us */
    uint64_t nFrameInterval = 1000000/30; //unit:us
    int iFrameIdx = 0;
    time_t pre_time  = 0;
    while (iFrameIdx < pVO_Params->iFrameNum) {
        // ��ȡһ֡YUV����
        VIDEO_FRAME_INFO_S stFrameInfo;
        if (SUCCESS == AW_MPI_VI_GetFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo, 500)) {
            // ��ȡVirVi���ݳɹ�

            g_last_vo_time = stFrameInfo.VFrame.mpts;

            // OSD �������
            update_osd(pOSDParams, (void*)&pre_time);

            // �������ݸ�VO
            memcpy(VFrameInfo.VFrame.mpVirAddr[0], stFrameInfo.VFrame.mpVirAddr[0], iPixelSize);
            memcpy(VFrameInfo.VFrame.mpVirAddr[1], stFrameInfo.VFrame.mpVirAddr[1], iPixelSize/2);

            pthread_mutex_lock(&g_stResult_lock);
            memset(&eve_result, 0, sizeof(AW_AI_EVE_EVENT_RESULT_S));
            memcpy(&eve_result, &g_stResult, sizeof(AW_AI_EVE_EVENT_RESULT_S));
            pthread_mutex_unlock(&g_stResult_lock);  // mutex_Unlock

            for (int i = 0; i < eve_result.sTarget.s32TargetNum; i++) {
                AW_RECT_S* pRect = &eve_result.sTarget.astTargets[i].stRect;
                int iFaceLeft   = pRect->s16Left   * VI_CAPTURE_WIDTH / EVE_CALC_WIDTH;
                int iFaceTop    = pRect->s16Top    * VI_CAPTURE_HIGHT / EVE_CALC_HIGHT;
                int iFaceRight  = pRect->s16Right  * VI_CAPTURE_WIDTH / EVE_CALC_WIDTH;
                int iFaceBottom = pRect->s16Bottom * VI_CAPTURE_HIGHT / EVE_CALC_HIGHT;
                int iFaceWidth  = iFaceRight - iFaceLeft;
                int iFaceHeight = iFaceBottom - iFaceTop;

                DrawRect_Nv21((char *)VFrameInfo.VFrame.mpVirAddr[0]
                              ,(char *)VFrameInfo.VFrame.mpVirAddr[1]
                              ,VI_CAPTURE_WIDTH
                              ,VI_CAPTURE_HIGHT
                              ,iFaceLeft
                              ,iFaceTop
                              ,iFaceWidth
                              ,iFaceHeight
                              ,3// int len_w
                             );
            }

            VFrameInfo.VFrame.mpts = nPts;
            VFrameInfo.VFrame.mpts = stFrameInfo.VFrame.mpts;
            nPts += nFrameInterval;
            AW_MPI_VO_SendFrame(pVO_Params->iVoLayer, pVO_Params->iVoChn, &VFrameInfo, 0);

            alogv("dev = %d  chn = %d  iFrameIdx = %d TimeStamp = %lld"
                  ,pVirViParam->iViDev
                  ,pVirViParam->iViChn
                  ,iFrameIdx
                  ,stFrameInfo.VFrame.mpts
                 );

            // �ͷ�YUV����
            AW_MPI_VI_ReleaseFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo);
        } else {
            // ��ȡVirVi����ʧ��
            alogw("dev = %d  chn = %d GetFrame Failed!", pVirViParam->iViDev, pVirViParam->iViChn);
            usleep(20 * 1000);
        }
        iFrameIdx++;
    }

    AW_MPI_SYS_MmzFree(u32PhyAddr, pVoAddr);
    destroy_osd(pOSDParams);
    return NULL;
}

static void *VENC_Proc(void* pThreadData)
{
    UserConfig* pUserCfg = (UserConfig*)pThreadData;
    assert(pUserCfg != NULL);

    VirVi_Params *pVirViParam = (VirVi_Params*)&pUserCfg->stVirViParams[VirVi_VENC];
    assert(pVirViParam != NULL);

    VENC_Params *pVENCParams = (VENC_Params*)&pUserCfg->stVENCParams;
    assert(pVENCParams != NULL);
    assert(pVENCParams->mVideoEncoderFmt == PT_H264);

    Picture_Params *pPicParam = (Picture_Params*)&pUserCfg->stPictureParam;
    assert(pPicParam != NULL);

    alogd("Start VEnc Thread!  mViDev[%d] mViChn[%d]"
          ,pVirViParam->iViDev
          ,pVirViParam->iViChn
         );

    VIDEO_FRAME_INFO_S *pVideoFrame = (VIDEO_FRAME_INFO_S*)malloc(sizeof(VIDEO_FRAME_INFO_S));
    memset(pVideoFrame, 0, sizeof(VIDEO_FRAME_INFO_S));

    unsigned int uPhyAddr;
    void *pVirtAddr;
    int nFrameSize = pVENCParams->srcWidth * pVENCParams->srcHeight * 3 / 2;
    int nPixelSize = pVENCParams->srcWidth * pVENCParams->srcHeight;
    AW_MPI_SYS_MmzAlloc_Cached(&uPhyAddr, &pVirtAddr, nFrameSize);

    pVideoFrame->VFrame.mpVirAddr[0]  = (void *)pVirtAddr;
    pVideoFrame->VFrame.mpVirAddr[1]  = (void *)pVirtAddr + nPixelSize;
    pVideoFrame->VFrame.mPhyAddr[0]   = (unsigned int)uPhyAddr;
    pVideoFrame->VFrame.mPhyAddr[1]   = (unsigned int)uPhyAddr + nPixelSize;
    pVideoFrame->VFrame.mWidth	      = pVENCParams->srcWidth;
    pVideoFrame->VFrame.mHeight       = pVENCParams->srcHeight;
    pVideoFrame->VFrame.mOffsetLeft   = 0;
    pVideoFrame->VFrame.mOffsetTop	  = 0;
    pVideoFrame->VFrame.mOffsetRight  = pVideoFrame->VFrame.mOffsetLeft + pVideoFrame->VFrame.mWidth;
    pVideoFrame->VFrame.mOffsetBottom = pVideoFrame->VFrame.mOffsetTop + pVideoFrame->VFrame.mHeight;
    pVideoFrame->VFrame.mField	      = VIDEO_FIELD_FRAME;
    pVideoFrame->VFrame.mVideoFormat  = VIDEO_FORMAT_LINEAR;
    pVideoFrame->VFrame.mCompressMode = COMPRESS_MODE_NONE;

    VENC_CHN mVeChnH264 = 0; // VENC_CHN_0
    create_venc(pVENCParams, mVeChnH264, PT_H264);

#ifdef SAVE_H264_FILE
    // ���ļ���д��SPS��PPSͷ
    FILE* fd_out = fopen(pVENCParams->szOutputFile, "wb");

    VencHeaderData pH264SpsPpsInfo= {0};
    AW_MPI_VENC_GetH264SpsPpsInfo(mVeChnH264, &pH264SpsPpsInfo);
    if (pH264SpsPpsInfo.nLength) {
        fwrite(pH264SpsPpsInfo.pBuffer, 1, pH264SpsPpsInfo.nLength, fd_out);
    }
#endif

    VENC_PACK_S mpPack;
    VENC_STREAM_S vencFrame;
    memset(&vencFrame, 0, sizeof(VENC_STREAM_S));
    vencFrame.mpPack = &mpPack;
    vencFrame.mPackCount = 1;

    int iFrameIdx = 0;
    while (iFrameIdx < pVENCParams->iFrameNum) {
        // ��ȡһ֡YUV����
        VIDEO_FRAME_INFO_S stFrameInfo;
        if (SUCCESS == AW_MPI_VI_GetFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo, 500)) {
            // ��ȡVirVi���ݳɹ�

            alogv("dev = %d  chn = %d  iFrameIdx = %d TimeStamp = %lld"
                  ,pVirViParam->iViDev
                  ,pVirViParam->iViChn
                  ,iFrameIdx
                  ,stFrameInfo.VFrame.mpts
                 );

            // ����һ֡����
            pVideoFrame->VFrame.mpts         = stFrameInfo.VFrame.mpts;
            pVideoFrame->VFrame.mPixelFormat = pVENCParams->srcPixFmt;
            memcpy(pVideoFrame->VFrame.mpVirAddr[0], stFrameInfo.VFrame.mpVirAddr[0], nPixelSize);
            memcpy(pVideoFrame->VFrame.mpVirAddr[1], stFrameInfo.VFrame.mpVirAddr[1], nPixelSize / 2);

            // �������ͨ��
            AW_MPI_VENC_SendFrame(mVeChnH264, pVideoFrame, 0);

            // ��ȡ������
            int ret = AW_MPI_VENC_GetStream(mVeChnH264, &vencFrame, 10);
            if (SUCCESS == ret) {
#ifdef SAVE_H264_FILE
                //����H264�ļ�
                if (vencFrame.mpPack->mLen0) {
                    fwrite(vencFrame.mpPack->mpAddr0, 1, vencFrame.mpPack->mLen0, fd_out);
                }
                if (vencFrame.mpPack->mLen1) {
                    fwrite(vencFrame.mpPack->mpAddr1, 1, vencFrame.mpPack->mLen1, fd_out);
                }
                alogv("VENC Got frame, pts = %llu, seq = %d", vencFrame.mpPack->mPTS, vencFrame.mSeq);
#endif
                AW_MPI_VENC_ReleaseStream(mVeChnH264, &vencFrame);
            }

            bool bNeedEncJpeg = FALSE;
            pthread_mutex_lock(&g_stResult_lock);
            pthread_mutex_lock(&pPicParam->lockPicture);
            if (g_eve_ready == 1 && g_stResult.sTarget.s32TargetNum > 0 && (pPicParam->ePicState == MPP_StateIdle)) {
                bNeedEncJpeg = TRUE;
                g_eve_ready = 0;
            }
            pthread_mutex_unlock(&pPicParam->lockPicture);
            pthread_mutex_unlock(&g_stResult_lock);

            if (bNeedEncJpeg) {
                pPicParam->stVideoFrame.VFrame.mpts         = stFrameInfo.VFrame.mpts;
                pPicParam->stVideoFrame.VFrame.mPixelFormat = pVENCParams->srcPixFmt;
                memcpy(pPicParam->stVideoFrame.VFrame.mpVirAddr[0], stFrameInfo.VFrame.mpVirAddr[0], nPixelSize);
                memcpy(pPicParam->stVideoFrame.VFrame.mpVirAddr[1], stFrameInfo.VFrame.mpVirAddr[1], nPixelSize / 2);
                memcpy(pPicParam->pEveResult, &g_stResult, sizeof(AW_AI_EVE_EVENT_RESULT_S));

                pthread_mutex_lock(&pPicParam->lockPicture);
                pPicParam->ePicState = MPP_StateFilled;
                pthread_mutex_unlock(&pPicParam->lockPicture);
            }

            // �ͷ�YUV����
            AW_MPI_VI_ReleaseFrame(pVirViParam->iViDev, pVirViParam->iViChn, &stFrameInfo);
        } else {
            // ��ȡVirVi����ʧ��
            alogw("dev = %d  chn = %d GetFrame Failed!", pVirViParam->iViDev, pVirViParam->iViChn);
            usleep(20 * 1000);
        }

        iFrameIdx++;
    }
    destroy_venc(mVeChnH264);

    AW_MPI_SYS_MmzFree(uPhyAddr, pVirtAddr);
#ifdef SAVE_H264_FILE
    fclose(fd_out);
#endif

    if (pVideoFrame) {
        free(pVideoFrame);
        pVideoFrame = NULL;
    }
    g_exit_flag = 1;
    return NULL;
}

static void *JPEG_Proc(void* pThreadData)
{
    alogd("Start Jpeg Thread!");
    system("rm -rf pic;mkdir pic;");
    int last_cap_time = 0;

    UserConfig* pUserCfg = (UserConfig*)pThreadData;
    assert(pUserCfg != NULL);

    VENC_Params *pVENCParams = (VENC_Params*)&pUserCfg->stVENCParams;
    assert(pVENCParams != NULL);

    Picture_Params *pPicParam = (Picture_Params*)&pUserCfg->stPictureParam;
    assert(pPicParam != NULL);

    VENC_CHN mVeChnJPG = 1;
    create_venc(pVENCParams, mVeChnJPG, PT_MJPEG);

    int iFrameIdx = 0;
    while (!g_exit_flag) {
        bool bNeedEncJpeg = FALSE;
        pthread_mutex_lock(&pPicParam->lockPicture);
        int curr_time = time(NULL);
        if (pPicParam->ePicState == MPP_StateFilled && curr_time - last_cap_time >= 3) {
            bNeedEncJpeg = TRUE;
            last_cap_time = curr_time;
        }
        pthread_mutex_unlock(&pPicParam->lockPicture);

        // Get EVE Region Crop face picture
        if (bNeedEncJpeg) {
            for (int i = 0; i < pPicParam->pEveResult->sTarget.s32TargetNum; i++) {
                AW_RECT_S* pRect = &pPicParam->pEveResult->sTarget.astTargets[i].stRect;
                int iFaceLeft   = pRect->s16Left   * VI_CAPTURE_WIDTH / EVE_CALC_WIDTH;
                int iFaceTop    = pRect->s16Top    * VI_CAPTURE_HIGHT / EVE_CALC_HIGHT;
                int iFaceRight  = pRect->s16Right  * VI_CAPTURE_WIDTH / EVE_CALC_WIDTH;
                int iFaceBottom = pRect->s16Bottom * VI_CAPTURE_HIGHT / EVE_CALC_HIGHT;
                int iFaceWidth  = iFaceRight - iFaceLeft;
                int iFaceHeight = iFaceBottom - iFaceTop;
                if (iFaceWidth <= 0 || iFaceHeight <= 0) {
                    alogw("Wrong FacePos! iFaceLeft = %d iFaceRight = %d iFaceTop = %d iFaceBottom = %d", iFaceLeft, iFaceRight, iFaceTop, iFaceBottom);
                    continue;
                }

                int face_left   = MAX(0,         iFaceLeft   - (int)(iFaceWidth * 0.2));
                int face_right  = MAX(face_left, iFaceRight  + (int)(iFaceWidth * 0.2));
                int face_top    = MAX(0,         iFaceTop    - (int)(iFaceHeight * 0.4));
                int face_bottom = MAX(face_top,  iFaceBottom + (int)(iFaceHeight * 0.2));
                face_right  = MIN(VI_CAPTURE_WIDTH, face_right);
                face_bottom = MIN(VI_CAPTURE_HIGHT, face_bottom);

                VENC_CROP_CFG_S crop_cfg;
                crop_cfg.bEnable     = TRUE;
                crop_cfg.Rect.X      = ALIGN(face_left, 16);
                crop_cfg.Rect.Y      = ALIGN(face_top, 16);
                crop_cfg.Rect.Width  = ALIGN(face_right - face_left, 16);
                crop_cfg.Rect.Height = ALIGN(face_bottom - face_top, 16);
                alogd("FacePos iFaceLeft = %d iFaceRight = %d iFaceTop = %d iFaceBottom = %d", iFaceLeft, iFaceRight, iFaceTop, iFaceBottom);
                alogd("CropPos left[%d] right[%d] top[%d] bottom[%d]", face_left, face_right, face_top, face_bottom);
                char szJpgPath [256];
                sprintf(szJpgPath, "pic/%d_%dx%d.jpg", iFrameIdx, crop_cfg.Rect.Width, crop_cfg.Rect.Height);
                save_jpeg_venc(mVeChnJPG, &crop_cfg, &pPicParam->stVideoFrame, szJpgPath);
            }

            pthread_mutex_lock(&pPicParam->lockPicture);
            pPicParam->ePicState = MPP_StateIdle;
            bNeedEncJpeg = FALSE;
            pthread_mutex_unlock(&pPicParam->lockPicture);
        } else {
            usleep(20 * 1000);
        }
        iFrameIdx++;
    }

    destroy_venc(mVeChnJPG);
    return NULL;
}

int sysconfig_init(UserConfig* pUserCfg)
{
    // Initial global vipp map
    g_VIPP_map[VIPP_1].bInit      = 0;
    g_VIPP_map[VIPP_1].width      = VI_CAPTURE_WIDTH;
    g_VIPP_map[VIPP_1].height     = VI_CAPTURE_HIGHT;
    g_VIPP_map[VIPP_1].eformat    = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    g_VIPP_map[VIPP_1].frame_rate = 30;
    g_VIPP_map[VIPP_1].bMirror    = 0;
    g_VIPP_map[VIPP_1].bFlip      = 0;

    g_VIPP_map[VIPP_3].bInit      = 0;
    g_VIPP_map[VIPP_3].width      = EVE_CALC_WIDTH;
    g_VIPP_map[VIPP_3].height     = EVE_CALC_HIGHT;
    g_VIPP_map[VIPP_3].eformat    = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;
    g_VIPP_map[VIPP_3].frame_rate = 30;
    g_VIPP_map[VIPP_3].bMirror    = 0;
    g_VIPP_map[VIPP_3].bFlip      = 0;

    // Initial VirVi params
    pUserCfg->stVirViParams[VirVi_EVE].iViDev  = VIPP_3;
    pUserCfg->stVirViParams[VirVi_EVE].iViChn  = VirViChn_0;

    pUserCfg->stVirViParams[VirVi_VO].iViDev   = VIPP_1;
    pUserCfg->stVirViParams[VirVi_VO].iViChn   = VirViChn_0;

    pUserCfg->stVirViParams[VirVi_VENC].iViDev = VIPP_1;
    pUserCfg->stVirViParams[VirVi_VENC].iViChn = VirViChn_1;

    // Initial EVE params
    pUserCfg->stEVEParams.ePixFmt    = MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420;   //TODO: Ŀǰд��nv21
    pUserCfg->stEVEParams.iPicWidth  = EVE_CALC_WIDTH;
    pUserCfg->stEVEParams.iPicHeight = EVE_CALC_HIGHT;
    pUserCfg->stEVEParams.iFrmRate   = 30;
    pUserCfg->stEVEParams.iFrameNum  = TEST_FRAME_NUM;

    // Initial VO params
    pUserCfg->stVOParams.iVoDev	        = 0;
    pUserCfg->stVOParams.iVoChn	        = 0;
    pUserCfg->stVOParams.iVoLayer	    = 0;
    pUserCfg->stVOParams.iMiniGUILayer	= HLAY(2, 0);
#ifndef TRANS_VO_TO_LCD  //HDMI
    pUserCfg->stVOParams.iDispType		= VO_INTF_HDMI;
    pUserCfg->stVOParams.iDispSync		= VO_OUTPUT_1080P50;
    pUserCfg->stVOParams.iWidth 		= VO_HDMI_DISPLAY_WIDTH;
    pUserCfg->stVOParams.iHeight 		= VO_HDMI_DISPLAY_HIGHT;
#else  //LCD
    pUserCfg->stVOParams.iDispType		= VO_INTF_LCD;
    pUserCfg->stVOParams.iDispSync		= VO_OUTPUT_NTSC;
    pUserCfg->stVOParams.iWidth 		= VO_LCD_DISPLAY_WIDTH;
    pUserCfg->stVOParams.iHeight 		= VO_LCD_DISPLAY_HIGHT;
#endif
    pUserCfg->stVOParams.iFrameNum 	    = TEST_FRAME_NUM;

    // Inital VENC params
    pUserCfg->stVENCParams.iFrameNum        = TEST_FRAME_NUM;
    strcpy(pUserCfg->stVENCParams.szOutputFile, "test_out.h264");  //����ļ�
    pUserCfg->stVENCParams.srcWidth		    = VI_CAPTURE_WIDTH;
    pUserCfg->stVENCParams.srcHeight 		= VI_CAPTURE_HIGHT;
    pUserCfg->stVENCParams.srcSize		    = VI_CAPTURE_WIDTH;
    pUserCfg->stVENCParams.srcPixFmt 		= MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420; //NV21

    pUserCfg->stVENCParams.dstWidth  		= VI_CAPTURE_WIDTH;
    pUserCfg->stVENCParams.dstHeight 		= VI_CAPTURE_HIGHT;
    pUserCfg->stVENCParams.dstSize		    = VI_CAPTURE_WIDTH;
    pUserCfg->stVENCParams.dstPixFmt 		= MM_PIXEL_FORMAT_YVU_SEMIPLANAR_420; //NV21

    pUserCfg->stVENCParams.mVideoEncoderFmt = PT_H264; // PT_H265  PT_MJPEG   TODO:�˴���Ӧ���ô˲���
    pUserCfg->stVENCParams.mField 		    = VIDEO_FIELD_FRAME;
    pUserCfg->stVENCParams.mVideoBitRate 	= 4 * 1024 * 1024;
    pUserCfg->stVENCParams.mVideoFrameRate  = 30;
    pUserCfg->stVENCParams.maxKeyFrame 	    = 20;

    pUserCfg->stVENCParams.mEncUseProfile   = 2; // profile 0-low 1-mid 2-high
    pUserCfg->stVENCParams.mRcMode 		    = 0; // rc_mode
    pUserCfg->stVENCParams.rotate 		    = ROTATE_NONE; // ROTATE_90 ROTATE_180 ROTATE_270

    // Inital OSD params
    pUserCfg->stOSDParams.mOverlayHandle  = 0;
    pUserCfg->stOSDParams.mMppChn.mModId  = MOD_ID_VIU;
    pUserCfg->stOSDParams.mMppChn.mDevId  = VIPP_1;
    pUserCfg->stOSDParams.mMppChn.mChnId  = VirViChn_0;

    // Inital Picture Params
    pUserCfg->stPictureParam.ePicState = MPP_StateIdle;
    pthread_mutex_init(&pUserCfg->stPictureParam.lockPicture , NULL);

    pUserCfg->stPictureParam.pEveResult = (AW_AI_EVE_EVENT_RESULT_S*)malloc(sizeof(AW_AI_EVE_EVENT_RESULT_S));
    memset(pUserCfg->stPictureParam.pEveResult,0,sizeof(AW_AI_EVE_EVENT_RESULT_S));

    unsigned int uPhyAddr;
    void *pVirtAddr;
    int nFrameSize = pUserCfg->stVENCParams.srcWidth * pUserCfg->stVENCParams.srcHeight * 3 / 2;
    int nPixelSize = pUserCfg->stVENCParams.srcWidth * pUserCfg->stVENCParams.srcHeight;

    AW_MPI_SYS_MmzAlloc_Cached(&uPhyAddr, &pVirtAddr, nFrameSize);
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mpVirAddr[0]  = (void *)pVirtAddr;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mpVirAddr[1]  = (void *)pVirtAddr + nPixelSize;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mPhyAddr[0]   = (unsigned int)uPhyAddr;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mPhyAddr[1]   = (unsigned int)uPhyAddr + nPixelSize;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mWidth        = pUserCfg->stVENCParams.srcWidth;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mHeight       = pUserCfg->stVENCParams.srcHeight;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetLeft   = 0;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetTop    = 0;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetRight  = pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetLeft + pUserCfg->stPictureParam.stVideoFrame.VFrame.mWidth;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetBottom = pUserCfg->stPictureParam.stVideoFrame.VFrame.mOffsetTop + pUserCfg->stPictureParam.stVideoFrame.VFrame.mHeight;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mField        = VIDEO_FIELD_FRAME;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mVideoFormat  = VIDEO_FORMAT_LINEAR;
    pUserCfg->stPictureParam.stVideoFrame.VFrame.mCompressMode = COMPRESS_MODE_NONE;

    // Config UserCfg
    pUserCfg->stThreadData[PROC_EVE].ProcessFunc   = EVE_Proc;
    pUserCfg->stThreadData[PROC_EVE].mThreadID     = 0;
    pUserCfg->stThreadData[PROC_EVE].pPrivateData  = NULL;

    pUserCfg->stThreadData[PROC_VO].ProcessFunc    = VO_Proc;
    pUserCfg->stThreadData[PROC_VO].mThreadID      = 0;
    pUserCfg->stThreadData[PROC_VO].pPrivateData   = NULL;

    pUserCfg->stThreadData[PROC_VENC].ProcessFunc  = VENC_Proc;
    pUserCfg->stThreadData[PROC_VENC].mThreadID    = 0;
    pUserCfg->stThreadData[PROC_VENC].pPrivateData = NULL;

    pUserCfg->stThreadData[PROC_JPEG].ProcessFunc   = JPEG_Proc;
    pUserCfg->stThreadData[PROC_JPEG].mThreadID     = 0;
    pUserCfg->stThreadData[PROC_JPEG].pPrivateData  = NULL;

    return 0;
}

void sysconfig_destroy(UserConfig* pUserCfg)
{
    AW_MPI_SYS_MmzFree((unsigned int)pUserCfg->stPictureParam.stVideoFrame.VFrame.mPhyAddr[0]
                       ,(void *)pUserCfg->stPictureParam.stVideoFrame.VFrame.mpVirAddr[0]
                      );
    free(pUserCfg->stPictureParam.pEveResult);
    pthread_mutex_destroy(&pUserCfg->stPictureParam.lockPicture);
    return;
}

int main(int argc, char *argv[])
{
    printf("sample_face_detect buile time = %s, %s.\r\n", __DATE__, __TIME__);
    int ret = -1;

    // ����MPP
    mpp_init();

    // ���ò���
    UserConfig stUserCfg;
    sysconfig_init(&stUserCfg);

    create_vi(stUserCfg.stVirViParams);
    create_eve(&stUserCfg.stEVEParams);
    create_vo(&stUserCfg.stVOParams);

    // Create Process thread
    for (int ProcIndex = 0; ProcIndex < PROC_MAX; ProcIndex++) {
        // �������ݲɼ��߳�
        ret = pthread_create(&stUserCfg.stThreadData[ProcIndex].mThreadID
                             ,NULL
                             ,stUserCfg.stThreadData[ProcIndex].ProcessFunc
                             ,(void*)&stUserCfg
                            );
        if (ret) {
            aloge("chn 0 Save YUV Thread exit! ret[%d]",ret);
            abort();
        }
    }

    // �ȴ������߳̽���
    for (int ProcIndex = 0; ProcIndex < PROC_MAX; ProcIndex++) {
        pthread_join(stUserCfg.stThreadData[ProcIndex].mThreadID, (void*)&ret);
        alogd("Thread %d Exit!", ProcIndex);
    }

    destroy_vo(&stUserCfg.stVOParams);
    destroy_eve(&stUserCfg.stEVEParams);
    destroy_vi(stUserCfg.stVirViParams);

    // �����û�����
    sysconfig_destroy(&stUserCfg);

    // ����MPP
    mpp_destroy();

    printf("sample_face_detect exit!\n");
    return 0;
}
