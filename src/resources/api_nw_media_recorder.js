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

var Binding = require('binding').Binding;
var forEach = require('utils').forEach;
var nwNative = requireNative('nw_natives');
var sendRequest = require('sendRequest');
var contextMenuNatives = requireNative('context_menus');
var messagingNatives = requireNative('messaging_natives');
var NWEvent = require('event_bindings').Event;
var util = nw.require('util');

var mediaRecorderEvents = { objs: {}, stopEvent: {}, dataEvent: {} };

mediaRecorderEvents.dataEvent = new NWEvent("NWObjectMediaRecorderStart");
mediaRecorderEvents.dataEvent.addListener(function(id) {
  var obj = mediaRecorderEvents.objs[id];
  if (!obj)
    return;
  if (obj.onstart)
    obj.onstart(new Event('start'));
});

mediaRecorderEvents.stopEvent = new NWEvent("NWObjectMediaRecorderStop");
mediaRecorderEvents.stopEvent.addListener(function(id) {
  var obj = mediaRecorderEvents.objs[id];
  if (!obj)
    return;
  privates(obj).state = "inactive";
  if (obj.ondataavailable)
    obj.ondataavailable(new BlobEvent('dataavailable', {data: null}));
  if (obj.onstop)
    obj.onstop(new Event('stop'));
  delete mediaRecorderEvents.objs[id];
});

mediaRecorderEvents.dataEvent = new NWEvent("NWObjectMediaRecorderData");
mediaRecorderEvents.dataEvent.addListener(function(id, arrayBuffer) {
  var obj = mediaRecorderEvents.objs[id];
  if (!obj)
    return;
  var blobEvent = new BlobEvent('dataavailable', {data: new Blob([arrayBuffer])});
  if (obj.ondataavailable)
    obj.ondataavailable(blobEvent);
});

mediaRecorderEvents.dataEvent = new NWEvent("NWObjectMediaRecorderPause");
mediaRecorderEvents.dataEvent.addListener(function(id) {
  var obj = mediaRecorderEvents.objs[id];
  if (!obj)
    return;
  if (obj.onpause)
    obj.onpause(new Event('pause'));
});

mediaRecorderEvents.dataEvent = new NWEvent("NWObjectMediaRecorderResume");
mediaRecorderEvents.dataEvent.addListener(function(id) {
  var obj = mediaRecorderEvents.objs[id];
  if (!obj)
    return;
  if (obj.onresume)
    obj.onresume(new Event('resume'));
});

function MediaRecorder(stream, mimeType) {
  if (!(this instanceof MediaRecorder)) {
    return new MediaRecorder();
  }

  mimeType = mimeType || "video/mp4";

  this.id = contextMenuNatives.GetNextContextMenuId();
  privates(this).stream = stream;
  privates(this).mimeType = mimeType;
  privates(this).state = "inactive";
  this.ignoreMutedMedia = true;

  nw.Obj.create(this.id, 'MediaRecorder', {});
  messagingNatives.BindToGC(this, nw.Obj.destroy.bind(undefined, this.id), -1);
}

MediaRecorder.prototype.__defineGetter__('stream', function() {return privates(this).stream;});
MediaRecorder.prototype.__defineGetter__('mimeType', function() {return privates(this).mimeType;});
MediaRecorder.prototype.__defineGetter__('state', function() {return privates(this).state;});

MediaRecorder.prototype.__defineGetter__('onstart', function() {return privates(this).onstart;});
MediaRecorder.prototype.__defineSetter__('onstart', function(callback) {
  if(callback == null) {
    privates(this).onstart = callback;
  }else if(typeof(callback) == "function") {
    privates(this).onstart = callback;
  }
})

MediaRecorder.prototype.__defineGetter__('onstop', function() {return privates(this).onstop;});
MediaRecorder.prototype.__defineSetter__('onstop', function(callback) {
  if(callback == null) {
    privates(this).onstop = callback;
  }else if(typeof(callback) == "function") {
    privates(this).onstop = callback;
  }
})

MediaRecorder.prototype.__defineGetter__('ondataavailable', function() {return privates(this).ondataavailable;});
MediaRecorder.prototype.__defineSetter__('ondataavailable', function(callback) {
  if(callback == null) {
    privates(this).ondataavailable = callback;
  } else if(typeof(callback) == "function") {
    privates(this).ondataavailable = callback;
  }
})

MediaRecorder.prototype.__defineGetter__('onpause', function() {return privates(this).onpause;});
MediaRecorder.prototype.__defineSetter__('onpause', function(callback) {
  if(callback == null) {
    privates(this).onpause = callback;
  }else if(typeof(callback) == "function") {
    privates(this).onpause = callback;
  }
})

MediaRecorder.prototype.__defineGetter__('onresume', function() {return privates(this).onresume;});
MediaRecorder.prototype.__defineSetter__('onresume', function(callback) {
  if(callback == null) {
    privates(this).onresume = callback;
  }else if(typeof(callback) == "function") {
    privates(this).onresume = callback;
  }
})

MediaRecorder.prototype.setVideoURL = function (stream) {
  privates(this).stream = stream;
  return true;
}

MediaRecorder.prototype.setAudioURL = function (stream) {
  privates(this).audioStream = stream;
  return true;
}

MediaRecorder.prototype.start = function (param1, param2) {
  if (this.state != "inactive")
    throw new DOMException("InvalidStateError", "The MediaRecorder's state is " + this.state + ".");
  
  var options, timeslice;
  
  if (typeof(param1) == 'object') options = param1;
  else if (typeof(param1) == 'number') timeslice = param1;

  if (typeof(param2) == 'object') options = param2;

  options = options || {};
  timeslice = timeslice || 1000;
  
  var videoParams = "";
  var audioParams = "";
  var muxerParams = "";
  
  if (this.mimeType == "video/mp4") {
    videoParams += "preset=ultrafast;tune=zerolatency;threads=1;g=100;slices=1";
    muxerParams += "frag_duration="+timeslice*1000;
  } else if (this.mimeType == "video/webm") {
    videoParams += "g=100;threads="+Math.min(8,Math.floor((navigator.hardwareConcurrency+1)/2));
    muxerParams += "cluster_time_limit="+timeslice;
  } else if (this.mimeType == "application/ogg") {
    muxerParams += "page_duration="+timeslice*1000;
  }

  if (!options.hasOwnProperty('audioBitRate'))
      options.audioBitRate = 0;
  if (!options.hasOwnProperty('audioSampleRate'))
      options.audioSampleRate = 0;
  if (!options.hasOwnProperty('videoBitRate'))
      options.videoBitRate = 0;
  if (!options.hasOwnProperty('frameRate'))
      options.frameRate = 60;
  if (!options.hasOwnProperty('width'))
      options.width = 0;
  if (!options.hasOwnProperty('height'))
      options.height = 0;
  if (!options.hasOwnProperty('forceSync'))
      options.forceSync = 1;
  if (!options.hasOwnProperty('videoParams'))
      options.videoParams = "";
  if (!options.hasOwnProperty('audioParams'))
      options.audioParams = "";
  if (!options.hasOwnProperty('muxerParams'))
      options.muxerParams = "";

  // convert all the options to integer
  options.audioBitRate = Math.round(Number(options.audioBitRate));
  options.audioSampleRate = Math.round(Number(options.audioSampleRate));
  options.videoBitRate = Math.round(Number(options.videoBitRate));
  options.frameRate = Math.round(Number(options.frameRate));
  options.width = Math.round(Number(options.width));
  options.height = Math.round(Number(options.height));

  mediaRecorderEvents.objs[this.id] = this;
  var audioUrl = null;
  if(privates(this).audioStream) audioUrl = window.URL.createObjectURL(privates(this).audioStream);

  videoParams += ";time_base=1/"+options.frameRate;
  if (options.videoBitRate)
    videoParams += ";b="+options.videoBitRate;
  if (options.width && options.height)
    videoParams += ";video_size="+options.width+"x"+options.height;
  
  audioParams += "ar="+options.audioSampleRate;
  if(options.audioBitRate)
    audioParams += ";ab="+options.audioBitRate;
  
  videoParams += ";"+options.videoParams;
  audioParams += ";"+options.audioParams;
  muxerParams += ";"+options.muxerParams;

  var res = nwNative.MediaRecorderStart(this.id, this.mimeType,
    window.URL.createObjectURL(this.stream), audioUrl, options.forceSync,
    audioParams, videoParams, muxerParams);
  
  if(res) privates(this).state = "recording";
};

MediaRecorder.prototype.stop = function () {
  if (this.state == "inactive")
    throw new DOMException("InvalidStateError", "The MediaRecorder's state is " + this.state + ".");
  
  nwNative.MediaRecorderStop(this.id);
};

MediaRecorder.prototype.pause = function () {
  if (this.state == "inactive")
    throw new DOMException("InvalidStateError", "The MediaRecorder's state is " + this.state + ".");
  
  nwNative.MediaRecorderCall(this.id, "pause");
};

MediaRecorder.prototype.resume = function () {
  if (this.state == "inactive")
    throw new DOMException("InvalidStateError", "The MediaRecorder's state is " + this.state + ".");
  
  nwNative.MediaRecorderCall(this.id, "resume");
};

MediaRecorder.prototype.requestData = function () {
  if (this.state != "recording")
    throw new DOMException("InvalidStateError", "The MediaRecorder's state is " + this.state + ".");
  
  nwNative.MediaRecorderCall(this.id, "requestData");
};

MediaRecorder.canRecordMimeType = function (mimeType) {
  if (mimeType == "video/mp4" || mimeType == "video/webm" || mimeType == "application/ogg")
    return "probably";
  return "";
}

exports.binding = MediaRecorder;
