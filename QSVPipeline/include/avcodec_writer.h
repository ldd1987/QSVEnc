﻿//  -----------------------------------------------------------------------------------------
//    QSVEnc by rigaya
//  -----------------------------------------------------------------------------------------
//   ソースコードについて
//   ・無保証です。
//   ・本ソースコードを使用したことによるいかなる損害・トラブルについてrigayaは責任を負いません。
//   以上に了解して頂ける場合、本ソースコードの使用、複製、改変、再頒布を行って頂いて構いません。
//  ---------------------------------------------------------------------------------------
#ifndef _AVCODEC_WRITER_H_
#define _AVCODEC_WRITER_H_

#include "sample_utils.h"

#if ENABLE_AVCODEC_QSV_READER
#include "avcodec_qsv.h"

typedef struct AVMuxer {
	AVFormatContext      *pFormatCtx;       //出力ファイルのformatContext
	AVOutputFormat       *pOutputFmt;       //出力ファイルのoutputFormat
	AVStream             *pStreamAudio;     //出力ファイルの音声ストリーム
	int                   nPacketWritten;   //出力したパケットの数
	AVRational            pktTimebase;      //元の音声ストリームのtimebase
	
	//PCMの変換用
	AVCodec              *pAudioOutCodecDecode;     //変換するPCMの元のコーデック
	AVCodecContext       *pAudioOutCodecDecodeCtx;  //変換するPCMの元のCodecContext
	AVCodec              *pAudioOutCodecEncode;     //変換先のPCMの音声のコーデック
	AVCodecContext       *pAudioOutCodecEncodeCtx;  //変換先のPCMの音声のCodecContext
	AVPacket              audioOutPacket;           //変換用の音声バッファ

	mfxI64                nLastPktDtsAudio; //出力音声のdts

	bool                  bStreamError;     //エラーが発生
} AVMuxer;

typedef struct AvcodecWriterPrm {
	const AVCodecContext *pCodecCtxAudioIn;    //入力ファイルの音声のcodecContext
} AvcodecWriterPrm;

class CAvcodecWriter : public CSmplBitstreamWriter
{
public:
	CAvcodecWriter();
	virtual ~CAvcodecWriter();

	virtual mfxStatus Init(const msdk_char *strFileName, const void *option, CEncodeStatusInfo *pEncSatusInfo) override;

	virtual mfxStatus WriteNextFrame(AVPacket *pkt);

	virtual void Close();
private:
	AVCodecID PCMRequiresConversion(const AVCodecContext *audioCtx);
	AVMuxer m_Muxer;
};

#endif //ENABLE_AVCODEC_QSV_READER

#endif //_AVCODEC_WRITER_H_
