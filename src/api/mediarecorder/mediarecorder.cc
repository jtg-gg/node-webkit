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
#include "content/public/renderer/render_view.h"
#include "extensions/renderer/dispatcher.h"
#include "extensions/renderer/script_context.h"
#include "media/base/audio_parameters.h"
#include "media/base/bind_to_current_loop.h"
#include "media/base/video_frame.h"
#include "base/strings/utf_string_conversions.h"

#include "ffmpeg_mediarecorder_wrapper.h"

namespace nw {
  MediaRecorder::MediaRecorder(int id,
    const base::WeakPtr<ObjectManager>& object_manager,
    const base::DictionaryValue& option,
    const std::string& extension_id)
    : Base(id, object_manager, option, extension_id) {
  }

  MediaRecorder::~MediaRecorder() {

  }

  class MediaPause {
    base::TimeDelta deltaTime_;
    base::TimeTicks pauseTime_;
    enum PauseState {
      StateActive, StatePause, StatePaused, StateResume
    };
    PauseState pause_;
  public:
    MediaPause(): pause_(StateActive) {}
    ~ MediaPause() {}

    bool isPaused(const base::TimeTicks& time) {
      if (pause_ == StatePause) {
        pauseTime_ = time;
        pause_ = StatePaused;
      } else if (pause_ == StateResume) {
        deltaTime_ += time - pauseTime_;
        pause_ = StateActive;
      }
      
      return pause_ != StateActive;
    }
    
    void Pause() {
      assert(pause_ == StateActive);
      pause_ = StatePause;
    }
    
    void Resume() {
      assert(pause_ == StatePaused);
      pause_ = StateResume;
    }
    
    base::TimeDelta getDeltaTime() {
      return deltaTime_;
    }
    
  };
  
  class MediaRecorderSink : content::MediaStreamVideoSink, 
    content::MediaStreamAudioSink, public base::RefCountedThreadSafe<MediaRecorderSink> {
  public:
    MediaRecorderSink(const char* mime, blink::WebMediaStreamTrack& videoTrack,
      blink::WebMediaStreamTrack& audioTrack, const v8::FunctionCallbackInfo<v8::Value>& args,
      const FFMpegMediaRecorder::EventCB& event_cb)
      : videoTrack_(videoTrack), audioTrack_(audioTrack), event_cb_(event_cb) {

      v8::Isolate* isolate = args.GetIsolate();
      const std::string audioParams = *v8::String::Utf8Value(args[5]);
      const std::string videoParams = *v8::String::Utf8Value(args[6]);
      const std::string muxerParams = *v8::String::Utf8Value(args[7]);

      ffmpeg_.Init(mime, event_cb, audioTrack_.isNull() ? NULL : audioParams.c_str(), videoTrack_.isNull() ? NULL: videoParams.c_str(), muxerParams.c_str());
        
      forceSync_ = args[4]->ToInt32(isolate)->Value();

      if (!videoTrack_.isNull())
        MediaStreamVideoSink::ConnectToTrack(
          videoTrack_,
          base::Bind(
            &MediaRecorderSink::OnVideoFrame,
            this), false);

      if (!audioTrack_.isNull())
        MediaStreamAudioSink::AddToAudioTrack(this, audioTrack_);
    }

    // Ref Counted, Stop to Delete !!
    void Stop() {
      if (!audioTrack_.isNull())
        MediaStreamAudioSink::RemoveFromAudioTrack(this, audioTrack_);

      if (!videoTrack_.isNull())
        MediaStreamVideoSink::DisconnectFromTrack();
    }

    void Pause() {
      videoPause_.Pause();
      audioPause_.Pause();
      event_cb_.Run("NWObjectMediaRecorderPause", NULL);
    }

    void Resume() {
      videoPause_.Resume();
      audioPause_.Resume();
      event_cb_.Run("NWObjectMediaRecorderResume", NULL);
    }

    void RequestData() {
      ffmpeg_.RequestData();
    }

  private:
    void OnVideoFrame(
      const scoped_refptr<media::VideoFrame>& frame,
      base::TimeTicks estimated_capture_time) {

      if (!ffmpeg_.has_video()) {
        ffmpeg_.InitVideo(frame->coded_size().width(), frame->coded_size().height(), frame->format());
      }
      if(videoPause_.isPaused(estimated_capture_time)) return;
      ffmpeg_.UpdateVideo(*frame, estimated_capture_time - videoPause_.getDeltaTime());
    }

    void OnData(const media::AudioBus& audio_bus,
      base::TimeTicks estimated_capture_time) override {
      if(audioPause_.isPaused(estimated_capture_time)) return;
      ffmpeg_.UpdateAudio(audio_bus, estimated_capture_time - audioPause_.getDeltaTime());
    }

    void OnSetFormat(const media::AudioParameters& params) override {
      ffmpeg_.InitAudio(params.sample_rate(), params.channels(), params.frames_per_buffer(), forceSync_);
    }

    friend class base::RefCountedThreadSafe<MediaRecorderSink>;
    ~MediaRecorderSink() override {
      DVLOG(3) << "MediaRecorderSink dtor().";
    }

    int forceSync_;
    blink::WebMediaStreamTrack videoTrack_, audioTrack_;
    MediaPause videoPause_, audioPause_;
    FFMpegMediaRecorder::EventCB event_cb_;
    FFMpegMediaRecorder ffmpeg_;
    DISALLOW_COPY_AND_ASSIGN(MediaRecorderSink);
  };

  static void Dispatch(const extensions::Dispatcher* dispatcher, const extensions::ScriptContext* context, int object_id,
                const std::string event, base::Value* arg) {
    base::ListValue* event_args = new base::ListValue();
    event_args->AppendInteger(object_id);
    if (arg)
      event_args->Append(arg);
    
    dispatcher->DispatchEvent(context->GetExtensionID(), event, event_args);
  }
  
  typedef std::map < int, MediaRecorderSink* > MRSMap;
  static MRSMap mrsMap;
  
  void MediaRecorder::Start(const v8::FunctionCallbackInfo<v8::Value>& args,
    const extensions::ScriptContext* context, const extensions::Dispatcher* dispatcher) {
    v8::Isolate* isolate = args.GetIsolate();

    int object_id = args[0]->ToInt32(isolate)->Value();
    std::string mime = *v8::String::Utf8Value(args[1]);
    std::string videoID = *v8::String::Utf8Value(args[2]);
    std::string audioID = *v8::String::Utf8Value(args[3]);
    if (audioID.compare("null") == 0) audioID = videoID;

    MRSMap::iterator i = mrsMap.find(object_id);
    if (i != mrsMap.end()) {
      args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate,
        "MediaRecorder already started"))));
      return;
    }
    
    const blink::WebMediaStream& video = blink::WebMediaStreamRegistry::lookupMediaStreamDescriptor(GURL(videoID));
    const blink::WebMediaStream& audio = blink::WebMediaStreamRegistry::lookupMediaStreamDescriptor(GURL(audioID));
    if (audio.isNull() && video.isNull()) {
      args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate,
        "MediaRecorder, both audio and video are null"))));
      return;
    }
    
    blink::WebVector<blink::WebMediaStreamTrack> videoTracks;
    blink::WebVector<blink::WebMediaStreamTrack> audioTracks;
    
    if(!video.isNull()) video.videoTracks(videoTracks);
    if(!audio.isNull()) audio.audioTracks(audioTracks);
    
    blink::WebMediaStreamTrack video0;
    if(videoTracks.size() > 0) video0 = videoTracks[0];
    
    blink::WebMediaStreamTrack audio0;
    if(audioTracks.size() > 0) audio0 = audioTracks[0];
    
    FFMpegMediaRecorder::EventCB event_cb = media::BindToCurrentLoop(
      base::Bind(&Dispatch, dispatcher, context, object_id));
    
    MediaRecorderSink* mrs = new MediaRecorderSink(mime.c_str(), video0, audio0, args, event_cb);
    mrsMap.insert(std::pair<MRSMap::key_type, MRSMap::mapped_type>(object_id, mrs));
    mrs->AddRef();
    args.GetReturnValue().Set(true);
  }
  
  void MediaRecorder::Stop(const v8::FunctionCallbackInfo<v8::Value>& args) {
    v8::Isolate* isolate = args.GetIsolate();

    int object_id = args[0]->ToInt32(isolate)->Value();
    MRSMap::iterator i = mrsMap.find(object_id);
    if (i == mrsMap.end()) {
      args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate,
        "MediaRecorder object id not found"))));
      return;
    }
    i->second->Stop();
    i->second->Release();
    mrsMap.erase(i);
    args.GetReturnValue().Set(true);
  }
  
  void MediaRecorder::Call(const v8::FunctionCallbackInfo<v8::Value>& args) {
        v8::Isolate* isolate = args.GetIsolate();

    int object_id = args[0]->ToInt32(isolate)->Value();
    std::string call = *v8::String::Utf8Value(args[1]);

    MRSMap::iterator i = mrsMap.find(object_id);
    if (i == mrsMap.end()) {
      args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate,
        "MediaRecorder object id not found"))));
      return;
    }

    if (call.compare("pause") == 0) {
      i->second->Pause();
    } else if (call.compare("resume") == 0) {
      i->second->Resume();
    } else if (call.compare("requestData") == 0) {
      i->second->RequestData();
    } else {
      args.GetReturnValue().Set(isolate->ThrowException(v8::Exception::Error(v8::String::NewFromUtf8(isolate,
        "MediaRecorder unknown function call"))));
      return;
    }
  }
}
