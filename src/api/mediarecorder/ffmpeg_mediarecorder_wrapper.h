// Copyright (c) 2018 Jefry Tedjokusumo
// Copyright (c) 2018 The Chromium Authors
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

#include "ffmpeg_mediarecorder.h"
#ifdef __cplusplus

namespace base {
  class TimeTicks;
}

namespace media {
  class VideoFrame;
  struct VideoCaptureFormat;
  class AudioBus;
}

extern "C" {
#endif

struct AVOutputFormat;
struct AVFormatContext;
struct AVCodec;
class FFMpegAVPacket;
  
class FFMpegMediaRecorder {
public:
  typedef base::Callback<void(const std::string event, base::Value* arg)> EventCB;

private:
  struct OutputContext {
    AVFormatContext* fc;
    bool active;
    bool saveOffset[2];
    int64_t pts_offset[2];
    int64_t dts_offset[2];
    OutputContext(AVFormatContext* fc) {
      this->fc = fc;
      active = true;
      for (int i=0; i<2; i++) {
        saveOffset[i] = false;
        pts_offset[i] = 0;
        dts_offset[i] = 0;
      }
    }
  };
  OutputStream video_st, audio_st;
  std::vector<OutputContext> ocs;
  bool have_video, have_audio, audio_only, video_only;
  int forceAudioSync, outTolerance;
  AVDictionary *audioOpt_, *videoOpt_, *muxerOpt_;
  std::string output_;

  AVFrame* blackPixel_;
  SwsContext* blackScaler_;
  uint8_t *dstSlice_[media::VideoFrame::kMaxPlanes];
  
  base::Lock lock_;
  base::TimeTicks videoStart_;
  base::TimeTicks audioStart_;
  bool fileReady_;
  short lastSrcW_, lastSrcH_;
  
  std::unique_ptr<base::Thread> worker_thread_;
  EventCB event_cb_;

  int InitFile();
  void write_frames(const AVRational *time_base, int st_index, AVPacket *pkt);

public:
  FFMpegMediaRecorder();
  ~FFMpegMediaRecorder();
  
  int Init(const char* mime, const EventCB& dipatcher_cb, const char* audioOpt, const char* videoOpt, const char* muxerOpt, const std::string& output, const int logLevel);
  int InitVideo(int width, int height, media::VideoPixelFormat pixelFormat);
  int InitAudio(int samplerate, int channels, int frame_size, int forceSync);
  
  bool UpdateVideo(const media::VideoFrame& frame,
    const base::TimeTicks& estimated_capture_time);
  
  bool UpdateAudio(const media::AudioBus& audio_bus,
    const base::TimeTicks& estimated_capture_time);

  int ReOpen(const char* old_output, const char* new_output);

  int has_video() const { return have_video; }
  
  void WriteFrame(const scoped_refptr<FFMpegAVPacket>& pkt, OutputStream* ost);
  
  void RequestData(bool sendToThread = true);

  void Stop();
};

#ifdef __cplusplus
};
#endif
