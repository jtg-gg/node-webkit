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

#define inline __inline
#define strtoll _strtoi64
#define _ISOC99_SOURCE
#define _LARGEFILE_SOURCE
#define FF_API_PIX_FMT_DESC 0
#define FF_API_OLD_DECODE_AUDIO 0
#define FF_API_DESTRUCT_PACKET 0
#define FF_API_GET_BUFFER 0

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

#include "base/compiler_specific.h"

MSVC_PUSH_DISABLE_WARNING(4204);
MSVC_PUSH_DISABLE_WARNING(4211);
MSVC_PUSH_DISABLE_WARNING(4701);
MSVC_PUSH_DISABLE_WARNING(4703);

#include <libavutil/avassert.h>
#include <libavutil/channel_layout.h>
#include <libavutil/opt.h>
#include <libavutil/mathematics.h>
#include <libavformat/avformat.h>
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>

#include "ffmpeg_mediarecorder.h"

#define SCALE_FLAGS SWS_BICUBIC

//#define VERBOSE_DEBUG
#ifdef VERBOSE_DEBUG
#ifdef OS_WIN
#include <Windows.h>
#define snprintf _snprintf
#endif
#include <libavutil/timestamp.h>
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
  AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;
#ifdef OS_WIN
  char temp[1024];
  sprintf(temp,
#else
  printf(
#endif
    "pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
    av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
    av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
    av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
    pkt->stream_index);

#ifdef OS_WIN
  OutputDebugStringA(temp);
#endif
}
#endif

int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
  /* rescale output packet timestamp values from codec to stream timebase */
  av_packet_rescale_ts(pkt, *time_base, st->time_base);
  pkt->stream_index = st->index;
#ifdef VERBOSE_DEBUG
  log_packet(fmt_ctx, pkt);
#endif
  /* Write the compressed frame to the media file. */
  return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
int add_stream(OutputStream *ost, AVFormatContext *oc,
  AVCodec **codec, int codec_id, int bitrate,
  int samplerate, int channels,// audio
  short width, short height, int AVPixelFormat //video
  )
{
  AVCodecContext *c;
  int i;

  /* find the encoder */
  *codec = avcodec_find_encoder(codec_id);
  if (!(*codec)) {
    fprintf(stderr, "Could not find encoder for '%s'\n",
      avcodec_get_name(codec_id));
    return -1;
  }

  ost->st = avformat_new_stream(oc, *codec);
  if (!ost->st) {
    fprintf(stderr, "Could not allocate stream\n");
    return -1;
  }
  ost->st->id = oc->nb_streams - 1;
  c = ost->st->codec;

  switch ((*codec)->type) {
  case AVMEDIA_TYPE_AUDIO:
    c->sample_fmt = (*codec)->sample_fmts ?
      (*codec)->sample_fmts[0] : AV_SAMPLE_FMT_FLTP;
    c->sample_rate = samplerate;
    if ((*codec)->supported_samplerates) {
      const int* supported_samplerates = (*codec)->supported_samplerates;
      c->sample_rate = *supported_samplerates;
      for (i = 0; *supported_samplerates; supported_samplerates++) {
        if (*supported_samplerates >= samplerate)
          c->sample_rate = *supported_samplerates;
      }
    }
    c->channels = channels;
    uint64_t defChannelLayout = av_get_default_channel_layout(channels);
    c->channel_layout = defChannelLayout;
    if ((*codec)->channel_layouts) {
      c->channel_layout = (*codec)->channel_layouts[0];
      for (i = 0; (*codec)->channel_layouts[i]; i++) {
        if ((*codec)->channel_layouts[i] == defChannelLayout)
          c->channel_layout = defChannelLayout;
      }
    }
    break;

  case AVMEDIA_TYPE_VIDEO:
    c->codec_id = codec_id;

    c->bit_rate = bitrate;
    /* Resolution must be a multiple of two. */
    c->width = width;
    c->height = height;
    if (c->codec_id == AV_CODEC_ID_VP8) {
      if (!c->bit_rate) c->bit_rate = width * height * 10 / 3;
    }
    c->pix_fmt = AVPixelFormat;
    if (c->codec_id == AV_CODEC_ID_MPEG2VIDEO) {
      /* just for testing, we also add B frames */
      c->max_b_frames = 2;
    }
    if (c->codec_id == AV_CODEC_ID_MPEG1VIDEO) {
      /* Needed to avoid using macroblocks in which some coeffs overflow.
      * This does not happen with normal video, it just happens here as
      * the motion of the chroma plane does not match the luma plane. */
      c->mb_decision = 2;
    }
    break;

  default:
    break;
  }

  /* Some formats want stream headers to be separate. */
  if (oc->oformat->flags & AVFMT_GLOBALHEADER)
    c->flags |= CODEC_FLAG_GLOBAL_HEADER;

  return 0;
}

/**************************************************************/
/* audio output */

static AVFrame *alloc_audio_frame(enum AVSampleFormat sample_fmt,
  uint64_t channel_layout,
  int sample_rate, int nb_samples)
{
  AVFrame *frame = av_frame_alloc();
  int ret;

  if (!frame) {
    fprintf(stderr, "Error allocating an audio frame\n");
    return NULL;
  }

  frame->format = sample_fmt;
  frame->channel_layout = channel_layout;
  frame->sample_rate = sample_rate;
  frame->nb_samples = nb_samples;

  if (nb_samples) {
    ret = av_frame_get_buffer(frame, 0);
    if (ret < 0) {
      fprintf(stderr, "Error allocating an audio buffer\n");
      return NULL;
    }
  }

  return frame;
}

int open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, const int samplerate, const int channels, const int frame_size, AVDictionary **opt)
{
  AVCodecContext *c;
  int nb_samples;
  int ret;

  c = ost->st->codec;

  /* open it */
  ret = avcodec_open2(c, codec, opt);
  if (ret < 0) {
    fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
    return -1;
  }
  
  c->time_base = (AVRational){ 1, c->sample_rate };  
  ost->st->time_base = (AVRational){ 1, c->sample_rate };
  
  if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
    nb_samples = frame_size;
  else
    nb_samples = c->frame_size;

  // Source
  ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_FLTP, av_get_default_channel_layout(channels),
    samplerate, frame_size);

  // Destination
  ost->frame = alloc_audio_frame(c->sample_fmt, c->channel_layout,
    c->sample_rate, nb_samples);
  /* create resampler context */
  ost->swr_ctx = swr_alloc();
  if (!ost->swr_ctx) {
    fprintf(stderr, "Could not allocate resampler context\n");
    return -1;
  }

  /* set options */
  av_opt_set_int(ost->swr_ctx, "in_channel_count", channels, 0);
  av_opt_set_int(ost->swr_ctx, "in_sample_rate", samplerate, 0);
  av_opt_set_sample_fmt(ost->swr_ctx, "in_sample_fmt", AV_SAMPLE_FMT_FLTP, 0);

  av_opt_set_int(ost->swr_ctx, "out_channel_count", c->channels, 0);
  av_opt_set_int(ost->swr_ctx, "out_sample_rate", c->sample_rate, 0);
  av_opt_set_sample_fmt(ost->swr_ctx, "out_sample_fmt", c->sample_fmt, 0);

  /* initialize the resampling context */
  if ((ret = swr_init(ost->swr_ctx)) < 0) {
    fprintf(stderr, "Failed to initialize the resampling context\n");
    return -1;
  }
  return 0;
}

/*
* encode one audio frame (tmp_frame) until it returns a packet
* srcNumSamples should be tmp_frame->nb_samples, 0 when doing loop, to empty the buffer
* return 1 when packet is exist and the buffer not yet empty
* return 0 when packet is exist and the buffer is "empty"
* return -1 error
* return -2 to break the caller loop (dest nb_samples > src)
*/
int write_audio_frame(OutputStream *ost, AVPacket* pkt, int srcNumSamples)
{
  AVCodecContext *c;
  int ret;
  int got_packet;
  int delay;
  int converted;

  av_init_packet(pkt);
  c = ost->st->codec;

  /* when we pass a frame to the encoder, it may keep a reference to it
  * internally;
  * make sure we do not overwrite it here
  */
  ret = av_frame_make_writable(ost->frame);
  if (ret < 0)
    return -1;
  
  do {
    // if the dest nb_samples > src, we need to fill it until it is full, before we can send the frame to encoder
    // frame_sample is the current index
    uint8_t *dest[AV_NUM_DATA_POINTERS];
    memcpy(dest, ost->frame->data, AV_NUM_DATA_POINTERS);
    for (int i=0; i<AV_NUM_DATA_POINTERS; i++){
      if (ost->frame->data[i]==0) break;
      dest[i] = ost->frame->data[i] + ost->frame_sample * (ost->frame->linesize[0] / ost->frame->nb_samples);
    }

    /* convert to destination format */
    converted = swr_convert(ost->swr_ctx,
                      dest, ost->frame->nb_samples - ost->frame_sample,
                      (const uint8_t **)ost->tmp_frame->data, srcNumSamples);

    if (converted < 0) {
      fprintf(stderr, "Error while converting\n");
      return -1;
    }

#ifdef VERBOSE_DEBUG
    delay = swr_get_delay(ost->swr_ctx, ost->frame->sample_rate);
#endif

    // returns if destination frame is not yet full
    ost->frame_sample += converted;
    if (ost->frame_sample < ost->frame->nb_samples) {
#ifdef VERBOSE_DEBUG
      printf("converted %d, delay %d, pts %lld\n",converted, delay, ost->frame->pts);
#endif
      return -2;
    }
    
    // in this line, it is guaranteed the dest frame is full, reset the frame_sample
    av_assert0(ost->frame_sample == ost->frame->nb_samples);
    ost->frame_sample = 0;
    ost->frame->pts += ost->frame->nb_samples;

#ifdef VERBOSE_DEBUG
    //enable this for debugging, the converted must be == ost->frame->nb_samples, 64 for ogg
    printf("converted %d, delay %d, pts %lld\n",converted, delay, ost->frame->pts);
#endif

    ret = avcodec_encode_audio2(c, pkt, ost->frame, &got_packet);
    if (ret < 0) {
      fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
      return -1;
    }

    // if we are looping, flush the buffer
    srcNumSamples = 0;
    delay = swr_get_delay(ost->swr_ctx, ost->frame->sample_rate);

  // loop until buffer / delay is < 2 times of the sample size
  // break the loop whenever the packet is ready
  } while (delay > ost->frame->nb_samples * 2 && !got_packet);
    

  if (got_packet) {
    if (delay > ost->frame->nb_samples * 2) {
#ifdef VERBOSE_DEBUG
      printf("converted %d, delay %d, pts %lld\n",converted, delay, ost->frame->pts);
#endif
      return 1; //the caller to loop again, the buffer is not yet < 2 * sample size
    }
    else
      return 0;
  }
  
  return -1;
}

/**************************************************************/
/* video output */

AVFrame *alloc_picture(enum AVPixelFormat pix_fmt, int width, int height)
{
  AVFrame *picture;
  int ret;

  picture = av_frame_alloc();
  if (!picture)
    return NULL;

  picture->format = pix_fmt;
  picture->width = width;
  picture->height = height;

  /* allocate the buffers for the frame data */
  ret = av_frame_get_buffer(picture, 32);
  if (ret < 0) {
    fprintf(stderr, "Could not allocate frame data.\n");
    return NULL;
  }

  return picture;
}

int open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, AVDictionary **opt)
{
  int ret;
  AVCodecContext *c = ost->st->codec;

  /* open the codec */
  ret = avcodec_open2(c, codec, opt);
  if (ret < 0) {
    fprintf(stderr, "Could not open video codec: %s\n", av_err2str(ret));
    return -1;
  }

  /* allocate and init a re-usable frame */
  ost->frame = alloc_picture(c->pix_fmt, c->width, c->height);
  if (!ost->frame) {
    fprintf(stderr, "Could not allocate video frame\n");
    return -1;
  }
  
  ost->st->time_base = c->time_base;

  ost->tmp_frame = NULL;
  return 0;
}

/*
* encode one video frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
int write_video_frame(AVFormatContext *oc, OutputStream *ost, AVFrame *frame, AVPacket* pkt)
{
  int ret = -1;
  AVCodecContext *c;
  int got_packet = 0;

  c = ost->st->codec;

  if (oc->oformat->flags & AVFMT_RAWPICTURE) {
    /* a hack to avoid data copy with some raw video muxers */
    av_init_packet(pkt);

    if (!frame)
      return 1;

    pkt->flags |= AV_PKT_FLAG_KEY;
    pkt->data = (uint8_t *)frame;
    pkt->size = sizeof(AVPicture);

    pkt->pts = pkt->dts = frame->pts;
    got_packet = 1;
  }
  else {
    av_init_packet(pkt);

    /* encode the image */
    ret = avcodec_encode_video2(c, pkt, frame, &got_packet);
    if (ret < 0) {
      fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
      return -1;
    }
  }

  return !got_packet;
}

void close_stream(AVFormatContext *oc, OutputStream *ost)
{
  avcodec_close(ost->st->codec);
  av_frame_free(&ost->frame);
  av_frame_free(&ost->tmp_frame);
  sws_freeContext(ost->sws_ctx);
  swr_free(&ost->swr_ctx);
}
