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

#include "media/ffmpeg/ffmpeg_common.h"
#include "media/base/audio_bus.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include "ffmpeg_mediarecorder_wrapper.h"

  FFMpegMediaRecorder::FFMpegMediaRecorder() {
    memset(&video_st, 0, sizeof(video_st));
    memset(&audio_st, 0, sizeof(audio_st));

    fmt = NULL;
    oc = NULL;
    audio_codec = NULL;
    video_codec = NULL;
    have_video = 0;
    have_audio = 0;
    audioIdx_ = 0;
    fileReady_ = false;
    lastSrcW_ = lastSrcH_ = 0;
    av_register_all();
  }

  PixelFormat VideoFormatToPixelFormat(media::VideoFrame::Format video_format) {
    if (video_format == media::VideoFrame::I420)
      return PIX_FMT_YUV420P;
    else
      return media::VideoFormatToPixelFormat(video_format);
  }

  int FFMpegMediaRecorder::InitVideo(short width, short height, char framerate, int bitrate, media::VideoFrame::Format videoFormat) {
    /* Add the video streams using the default format codecs
    * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
      if (add_stream(&video_st, oc, &video_codec, 
        fmt->video_codec, bitrate, -1, -1, width, height, framerate, ::VideoFormatToPixelFormat(videoFormat)) == 0) {
        have_video = 1;
      }
    }

    /* Now that all the parameters are set, we can open the
    * video codecs and allocate the necessary encode buffers. */
    if (have_video)
      have_video = open_video(oc, video_codec, &video_st, NULL) == 0;
    
    if (have_video && have_audio)
      InitFile();

    return have_video;
  }

  int FFMpegMediaRecorder::InitAudio(int samplerate, int bitrate, int channels) {
    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
      if (add_stream(&audio_st, oc, &audio_codec, 
        fmt->audio_codec, bitrate, samplerate, channels, -1, -1, -1, -1) == 0) {
        have_audio = 1;
      }
    }

    if (have_audio)
      have_audio = open_audio(oc, audio_codec, &audio_st, NULL, samplerate, channels) == 0;

    if (have_audio && have_video)
      InitFile();

    return have_audio;
  }

  int FFMpegMediaRecorder::Init(const char* filename) {
    filename_ = filename;
    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, NULL, NULL, filename_.c_str());
    if (!oc) {
      return -1;
    }

    fmt = oc->oformat;

    return 0;
  }

  int FFMpegMediaRecorder::InitFile() {
    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
      if (avio_open(&oc->pb, filename_.c_str(), AVIO_FLAG_WRITE) < 0) {
        return -1;
      }
    }

    /* Write the stream header, if any. */
    AVDictionary* opt = NULL;
    if (avformat_write_header(oc, &opt) < 0) {
      return -1;
    }

    fileReady_ = true;
    return 0;
  }

  bool FFMpegMediaRecorder::UpdateVideo(const media::VideoFrame& frame,
    const media::VideoCaptureFormat& format,
    const base::TimeTicks& estimated_capture_time) {

    if (!have_video || !fileReady_) return false;

    base::AutoLock lock(lock_);
    const AVCodecContext* c = video_st.st->codec;
    const int srcW = frame.coded_size().width();
    const int srcH = frame.coded_size().height();
    const PixelFormat srcF = ::VideoFormatToPixelFormat(frame.format());

    if (c->width != srcW || c->height != srcH ||
      c->pix_fmt != srcF) {

      SwsContext* swsC = video_st.sws_ctx;

      // Source resolution changed ?
      if (swsC && (lastSrcW_ != srcW || lastSrcH_ != srcH)) {
        sws_freeContext(video_st.sws_ctx);
        video_st.sws_ctx = NULL;
      }

      if (video_st.sws_ctx == NULL) {
        video_st.sws_ctx = sws_getContext(
          srcW, srcH, srcF,
          c->width, c->height, c->pix_fmt,
          SWS_BICUBIC, NULL, NULL, NULL);

        if (!video_st.sws_ctx) 
          return false;

        lastSrcW_ = srcW;
        lastSrcH_ = srcH;
      }

      const uint8_t *const srcSlice[] = { frame.data(0), frame.data(1), frame.data(2) };
      const int srcStride[] = { frame.row_bytes(0), frame.row_bytes(1), frame.row_bytes(2) };

      sws_scale(video_st.sws_ctx,
        srcSlice, srcStride,
        0, srcH, video_st.frame->data, video_st.frame->linesize);

    }
    else {
      const uint8_t *src_data[4] = { frame.data(0), frame.data(1), frame.data(2), 0 };
      const int src_linesizes[4] = { frame.row_bytes(0), frame.row_bytes(1), frame.row_bytes(2), 0 };

      AVFrame* avFrame = video_st.frame;
      av_image_copy(avFrame->data, avFrame->linesize, src_data, src_linesizes, (AVPixelFormat)avFrame->format, avFrame->width, avFrame->height);
    }

    if (videoStart_.is_null()) {
      videoStart_ = estimated_capture_time;
      video_st.frame->pts = 0;
    }
    else {
      base::TimeDelta dt = estimated_capture_time - videoStart_;
      video_st.frame->pts = dt.InSecondsF() / c->time_base.num * c->time_base.den;
    }
    write_video_frame(oc, &video_st, video_st.frame);
    return true;

  }

  // copy audio data from audiobus to avframe
  static void PutData(AVFrame* frame, int frameIdx, const media::AudioBus& audio_bus, int audioBusIdx, int size){
    const int channels = std::min(AV_NUM_DATA_POINTERS, audio_bus.channels());
    for (int i = 0; i < channels; i++)
      memcpy(frame->data[i] + frameIdx, &audio_bus.channel(i)[audioBusIdx / sizeof(float)], size);
  }

  bool FFMpegMediaRecorder::UpdateAudio(const media::AudioBus& audio_bus,
    const base::TimeTicks& estimated_capture_time) {
 
    if (!have_audio || !fileReady_) return false;

    AVFrame* frame = audio_st.tmp_frame;

    if (audioStart_.is_null()) {
      audioStart_ = estimated_capture_time;
      frame->pts = 0;
    }

    base::TimeDelta dt = estimated_capture_time - audioStart_;

    const int audio_bus_data_size = audio_bus.frames() * sizeof(float);
    const int left_over_data = frame->linesize[0] - audioIdx_;
    if (audioIdx_ == 0) frame->pts = dt.InSecondsF() * audio_st.st->codec->sample_rate;

    if (audioIdx_ + audio_bus_data_size < frame->linesize[0]) {
      PutData(frame, audioIdx_, audio_bus, 0, audio_bus_data_size);
      audioIdx_ += audio_bus_data_size;
      return true;
    }
    else {
      PutData(frame, audioIdx_, audio_bus, 0, left_over_data);
    }

    base::AutoLock lock(lock_);

    write_audio_frame(oc, &audio_st, frame);
    audioIdx_ = audio_bus_data_size - left_over_data;

    frame->pts = dt.InSecondsF() * audio_st.st->codec->sample_rate;
    PutData(frame, 0, audio_bus, left_over_data, audioIdx_);
    return true;

  }
  FFMpegMediaRecorder::~FFMpegMediaRecorder() {
    Stop();
  }

  bool FFMpegMediaRecorder::Stop() {
    if (oc == NULL)
      return false;

    base::AutoLock lock(lock_);

    /* Write the trailer, if any. The trailer must be written before you
    * close the CodecContexts open when you wrote the header; otherwise
    * av_write_trailer() may try to use memory that was freed on
    * av_codec_close(). */
    if (fileReady_)
      av_write_trailer(oc);

    /* Close each codec. */
    if (have_video)
      close_stream(oc, &video_st);
    if (have_audio)
      close_stream(oc, &audio_st);

    if (fileReady_ && !(fmt->flags & AVFMT_NOFILE))
      /* Close the output file. */
      avio_close(oc->pb);

    /* free the stream */
    avformat_free_context(oc);

    oc = NULL;
    return true;
  }
};
