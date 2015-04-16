// Copyright (c) 2015 Jefry Tedjokusumo
// Copyright (c) 2015 The Chromium Authors
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
//  in the Software without restriction, including without limitation the rights
//  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell co
// pies of the Software, and to permit persons to whom the Software is furnished
//  to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in al
// l copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR IM
// PLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY, FITNES
// S FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS
//  OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WH
// ETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
//  CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE SOFTWARE.

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

  struct AVStream;
  struct AVFrame;
  struct AVFormatContext;
  struct AVOutputFormat;
  struct AVCodec;
  struct AVPacket;
  struct AVRational;
  struct AVDictionary;

  // a wrapper around a single output AVStream
  typedef struct OutputStream {
    AVStream *st;

    AVFrame *frame;
    int frame_sample;    // used by audio
    AVFrame *tmp_frame;

    struct SwsContext *sws_ctx;
    struct SwrContext *swr_ctx;
  } OutputStream;


  /* Add an output stream. */
  int add_stream(OutputStream *ost, AVFormatContext *oc,
    AVCodec **codec, int AVCodecID, int bitrate,
    int samplerate, int channels,// audio
    short width, short height, int AVPixelFormat  //video
    );

  int open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, const int samplerate, const int channels, const int frame_size, AVDictionary **opt);
  int open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary **opt);
  void close_stream(AVFormatContext *oc, OutputStream *ost);

  AVFrame *alloc_picture(int AVPixelFormat, int width, int height);

  /*
  * encode one video frame and send it to the muxer
  * return 1 when encoding is finished, 0 otherwise
  */
  int write_video_frame(AVFormatContext *oc, OutputStream *ost, AVFrame *frame, AVPacket* pkt);

  /*
  * encode one audio frame (tmp_frame) until it returns a packet
  * srcNumSamples should be tmp_frame->nb_samples, 0 when doing loop, to empty the buffer
  * return 1 when packet is exist and the buffer not yet empty
  * return 0 when packet is exist and the buffer is "empty"
  * return -1 error
  * return -2 to break the caller loop (dest nb_samples > src)
  */
  int write_audio_frame(OutputStream *ost, AVPacket* pkt, int srcNumSamples);

  int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt);

#ifdef __cplusplus
};
#endif
