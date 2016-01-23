﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#ifndef _QSV_VERSION_H_
#define _QSV_VERSION_H_

#define VER_FILEVERSION             0,2,28,0
#define VER_STR_FILEVERSION          "2.28"
#define VER_STR_FILEVERSION_TCHAR _T("2.28")

#ifdef _M_IX86
#define BUILD_ARCH_STR _T("x86")
#else
#define BUILD_ARCH_STR _T("x64")
#endif

#if _UNICODE
const wchar_t *get_qsvenc_version();
#else
const char *get_qsvenc_version();
#endif

#if defined(_WIN32) || defined(_WIN64)

#define D3D_SURFACES_SUPPORT 1

#define ENABLE_FEATURE_COP3_AND_ABOVE 1

#define ENABLE_ADVANCED_DEINTERLACE 0

#define ENABLE_MVC_ENCODING 0

#define ENABLE_FPS_CONVERSION 0

#define ENABLE_OPENCL 1

#define ENABLE_SESSION_THREAD_CONFIG 0

#define ENABLE_AVCODEC_OUT_THREAD 1

#define ENABLE_AVCODEC_AUDPROCESS_THREAD 1

#ifndef QSVENC_AUO
#define ENABLE_AUO_LINK           0
#define ENABLE_AVI_READER         1
#define ENABLE_AVISYNTH_READER    1
#define ENABLE_VAPOURSYNTH_READER 1
#define ENABLE_AVCODEC_QSV_READER 1
#define ENABLE_CUSTOM_VPP         1
#else
#define QSV_AUO_NAME              "QSVEnc.auo"
#define ENABLE_AUO_LINK           1
#define ENABLE_AVI_READER         0
#define ENABLE_AVISYNTH_READER    0
#define ENABLE_VAPOURSYNTH_READER 0
#define ENABLE_AVCODEC_QSV_READER 1
#define ENABLE_CUSTOM_VPP         1
#endif

#else //_MSC_VER
#include "qsv_config.h"
#endif //_MSC_VER

#endif //_QSV_VERSION_H_