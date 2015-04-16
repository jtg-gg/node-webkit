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
#include "base/bind.h"
#include "base/bind_helpers.h"
#include "base/task_runner_util.h"
#include "base/threading/thread.h"

#include "media/ffmpeg/ffmpeg_common.h"
#include "media/base/audio_bus.h"

extern "C" {
#include <libswscale/swscale.h>
#include <libswresample/swresample.h>
#include <libavutil/parseutils.h>
#include "ffmpeg_mediarecorder_wrapper.h"
  
//#define MEMORY_PROFILING
//#define DO_CPU_PROFILING

  // forward declaration
  void ff_color_frame(AVFrame *frame, const int *c);
#ifdef OS_WIN
  void av_log_nwjs_callback(void* ptr, int level, const char* fmt, va_list vl) {
    if (level > AV_LOG_INFO) return;
    char dst[1024];
    int idx = vsnprintf(dst, 1024, fmt, vl);
    if (idx >= 0 && idx < 1024) {
      OutputDebugStringA(dst);
    }
  }
#endif

  class FFMpegAVPacket : public base::RefCountedThreadSafe<FFMpegAVPacket> {
  private:
    AVPacket packet_;
    friend class base::RefCountedThreadSafe<FFMpegAVPacket>;
    ~FFMpegAVPacket(){
#ifdef MEMORY_PROFILING
      object_counter_--;
#endif
    }
  public:
#ifdef MEMORY_PROFILING
    static int object_counter_;
    static int max_object_;
#endif
    AVPacket* get() {
      return &packet_;
    }
    
    FFMpegAVPacket(const AVPacket* pkt) {
      memcpy(&packet_, pkt, sizeof(AVPacket));
#ifdef MEMORY_PROFILING
      object_counter_++;
      if (object_counter_ > max_object_)
        max_object_ = object_counter_;
#endif
    }
    
  };

#ifdef MEMORY_PROFILING
  int FFMpegAVPacket::object_counter_ = 0;
  int FFMpegAVPacket::max_object_ = 0;
#endif
  
  FFMpegMediaRecorder::FFMpegMediaRecorder() {
    memset(&video_st, 0, sizeof(video_st));
    memset(&audio_st, 0, sizeof(audio_st));

    fmt = NULL;
    oc = NULL;
    have_video = 0;
    have_audio = 0;
    audio_only = 0;
    video_only = 0;
    fileReady_ = false;
    lastSrcW_ = lastSrcH_ = 0;
    
    blackPixel_  = NULL;
    blackScaler_ = NULL;

    forceAudioSync = 1;
    outTolerance = -1;
    audioOpt_ = NULL;
    videoOpt_ = NULL;
    muxerOpt_ = NULL;
    
    //av_log_set_level(AV_LOG_INFO);
#ifdef OS_WIN
    //don't forget to add the function def in ffmpegsumo.sigs
    //av_log_set_callback(av_log_nwjs_callback);
#endif
    av_register_all();
  }

  AVPixelFormat VideoPixelFormatToAVPixelFormat(media::VideoPixelFormat video_format) {
    if (video_format == media::PIXEL_FORMAT_I420)
      return AV_PIX_FMT_YUV420P;
    else
      return media::VideoPixelFormatToAVPixelFormat(video_format);
  }

  int FFMpegMediaRecorder::InitVideo(int width, int height, media::VideoPixelFormat videoFormat) {
    base::AutoLock lock(lock_);
    if (audio_only) {have_video = 0; return have_video;}
    AVCodec *video_codec = NULL;

    AVDictionaryEntry* dict_entry = av_dict_get(videoOpt_, "b", NULL, 0);
    int bitrate = 0;
    if (dict_entry) {
      bitrate = strtol(dict_entry->value, NULL, 10);
      av_dict_set(&videoOpt_, "b", NULL, 0);
    }
    
    dict_entry = av_dict_get(videoOpt_, "video_size", NULL, 0);
    if (dict_entry) {
      av_parse_video_size(&width, &height, dict_entry->value);
      av_dict_set(&videoOpt_, "video_size", NULL, 0);
    }
    
    /* Add the video streams using the default format codecs
    * and initialize the codecs. */
    if (fmt->video_codec != AV_CODEC_ID_NONE) {
      if (add_stream(&video_st, oc, &video_codec, 
        fmt->video_codec, bitrate, -1, -1, width, height, ::VideoPixelFormatToAVPixelFormat(videoFormat)) == 0) {
        have_video = 1;
      } else {
        LOG(ERROR) << "add_stream (video) fails";
      }
    }

    /* Now that all the parameters are set, we can open the
    * video codecs and allocate the necessary encode buffers. */
    if (have_video)
      have_video = open_video(oc, video_codec, &video_st, &videoOpt_) == 0;
    
    LOG_IF(INFO, have_video) << "open_video success";
    LOG_IF(ERROR, !have_video) << "open_video fails";

    if (have_video && (have_audio || video_only))
      InitFile();

    return have_video;
  }

  int FFMpegMediaRecorder::InitAudio(int samplerate, int channels, int frame_size, int forceSync) {
    base::AutoLock lock(lock_);
    if (video_only) {have_audio = 0; return have_audio;}
    AVCodec *audio_codec = NULL;

    int target_rate = 0;
    AVDictionaryEntry* dict_entry = av_dict_get(audioOpt_, "ar", NULL, 0);
    if (dict_entry) {
      target_rate = strtol(dict_entry->value, NULL, 10);
      av_dict_set(&audioOpt_, "ar", NULL, 0);
    }
    target_rate = target_rate ? target_rate : samplerate;

    if (fmt->audio_codec != AV_CODEC_ID_NONE) {
      if (add_stream(&audio_st, oc, &audio_codec, 
        fmt->audio_codec, -1, target_rate, channels, -1, -1, -1) == 0) {
        have_audio = 1;
      } else {
        LOG(ERROR) << "add_stream (audio) fails";
      }
    }

    if (have_audio)
      have_audio = open_audio(oc, audio_codec, &audio_st, samplerate, channels, frame_size, &audioOpt_) == 0;

    LOG_IF(INFO, have_audio) << "open_audio success, sample rate " << audio_st.st->codec->sample_rate;
    LOG_IF(ERROR, !have_audio) << "open_audio fails";

    if (have_audio && (have_video || audio_only))
      InitFile();
    
    forceAudioSync = forceSync;

    return have_audio;
  }

  int FFMpegMediaRecorder::Init(const char* mime, const EventCB& event_cb, const char* audioOpt, const char* videoOpt, const char* muxerOpt) {
    event_cb_ = event_cb;
    
    AVDictionary** opts[3] = {&audioOpt_, &videoOpt_, &muxerOpt_};
    const char* charOpts[3] = {audioOpt, videoOpt, muxerOpt};
    
    for(int i=0; i<3; i++) {
      assert(*opts[i] == NULL);
      if (charOpts[i]) av_dict_parse_string(opts[i], charOpts[i], "=", ";", 0);
      
      AVDictionaryEntry *t = av_dict_get(*opts[i], "", NULL, AV_DICT_IGNORE_SUFFIX);
      while (t) {
        LOG(INFO) << t->key << " --> " << t->value;
        t = av_dict_get(*opts[i], "", t, AV_DICT_IGNORE_SUFFIX);
      }
      
    }

    /* allocate the output media context */
    avformat_alloc_output_context2(&oc, av_guess_format(NULL, NULL, mime), NULL, NULL);
    if (!oc) {
      LOG(ERROR) << "avformat_alloc_output_context2 fails";
      return -1;
    }

    fmt = oc->oformat;
    
    audio_only = audioOpt && !videoOpt;
    video_only = videoOpt && !audioOpt;

    return 0;
  }
  
  int writeCB(void* h, uint8_t* data, int size) {
    FFMpegMediaRecorder::EventCB* event_cb = reinterpret_cast<FFMpegMediaRecorder::EventCB*>(h);
    //LOG(INFO) << "data size " << size;
    event_cb->Run("NWObjectMediaRecorderData",
                  base::BinaryValue::CreateWithCopiedBuffer((char*)data, size));
    return size;
  }

  int FFMpegMediaRecorder::InitFile() {
    /* open the output file, if needed */
    if (!(fmt->flags & AVFMT_NOFILE)) {
      int buffer_size = 32 * 1024;
      uint8_t* buffer = (uint8_t*)av_malloc(buffer_size);
      oc->pb = avio_alloc_context(buffer, buffer_size, AVIO_FLAG_WRITE, &event_cb_,
                              NULL,
                              writeCB,
                              NULL);
      
      if (oc->pb == 0) {
        av_free(buffer);
        LOG(ERROR) << "avio_alloc_context fails";
        return -1;
      }
    }

    /* Write the stream header, if any. */
    if (avformat_write_header(oc, &muxerOpt_) < 0) {
      LOG(ERROR) << "avformat_write_header fails";
      return -1;
    }

    fileReady_ = true;
    worker_thread_.reset(new base::Thread("FFMpegMediaRecorder_Worker"));
    worker_thread_->Start();
    LOG(INFO) << "FFMpegMediaRecorder can start recording now";
    event_cb_.Run("NWObjectMediaRecorderStart", NULL);
    return 0;
  }

#ifdef DO_CPU_PROFILING
  struct Profiler {
    const char* name_;
    base::TimeTicks start_;
    Profiler* parent_;
    std::ostringstream  ostream_;
    const int elapsed_;

    Profiler(const char* name, Profiler* parent = NULL, int elapsed = 0) : 
      parent_(parent), elapsed_(elapsed) {
      start_ = base::TimeTicks::HighResNow();
      name_ = name;
    }
    ~Profiler() {
      double elapsed = (base::TimeTicks::HighResNow() - start_).InMillisecondsF();
      if (parent_ == NULL) {
        if (elapsed > elapsed_) {
          //DVLOG(1) << "\n" << ostream_.str() << "PROFILER " << name_ << ": " << elapsed;
          ostream_ << "PROFILER " << name_ << ": " << elapsed << "\n";
#ifdef OS_WIN
          OutputDebugStringA(ostream_.str().c_str());
#else
          printf("%s", ostream_.str().c_str());
#endif
        }
      }
      else
        parent_->ostream_ << "  PROFILER " << name_ << ": " << elapsed << "\n";
    }
  };

#define PROFILE(name, parent, time) Profiler name (#name, parent, time);
#define PROFILE_START(name, parent) { PROFILE(name, parent, 0)
#define PROFILE_END }
#else
#define PROFILE(name, parent, time)
#define PROFILE_START(name, parent)
#define PROFILE_END
#endif

  bool FFMpegMediaRecorder::UpdateVideo(const media::VideoFrame& frame,
    const base::TimeTicks& estimated_capture_time) {

    if (!have_video || !fileReady_) return false;
    PROFILE(update_video, NULL, 20);
    const AVCodecContext* c = video_st.st->codec;
    AVFrame* dstFrame = video_st.frame;

    if (videoStart_.is_null()) {
      videoStart_ = estimated_capture_time;
      dstFrame->pts = 0;
    }
    else {
      base::TimeDelta dt = estimated_capture_time - videoStart_;
      double dExpectedPts = dt.InSecondsF() / c->time_base.num * c->time_base.den;
      int expectedPts = dExpectedPts + 0.5;
      dstFrame->pts++;
      //DVLOG(1) << "VIDEO dt: " << dt.InSecondsF() << ", dExpectedPts: " << dExpectedPts << ", pts: " << expectedPts << ", framePts: " << dstFrame->pts;

      if (dstFrame->pts < expectedPts) {
        //DVLOG(1) << "adjust the VIDEO frame " << dstFrame->pts << ",expected frame is " << dExpectedPts << "\n";
        dstFrame->pts = expectedPts;
      } else if ( dstFrame->pts -  dExpectedPts > 1.0) {
        //DVLOG(1) << "drop   the VIDEO frame " << dstFrame->pts << ",expected frame is " << dExpectedPts << "\n";
        dstFrame->pts--;
        return false;
      }
    }

    const int srcW = frame.coded_size().width();
    const int srcH = frame.coded_size().height();
    const AVPixelFormat srcF = ::VideoPixelFormatToAVPixelFormat(frame.format());
    
    const size_t srcNumPlanes = frame.NumPlanes(frame.format());
    const uint8_t* srcSlice[media::VideoFrame::kMaxPlanes];
    int srcStride[media::VideoFrame::kMaxPlanes];
    
    for(size_t i=0; i<srcNumPlanes; i++) {
      srcSlice[i]=frame.data(i);
      srcStride[i]=frame.stride(i);
    }
    for(size_t i=srcNumPlanes; i<media::VideoFrame::kMaxPlanes; i++) {
      srcSlice[i] = NULL;
      srcStride[i] = 0;
    }

    PROFILE_START(resize_copy, &update_video);
    if (c->width != srcW || c->height != srcH ||
      c->pix_fmt != srcF) {

      SwsContext* swsC = video_st.sws_ctx;

      // Source resolution changed ?
      if (swsC && (lastSrcW_ != srcW || lastSrcH_ != srcH)) {
        sws_freeContext(video_st.sws_ctx);
        video_st.sws_ctx = NULL;
      }
      
      float scale = (float)c->height/srcH;
      int dstW = srcW * scale;
      int dstH;
      
      if (dstW <= c->width)
        dstH = scale * srcH;
      else {
        scale = (float) c->width/srcW;
        dstW = scale * srcW;
        dstH = scale * srcH;
      }
      
      assert(dstW <= c->width && dstH <= c->height);

      if (video_st.sws_ctx == NULL) {
        video_st.sws_ctx = sws_getContext(
          srcW, srcH, srcF,
          dstW, dstH, c->pix_fmt,
          SWS_BICUBIC, NULL, NULL, NULL);

        if (!video_st.sws_ctx) 
          return false;

        lastSrcW_ = srcW;
        lastSrcH_ = srcH;
        
        const AVPixFmtDescriptor *desc = av_pix_fmt_desc_get((AVPixelFormat)dstFrame->format);
        if (desc->flags & AV_PIX_FMT_FLAG_PLANAR) {
          const int isRGB = desc->flags & AV_PIX_FMT_FLAG_RGB;
          // YUV black value is 0, 128, 128
          const int color[] = {0, isRGB ? 0 : 128, isRGB ? 0 : 128, 0};
          ff_color_frame(dstFrame, color);
        } else {
          // SLOW path, non planar, make black picture, scale it to the target !!
          if (blackPixel_ == NULL) {
            blackPixel_ = alloc_picture(AV_PIX_FMT_GRAY8, 1, 1);
            for (int i=0; i<AV_NUM_DATA_POINTERS; i++)
              memset(blackPixel_->data[i], 0, blackPixel_->linesize[i] * blackPixel_->height);
          }
          
          if (blackScaler_ == NULL) {
            blackScaler_ = sws_getContext(
              blackPixel_->width, blackPixel_->height, (AVPixelFormat)blackPixel_->format,
              c->width, c->height, c->pix_fmt,
              SWS_POINT, NULL, NULL, NULL);
          }

          sws_scale(blackScaler_,
                    blackPixel_->data, blackPixel_->linesize,
                    0, blackPixel_->height, dstFrame->data, dstFrame->linesize);
        }

        // Center the destination image
        const int shiftX = FFMAX(0,(c->width - dstW)/2);
        const int shiftY = FFMAX(0,(c->height - dstH)/2);
        const int* dstStride = dstFrame->linesize;
        int p;
        for (p=0; p<desc->nb_components; p++) {
          bool is_chroma = p == 1 || p == 2;
          int shiftx = is_chroma ? FF_CEIL_RSHIFT(shiftX, desc->log2_chroma_w) : shiftX;
          int shifty = is_chroma ? FF_CEIL_RSHIFT(shiftY, desc->log2_chroma_h) : shiftY;
          dstSlice_[p] = dstFrame->data[p] + shiftx + shifty * dstStride[p];
        }
        for(; p<media::VideoFrame::kMaxPlanes; p++)
          dstSlice_[p] = 0;
      }
      
      sws_scale(video_st.sws_ctx,
        srcSlice, srcStride,
        0, srcH, dstSlice_, dstFrame->linesize);

    }
    else {
      av_image_copy(dstFrame->data, dstFrame->linesize, srcSlice, srcStride,
                    (AVPixelFormat)dstFrame->format, dstFrame->width, dstFrame->height);
    }
    PROFILE_END

    AVPacket pkt = { 0 };
    int res;
    PROFILE_START(_write_video_frame, &update_video);
    res = write_video_frame(oc, &video_st, dstFrame, &pkt);
    assert(res == 0 || (pkt.buf == NULL && pkt.data == NULL && pkt.size == 0));
    PROFILE_END
    if (res == 0) {
      PROFILE_START(_write_frame, &update_video);
      base::SingleThreadTaskRunner* tr = worker_thread_ ? worker_thread_->task_runner().get() : NULL;
      scoped_refptr<FFMpegAVPacket> avpacket = new FFMpegAVPacket(&pkt);
      if (tr) {
        tr->PostTask(FROM_HERE, base::Bind(&FFMpegMediaRecorder::WriteFrame,
                                            base::Unretained(this),
                                            avpacket,
                                            base::Unretained(video_st.st)));
      } else {
        WriteFrame(avpacket.get(), video_st.st);
      }
      PROFILE_END
    }
    return true;
  }

  // copy audio data from audiobus to avframe
  static void PutData(AVFrame* frame, int frameIdx, const media::AudioBus& audio_bus, int audioBusIdx, int size){
    const int channels = std::min(AV_NUM_DATA_POINTERS, audio_bus.channels());
    for (int i = 0; i < channels; i++) {
      assert(frameIdx + size <= frame->linesize[0]);
      memcpy(frame->data[i] + frameIdx, &audio_bus.channel(i)[audioBusIdx / sizeof(float)], size);
    }
  }

  void FFMpegMediaRecorder::WriteFrame(FFMpegAVPacket *pkt, AVStream* st) {
    PROFILE(Lock_WriteFrame, NULL, 10);
    base::AutoLock lock(lock_);
    PROFILE(_write_frame, &Lock_WriteFrame, 0);
    if (write_frame(oc, &st->codec->time_base, st, pkt->get()) != 0) {
      DLOG(ERROR) << " Error while writing AUDIO frame";
    }
    assert(pkt->get()->buf == NULL && pkt->get()->data == NULL && pkt->get()->size == 0);
  }
  
  void FFMpegMediaRecorder::RequestData(bool sendToThread) {
    if(!sendToThread) {
      base::AutoLock lock(lock_);
      av_write_frame(oc, NULL);
    }
    else {
      base::SingleThreadTaskRunner* tr = worker_thread_ ? worker_thread_->task_runner().get() : NULL;
      if (tr) {
        tr->PostTask(FROM_HERE, base::Bind(&FFMpegMediaRecorder::RequestData,
                                           base::Unretained(this),
                                           false));
      } else {
        RequestData(false);
      }
    }
  }
  
  bool FFMpegMediaRecorder::UpdateAudio(const media::AudioBus& audio_bus,
    const base::TimeTicks& estimated_capture_time) {
 
    if (!have_audio || !fileReady_) return false;

    // let the video start first
    if (!audio_only && videoStart_.is_null()) return false;

    PROFILE(update_audio, NULL, 10);
    const bool isAudioStart = audioStart_.is_null();
    if (isAudioStart) {
      // audio delay compensation issue has been fixed, now we can use the timer =)
      audioStart_ = audio_only ? estimated_capture_time : videoStart_;
    } 
    
    const AVCodecContext* c = audio_st.st->codec;
    AVFrame* dstFrame = audio_st.frame;

    if (audio_st.frame_sample == 0) {
      PROFILE(if_audio_st_frame_sample, &update_audio, 0);
      base::TimeDelta dt = estimated_capture_time - audioStart_;
      double dExpectedPts = dt.InSecondsF() / c->time_base.num * c->time_base.den;
      int expectedPts = dExpectedPts + 0.5;
      int delay = swr_get_delay(audio_st.swr_ctx, dstFrame->sample_rate);
      //DVLOG(1) << "AUDIO dt: " << dt.InSecondsF() << ", expectedPts: " << expectedPts << ", framePts: " << dstFrame->pts << " + " << delay;
      int ptsDiff = expectedPts - dstFrame->pts - delay;
      //DVLOG(1) << "AUDIO ptsDiff: " << ptsDiff;

      const int tolerance = (1 + delay / dstFrame->nb_samples) * dstFrame->nb_samples;

      if (isAudioStart)
        dstFrame->pts = expectedPts;
      else if (abs(ptsDiff) >= tolerance) {
        if (forceAudioSync) {
          if (outTolerance < 0) {
            outTolerance = abs(ptsDiff) / dstFrame->nb_samples + 1;
            //DLOG(WARNING) << "AUDIO calculating outTolerance: " << ptsDiff << "/" << dstFrame->nb_samples << " = " << outTolerance << "\n"
            //  << "AUDIO dt: " << dt.InSecondsF() << ", expectedPts: " << expectedPts << ", framePts: " << dstFrame->pts << " + " << delay;
          }
          else {
            //DVLOG(1) << "AUDIO soon running --outTolerance: " << outTolerance << "\n";
            if (--outTolerance == 0) {
              const bool adjustAllowed = !(forceAudioSync == 1 && ptsDiff < 0);
              if (adjustAllowed)
                dstFrame->pts = expectedPts;
              DLOG(WARNING) << "AUDIO" << (adjustAllowed ? " " : " NOT ") << "adjusting frame pts: " << dstFrame->pts << ", ptsDiff: " << ptsDiff << "\n"
                << "AUDIO dt: " << dt.InSecondsF() << ", expectedPts: " << expectedPts << ", framePts: " << dstFrame->pts << " + " << delay;
            }
          }
        } else {
          DLOG(WARNING) << "AUDIO frame pts: " << dstFrame->pts << ", is out of tolerance: " << tolerance << "\n"
            << "AUDIO dt: " << dt.InSecondsF() << ", expectedPts: " << expectedPts << ", framePts: " << dstFrame->pts << " + " << delay;
        }
      } else {
        outTolerance = -1;
      }

    }

    int left_over_data = audio_st.tmp_frame->linesize[0] - 0;
    PutData(audio_st.tmp_frame, 0, audio_bus, 0, left_over_data);

    int res = -1;
    do {
      AVPacket pkt = { 0 }; // data and size must be 0;
      PROFILE_START(_write_audio_frame, &update_audio);
      res = write_audio_frame(&audio_st, &pkt, res > 0 ? 0 : audio_st.tmp_frame->nb_samples);
      assert(res >= 0 || (pkt.buf == NULL && pkt.data == NULL && pkt.size == 0));
      PROFILE_END
      if (res >= 0) {
        //int64_t scaledPktDts = av_rescale_q(pkt.dts, audio_st.st->codec->time_base, audio_st.st->time_base);
        // write_frame is most likely the encoding bottlenect, I/O bound
        PROFILE_START(_write_frame, &update_audio);
        base::SingleThreadTaskRunner* tr = worker_thread_ ? worker_thread_->task_runner().get() : NULL;
        scoped_refptr<FFMpegAVPacket> avpacket = new FFMpegAVPacket(&pkt);
        if (tr) {
          tr->PostTask(FROM_HERE, base::Bind(&FFMpegMediaRecorder::WriteFrame,
                                             base::Unretained(this),
                                             avpacket,
                                             base::Unretained(audio_st.st)));
        } else {
          WriteFrame(avpacket.get(), audio_st.st);
        }
        PROFILE_END
      }
    } while (res > 0);

    return true;

  }

  FFMpegMediaRecorder::~FFMpegMediaRecorder() {
    Stop();
  }

  bool FFMpegMediaRecorder::Stop() {
    if (audioOpt_) av_dict_free(&audioOpt_);
    if (videoOpt_) av_dict_free(&videoOpt_);
    if (muxerOpt_) av_dict_free(&muxerOpt_);
    
    if (oc == NULL)
      return false;

    base::AutoLock lock(lock_);
#ifdef MEMORY_PROFILING
    LOG(INFO) << "FFMpegAVPacket before stop: " << FFMpegAVPacket::object_counter_;
#endif
    if (worker_thread_) worker_thread_->Stop();
#ifdef MEMORY_PROFILING
    LOG(INFO) << "FFMpegAVPacket after  stop: " << FFMpegAVPacket::object_counter_ << ", Max object: " << FFMpegAVPacket::max_object_;
#endif

    AVCodecContext *c;
    int res;
    
    if (have_video) {
      c = video_st.st->codec;
      res = c->codec->capabilities & CODEC_CAP_DELAY ? 0 : -1;
      // flush the video encoder
      while (res == 0) {
        AVPacket pkt = { 0 };
        res = write_video_frame(oc, &video_st, NULL, &pkt);
        if (res == 0) {
          if (write_frame(oc, &c->time_base, video_st.st, &pkt) != 0) {
            DLOG(ERROR) << " Error while writing VIDEO frame";
          }
        }
      }
    }

    if (have_audio) {
    // flush the audio buffer
      c = audio_st.st->codec;
      do {
        AVPacket pkt = { 0 }; // data and size must be 0;
        res = write_audio_frame(&audio_st, &pkt, 0);
        if (res >= 0) {
          if (write_frame(oc, &c->time_base, audio_st.st, &pkt) != 0) {
            DLOG(ERROR) << " Error while writing AUDIO frame";
          }
        }
      } while (res > -2);
      
      // flush the audio encoder
      res = c->codec->capabilities & CODEC_CAP_DELAY ? 1 : -1;
      while (res > 0) {
        AVPacket pkt = { 0 }; // data and size must be 0;
        avcodec_encode_audio2(c, &pkt, NULL, &res);
        if (res) {
          if (write_frame(oc, &c->time_base, audio_st.st, &pkt) != 0) {
            DLOG(ERROR) << " Error while writing AUDIO frame";
          }
        }
      }
    }
    
    if (blackPixel_)  av_frame_free(&blackPixel_);
    if (blackScaler_) sws_freeContext(blackScaler_);

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

    if (fileReady_ && !(fmt->flags & AVFMT_NOFILE)) {
      oc->pb->opaque = NULL;
      /* Close the output file. */
      avio_close(oc->pb);
    }

    /* free the stream */
    avformat_free_context(oc);

    oc = NULL;
    
    event_cb_.Run("NWObjectMediaRecorderStop",
                  NULL);
    return true;
  }
};
