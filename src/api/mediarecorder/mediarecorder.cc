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

#include "content/nw/src/api/mediarecorder/mediarecorder.h"
#include "public/web/WebMediaStreamRegistry.h"
#include "third_party/WebKit/public/platform/WebURL.h"
#include "third_party/WebKit/public/platform/WebMediaStream.h"
#include "third_party/WebKit/public/platform/WebMediaStreamTrack.h"

#include "content/public/renderer/media_stream_video_sink.h"
#include "content/public/renderer/media_stream_audio_sink.h"
#include "media/audio/audio_parameters.h"
#include "media/base/video_frame.h"
#include "base/strings/utf_string_conversions.h"

#include "ffmpeg_mediarecorder_wrapper.h"

namespace nwapi{

  MediaRecorder::MediaRecorder(int id,
    const base::WeakPtr<DispatcherHost>& dispatcher_host,
    const base::DictionaryValue& option)
    : Base(id, dispatcher_host, option) {
  }

  MediaRecorder::~MediaRecorder() {

  }

  void MediaRecorder::CallSync(const std::string& method,
    const base::ListValue& arguments,
    base::ListValue* result) {
    result->Clear();
    if (!method.compare("SetVideoURL")) {
      result->AppendBoolean(arguments.GetString(0, &mVideoID));
    }
    else if (!method.compare("SetAudioURL")) {
      result->AppendBoolean(arguments.GetString(0, &mAudioID));
    }
    else if (!method.compare("Start") || !method.compare("Stop")) {
      // return the audio and video id to the process that hold the MediaStream
      result->AppendString(mVideoID);
      result->AppendString(mAudioID);
    }
  }

  class MediaRecorderSink : content::MediaStreamVideoSink, 
    content::MediaStreamAudioSink, public base::RefCountedThreadSafe<MediaRecorderSink> {
  public:
    MediaRecorderSink(const char* filename, blink::WebMediaStreamTrack& videoTrack, 
      blink::WebMediaStreamTrack& audioTrack, const base::ListValue& args)
      : videoTrack_(videoTrack), audioTrack_(audioTrack) {

      ffmpeg_.Init(filename);
      
      args.GetInteger(1, &audioBitRate_);
      args.GetInteger(2, &audioSampleRate_);
      args.GetInteger(3, &videoBitRate_);
      args.GetInteger(4, &frameRate_);
      args.GetInteger(5, &width_);
      args.GetInteger(6, &height_);
      args.GetInteger(7, &forceSync_);

      if (!videoTrack_.isNull())
        MediaStreamVideoSink::AddToVideoTrack(
          this,
          base::Bind(
            &MediaRecorderSink::OnVideoFrame,
            this),
          videoTrack_);

      if (!audioTrack_.isNull())
        MediaStreamAudioSink::AddToAudioTrack(this, audioTrack_);
    }

    // Ref Counted, Stop to Delete !!
    void Stop() {
      if (!audioTrack_.isNull())
        MediaStreamAudioSink::RemoveFromAudioTrack(this, audioTrack_);

      if (!videoTrack_.isNull())
        MediaStreamVideoSink::RemoveFromVideoTrack(this, videoTrack_);
    }

  private:
    void OnVideoFrame(
      const scoped_refptr<media::VideoFrame>& frame,
      const media::VideoCaptureFormat& format,
      const base::TimeTicks& estimated_capture_time) {

      if (!ffmpeg_.has_video()) {
        const int width  = width_  ? width_  : frame->coded_size().width();
        const int height = height_ ? height_ : frame->coded_size().height();
        const int frameRate = frameRate_ ? frameRate_ : 25;
        const int bitRate = videoBitRate_ ? videoBitRate_ : width * height;
        ffmpeg_.InitVideo(width, height, frameRate, bitRate, frame->format());
      }

      ffmpeg_.UpdateVideo(*frame, format, estimated_capture_time);
    }

    void OnData(const media::AudioBus& audio_bus,
      base::TimeTicks estimated_capture_time) override {
      ffmpeg_.UpdateAudio(audio_bus, estimated_capture_time);
    }

    void OnSetFormat(const media::AudioParameters& params) override {
      const int bitRate = audioBitRate_ ? audioBitRate_ : params.sample_rate() * 2;
      const int audioSampleRate = audioSampleRate_ ? audioSampleRate_ : params.sample_rate();
      ffmpeg_.InitAudio(params.sample_rate(), audioSampleRate, bitRate, params.channels(), params.frames_per_buffer(), forceSync_);
    }

    friend class base::RefCountedThreadSafe<MediaRecorderSink>;
    ~MediaRecorderSink() override {
      DVLOG(3) << "MediaRecorderSink dtor().";
    }

    int audioBitRate_, audioSampleRate_, videoBitRate_, frameRate_, width_, height_;
    int forceSync_;
    blink::WebMediaStreamTrack videoTrack_, audioTrack_;
    FFMpegMediaRecorder ffmpeg_;
    DISALLOW_COPY_AND_ASSIGN(MediaRecorderSink);
  };


  bool MediaRecorder::Process(const int object_id, const std::string method, const base::ListValue& args, const base::ListValue& streams) {
    typedef std::map < int, MediaRecorderSink* > MRSMap;
    static MRSMap mrsMap;

    std::string videoID, audioID;
    streams.GetString(0, &videoID);
    streams.GetString(1, &audioID);
    
    const blink::WebMediaStream& video = blink::WebMediaStreamRegistry::lookupMediaStreamDescriptor(GURL(videoID));
    const blink::WebMediaStream& audio = blink::WebMediaStreamRegistry::lookupMediaStreamDescriptor(GURL(audioID));

    if (!method.compare("Start")) {
      MRSMap::iterator i = mrsMap.find(object_id);

      if (i != mrsMap.end()) return false; // already started before
      if (audio.isNull() && video.isNull()) return false; // both are null ??

      blink::WebVector<blink::WebMediaStreamTrack> videoTracks;
      blink::WebVector<blink::WebMediaStreamTrack> audioTracks;

      if(!video.isNull()) video.videoTracks(videoTracks);
      if(!audio.isNull()) audio.audioTracks(audioTracks);

      blink::WebMediaStreamTrack video;
      if(videoTracks.size() > 0) video = videoTracks[0];

      blink::WebMediaStreamTrack audio;
      if(audioTracks.size() > 0) audio = audioTracks[0];

      base::string16 filename;
      if (!args.GetString(0, &filename))
        return false; // no filename

      MediaRecorderSink* mrs = new MediaRecorderSink(base::UTF16ToUTF8(filename).c_str(), video, audio, args);
      mrsMap.insert(std::pair<MRSMap::key_type, MRSMap::mapped_type>(object_id, mrs));
      mrs->AddRef();

      return true;
    }
    else if (!method.compare("Stop")) {
      MRSMap::iterator i = mrsMap.find(object_id);
      if (i != mrsMap.end()) {
        i->second->Stop();
        i->second->Release();
        mrsMap.erase(i);
        return true;
      }
    }
    return false;
  }
}
