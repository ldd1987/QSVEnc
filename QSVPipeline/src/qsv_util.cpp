﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  -----------------------------------------------------------------------------------------

#include <stdio.h>
#include "mfxStructures.h"
#include "mfxvideo.h"
#include "mfxvideo++.h"
#include "qsv_util.h"

BOOL Check_HWUsed(mfxIMPL impl) {
	static const int HW_list[] = {
		MFX_IMPL_HARDWARE,
		MFX_IMPL_HARDWARE_ANY,
		MFX_IMPL_HARDWARE2,
		MFX_IMPL_HARDWARE3,
		MFX_IMPL_HARDWARE4,
		NULL
	};
	for (int i = 0; HW_list[i]; i++)
		if (HW_list[i] == (HW_list[i] & (int)impl))
			return TRUE;
	return FALSE;
}

mfxVersion get_mfx_lib_version(mfxIMPL impl) {
	int i;
	for (i = 1; LIB_VER_LIST[i].Major; i++) {
		MFXVideoSession test;
		mfxVersion ver;
		memcpy(&ver, &LIB_VER_LIST[i], sizeof(mfxVersion));
		mfxStatus sts = test.Init(impl, &ver);
		if (sts != MFX_ERR_NONE)
			break;
	}
	return LIB_VER_LIST[i-1];
}

mfxVersion get_mfx_libhw_version() {
	static const mfxU32 impl_list[] = {
		MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11,
		MFX_IMPL_HARDWARE_ANY,
		MFX_IMPL_HARDWARE,
	};
	mfxVersion test = { 0 };
	//Win7でD3D11のチェックをやると、
	//デスクトップコンポジションが切られてしまう問題が発生すると報告を頂いたので、
	//D3D11をWin8以降に限定
	for (int i = (check_OS_Win8orLater() ? 0 : 1); i < _countof(impl_list); i++) {
		test = get_mfx_lib_version(impl_list[i]);
		if (check_lib_version(test, MFX_LIB_VERSION_1_1))
			break;
	}
	return test;
}
bool check_if_d3d11_necessary() {
	bool check_d3d11 = (0 != check_lib_version(get_mfx_lib_version(MFX_IMPL_HARDWARE_ANY | MFX_IMPL_VIA_D3D11), MFX_LIB_VERSION_1_1));
	bool check_d3d9  = (0 != check_lib_version(get_mfx_lib_version(MFX_IMPL_HARDWARE_ANY), MFX_LIB_VERSION_1_1));

	return (check_d3d11 == true && check_d3d9 == false);
}
mfxVersion get_mfx_libsw_version() {
	return get_mfx_lib_version(MFX_IMPL_SOFTWARE);
}

BOOL check_lib_version(mfxVersion value, mfxVersion required) {
	if (value.Major < required.Major)
		return FALSE;
	if (value.Major > required.Major)
		return TRUE;
	if (value.Minor < required.Minor)
		return FALSE;
	return TRUE;
}

mfxU32 GCD(mfxU32 a, mfxU32 b)
{
	if (0 == a)
		return b;
	else if (0 == b)
		return a;

	mfxU32 a1, b1;

	if (a >= b)
	{
		a1 = a;
		b1 = b;
	}
	else
	{
		a1 = b;
		b1 = a;
	}

	// a1 >= b1;
	mfxU32 r = a1 % b1;

	while (0 != r)
	{
		a1 = b1;
		b1 = r;
		r = a1 % b1;
	}

	return b1;
}
mfxI64 GCDI64(mfxI64 a, mfxI64 b)
{
	if (0 == a)
		return b;
	else if (0 == b)
		return a;

	mfxI64 a1, b1;

	if (a >= b)
	{
		a1 = a;
		b1 = b;
	}
	else
	{
		a1 = b;
		b1 = a;
	}

	// a1 >= b1;
	mfxI64 r = a1 % b1;

	while (0 != r)
	{
		a1 = b1;
		b1 = r;
		r = a1 % b1;
	}

	return b1;
}


BOOL check_lib_version(mfxU32 _value, mfxU32 _required) {
	mfxVersion value, required;
	value.Version = _value;
	required.Version = _required;
	if (value.Major < required.Major)
		return FALSE;
	if (value.Major > required.Major)
		return TRUE;
	if (value.Minor < required.Minor)
		return FALSE;
	return TRUE;
}

void adjust_sar(int *sar_w, int *sar_h, int width, int height) {
	int aspect_w = *sar_w;
	int aspect_h = *sar_h;
	//正負チェック
	if (aspect_w * aspect_h <= 0)
		aspect_w = aspect_h = 0;
	else if (aspect_w < 0) {
		//負で与えられている場合はDARでの指定
		//SAR比に変換する
		int dar_x = -1 * aspect_w;
		int dar_y = -1 * aspect_h;
		int x = dar_x * height;
		int y = dar_y * width;
		//多少のづれは容認する
		if (abs(y - x) > 16 * dar_y) {
			//gcd
			int a = x, b = y, c;
			while ((c = a % b) != 0)
				a = b, b = c;
			*sar_w = x / b;
			*sar_h = y / b;
		} else {
			 *sar_w = *sar_h = 1;
		}
	} else {
		//sarも一応gcdをとっておく
		int a = aspect_w, b = aspect_h, c;
		while ((c = a % b) != 0)
			a = b, b = c;
		*sar_w = aspect_w / b;
		*sar_h = aspect_h / b;
	}
}

const TCHAR *get_err_mes(int sts) {
	switch (sts) {
		case MFX_ERR_NONE:                     return _T("no error.");
		case MFX_ERR_UNKNOWN:                  return _T("unknown error.");
		case MFX_ERR_NULL_PTR:                 return _T("null pointer.");
		case MFX_ERR_UNSUPPORTED:              return _T("undeveloped feature.");
		case MFX_ERR_MEMORY_ALLOC:             return _T("failed to allocate memory.");
		case MFX_ERR_NOT_ENOUGH_BUFFER:        return _T("insufficient buffer at input/output.");
		case MFX_ERR_INVALID_HANDLE:           return _T("invalid handle.");
		case MFX_ERR_LOCK_MEMORY:              return _T("failed to lock the memory block.");
		case MFX_ERR_NOT_INITIALIZED:          return _T("member function called before initialization.");
		case MFX_ERR_NOT_FOUND:                return _T("the specified object is not found.");
		case MFX_ERR_MORE_DATA:                return _T("expect more data at input.");
		case MFX_ERR_MORE_SURFACE:             return _T("expect more surface at output.");
		case MFX_ERR_ABORTED:                  return _T("operation aborted.");
		case MFX_ERR_DEVICE_LOST:              return _T("lose the HW acceleration device.");
		case MFX_ERR_INCOMPATIBLE_VIDEO_PARAM: return _T("incompatible video parameters.");
		case MFX_ERR_INVALID_VIDEO_PARAM:      return _T("invalid video parameters.");
		case MFX_ERR_UNDEFINED_BEHAVIOR:       return _T("undefined behavior.");
		case MFX_ERR_DEVICE_FAILED:            return _T("device operation failure.");
		
		case MFX_WRN_IN_EXECUTION:             return _T("the previous asynchrous operation is in execution.");
		case MFX_WRN_DEVICE_BUSY:              return _T("the HW acceleration device is busy.");
		case MFX_WRN_VIDEO_PARAM_CHANGED:      return _T("the video parameters are changed during decoding.");
		case MFX_WRN_PARTIAL_ACCELERATION:     return _T("SW is used.");
		case MFX_WRN_INCOMPATIBLE_VIDEO_PARAM: return _T("incompatible video parameters.");
		case MFX_WRN_VALUE_NOT_CHANGED:        return _T("the value is saturated based on its valid range.");
		case MFX_WRN_OUT_OF_RANGE:             return _T("the value is out of valid range.");
		default:                               return _T("unknown error."); 
	}
}

mfxStatus ParseY4MHeader(char *buf, mfxFrameInfo *info) {
	char *p, *q = NULL;
	memset(info, 0, sizeof(mfxFrameInfo));
	for (p = buf; (p = strtok_s(p, " ", &q)) != NULL; ) {
		switch (*p) {
			case 'W':
				{
					char *eptr = NULL;
					int w = strtol(p+1, &eptr, 10);
					if (*eptr == '\0' && w)
						info->Width = (mfxU16)w;
				}
				break;
			case 'H':
				{
					char *eptr = NULL;
					int h = strtol(p+1, &eptr, 10);
					if (*eptr == '\0' && h)
						info->Height = (mfxU16)h;
				}
				break;
			case 'F':
				{
					int rate = 0, scale = 0;
					if (   (info->FrameRateExtN == 0 || info->FrameRateExtD == 0)
						&& sscanf_s(p+1, "%d:%d", &rate, &scale) == 2) {
							if (rate && scale) {
								info->FrameRateExtN = rate;
								info->FrameRateExtD = scale;
							}
					}
				}
				break;
			case 'A':
				{
					int sar_x = 0, sar_y = 0;
					if (   (info->AspectRatioW == 0 || info->AspectRatioH == 0)
						&& sscanf_s(p+1, "%d:%d", &sar_x, &sar_y) == 2) {
							if (sar_x && sar_y) {
								info->AspectRatioW = (mfxU16)sar_x;
								info->AspectRatioH = (mfxU16)sar_y;
							}
					}
				}
				break;
			case 'I':
				switch (*(p+1)) {
			case 'b':
				info->PicStruct = MFX_PICSTRUCT_FIELD_BFF;
				break;
			case 't':
			case 'm':
				info->PicStruct = MFX_PICSTRUCT_FIELD_TFF;
				break;
			default:
				break;
				}
				break;
			case 'C':
				if (   0 != _strnicmp(p+1, "420",      strlen("420"))
					&& 0 != _strnicmp(p+1, "420mpeg2", strlen("420mpeg2"))
					&& 0 != _strnicmp(p+1, "420jpeg",  strlen("420jpeg"))
					&& 0 != _strnicmp(p+1, "420paldv", strlen("420paldv"))) {
					return MFX_PRINT_OPTION_ERR;
				}
				break;
			default:
				break;
		}
		p = NULL;
	}
	return MFX_ERR_NONE;
}

BOOL check_OS_Win8orLater() {
	OSVERSIONINFO osvi = { 0 };
	osvi.dwOSVersionInfoSize = sizeof(OSVERSIONINFO);
	GetVersionEx(&osvi);
	return ((osvi.dwPlatformId == VER_PLATFORM_WIN32_NT) && ((osvi.dwMajorVersion == 6 && osvi.dwMinorVersion >= 2) || osvi.dwMajorVersion > 6));
}

#include <intrin.h>

bool isHaswellOrLater() {
	//単純にAVX2フラグを見る
	int CPUInfo[4];
	__cpuid(CPUInfo, 7);
	return ((CPUInfo[1] & 0x00000020) == 0x00000020);
}