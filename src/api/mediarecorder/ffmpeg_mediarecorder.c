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

#ifdef _DEBUG
#ifdef OS_WIN
#define snprintf _snprintf
#endif
#include <libavutil/timestamp.h>
static void log_packet(const AVFormatContext *fmt_ctx, const AVPacket *pkt)
{
  AVRational *time_base = &fmt_ctx->streams[pkt->stream_index]->time_base;

  printf("pts:%s pts_time:%s dts:%s dts_time:%s duration:%s duration_time:%s stream_index:%d\n",
    av_ts2str(pkt->pts), av_ts2timestr(pkt->pts, time_base),
    av_ts2str(pkt->dts), av_ts2timestr(pkt->dts, time_base),
    av_ts2str(pkt->duration), av_ts2timestr(pkt->duration, time_base),
    pkt->stream_index);
}
#endif

static int write_frame(AVFormatContext *fmt_ctx, const AVRational *time_base, AVStream *st, AVPacket *pkt)
{
  /* rescale output packet timestamp values from codec to stream timebase */
  av_packet_rescale_ts(pkt, *time_base, st->time_base);
  pkt->stream_index = st->index;
#ifdef _DEBUG
  log_packet(fmt_ctx, pkt);
#endif
  /* Write the compressed frame to the media file. */
  return av_interleaved_write_frame(fmt_ctx, pkt);
}

/* Add an output stream. */
int add_stream(OutputStream *ost, AVFormatContext *oc,
  AVCodec **codec, int codec_id, int bitrate,
  int samplerate, int channels,// audio
  short width, short height, char framerate, int AVPixelFormat //video
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
    c->bit_rate = bitrate;
    c->sample_rate = samplerate;
    if ((*codec)->supported_samplerates) {
      c->sample_rate = (*codec)->supported_samplerates[0];
      for (i = 0; (*codec)->supported_samplerates[i]; i++) {
        if ((*codec)->supported_samplerates[i] == samplerate)
          c->sample_rate = samplerate;
      }
    }
    // currently VORBIS only support 2 channel encoding !!
    if (codec_id == AV_CODEC_ID_VORBIS) channels = 2;
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
    ost->st->time_base = (AVRational){ 1, c->sample_rate };
    break;

  case AVMEDIA_TYPE_VIDEO:
    c->codec_id = codec_id;

    c->bit_rate = bitrate;
    /* Resolution must be a multiple of two. */
    c->width = width;
    c->height = height;
    /* timebase: This is the fundamental unit of time (in seconds) in terms
    * of which frame timestamps are represented. For fixed-fps content,
    * timebase should be 1/framerate and timestamp increments should be
    * identical to 1. */
    ost->st->time_base = (AVRational){ 1, framerate };
    c->time_base = ost->st->time_base;

    c->gop_size = 12; /* emit one intra frame every twelve frames at most */
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

int open_audio(AVFormatContext *oc, AVCodec *codec, OutputStream *ost, const int samplerate, const int channels)
{
  AVCodecContext *c;
  int nb_samples;
  int ret;
  AVDictionary *opt = NULL;

  c = ost->st->codec;

  /* open it */
  ret = avcodec_open2(c, codec, &opt);
  if (ret < 0) {
    fprintf(stderr, "Could not open audio codec: %s\n", av_err2str(ret));
    return -1;
  }

  if (c->codec->capabilities & CODEC_CAP_VARIABLE_FRAME_SIZE)
    nb_samples = 10000;
  else
    nb_samples = c->frame_size;

  // Source
  ost->tmp_frame = alloc_audio_frame(AV_SAMPLE_FMT_FLTP, av_get_default_channel_layout(channels),
    samplerate, nb_samples);
    
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
* encode one audio frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
int write_audio_frame(AVFormatContext *oc, OutputStream *ost, AVFrame* frame)
{
  AVCodecContext *c;
  AVPacket pkt = { 0 }; // data and size must be 0;
  int ret;
  int got_packet;
  int dst_nb_samples;

  av_init_packet(&pkt);
  c = ost->st->codec;

  if (frame && ost->swr_ctx) {
    /* convert samples from native format to destination codec format, using the resampler */
    /* compute destination number of samples */
    dst_nb_samples = av_rescale_rnd(swr_get_delay(ost->swr_ctx, c->sample_rate) + frame->nb_samples,
      c->sample_rate, c->sample_rate, AV_ROUND_UP);
    av_assert0(dst_nb_samples == frame->nb_samples);

    /* when we pass a frame to the encoder, it may keep a reference to it
    * internally;
    * make sure we do not overwrite it here
    */
    ret = av_frame_make_writable(ost->frame);
    if (ret < 0)
      return -1;

    /* convert to destination format */
    ret = swr_convert(ost->swr_ctx,
      ost->frame->data, dst_nb_samples,
      (const uint8_t **)frame->data, frame->nb_samples);
    if (ret < 0) {
      fprintf(stderr, "Error while converting\n");
      return -1;
    }
    ost->frame->pts = frame->pts;
    frame = ost->frame;
  }

  ret = avcodec_encode_audio2(c, &pkt, frame, &got_packet);
  if (ret < 0) {
    fprintf(stderr, "Error encoding audio frame: %s\n", av_err2str(ret));
    return -1;
  }

  if (got_packet) {
    ret = write_frame(oc, &c->time_base, ost->st, &pkt);
    if (ret < 0) {
      fprintf(stderr, "Error while writing audio frame: %s\n",
        av_err2str(ret));
      return -1;
    }
  }

  return (frame || got_packet) ? 0 : 1;
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

int open_video(AVFormatContext *oc, AVCodec *codec, OutputStream *ost)
{
  int ret;
  AVCodecContext *c = ost->st->codec;
  AVDictionary *opt = NULL;

  /* open the codec */
  ret = avcodec_open2(c, codec, &opt);
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

  ost->tmp_frame = NULL;
  return 0;
}

/*
* encode one video frame and send it to the muxer
* return 1 when encoding is finished, 0 otherwise
*/
 int write_video_frame(AVFormatContext *oc, OutputStream *ost, AVFrame *frame)
{
  int ret = -1;
  AVCodecContext *c;
  int got_packet = 0;

  c = ost->st->codec;

  if (oc->oformat->flags & AVFMT_RAWPICTURE) {
    /* a hack to avoid data copy with some raw video muxers */
    AVPacket pkt;
    av_init_packet(&pkt);

    if (!frame)
      return 1;

    pkt.flags |= AV_PKT_FLAG_KEY;
    pkt.stream_index = ost->st->index;
    pkt.data = (uint8_t *)frame;
    pkt.size = sizeof(AVPicture);

    pkt.pts = pkt.dts = frame->pts;
    av_packet_rescale_ts(&pkt, c->time_base, ost->st->time_base);

    ret = av_interleaved_write_frame(oc, &pkt);
  }
  else {
    AVPacket pkt = { 0 };
    av_init_packet(&pkt);

    /* encode the image */
    ret = avcodec_encode_video2(c, &pkt, frame, &got_packet);
    if (ret < 0) {
      fprintf(stderr, "Error encoding video frame: %s\n", av_err2str(ret));
      return -1;
    }

    if (got_packet) {
      ret = write_frame(oc, &c->time_base, ost->st, &pkt);
    }
    else {
      ret = 0;
    }
  }

  if (ret < 0) {
    fprintf(stderr, "Error while writing video frame: %s\n", av_err2str(ret));
    return -1;
  }

  return (frame || got_packet) ? 0 : 1;
}

void close_stream(AVFormatContext *oc, OutputStream *ost)
{
  avcodec_close(ost->st->codec);
  av_frame_free(&ost->frame);
  av_frame_free(&ost->tmp_frame);
  sws_freeContext(ost->sws_ctx);
  swr_free(&ost->swr_ctx);
}
