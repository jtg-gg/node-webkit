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


function MediaRecorder() {
    nw.allocateObject(this, {});
}
require('util').inherits(MediaRecorder, exports.Base);

MediaRecorder.prototype.setVideoURL = function (stream) {
    return nw.callObjectMethodSync(this, 'SetVideoURL', [stream])[0];
}

MediaRecorder.prototype.setAudioURL = function (stream) {
    return nw.callObjectMethodSync(this, 'SetAudioURL', [stream])[0];
}

MediaRecorder.prototype.start = function (filename, options) {
    if (!options.hasOwnProperty('audioBitRate'))
        options.audioBitRate = 0;
    if (!options.hasOwnProperty('audioSampleRate'))
        options.audioSampleRate = 0;
    if (!options.hasOwnProperty('videoBitRate'))
        options.videoBitRate = 0;
    if (!options.hasOwnProperty('frameRate'))
        options.frameRate = 25;
    if (!options.hasOwnProperty('width'))
        options.width = 0;
    if (!options.hasOwnProperty('height'))
        options.height = 0;
    if (!options.hasOwnProperty('forceSync'))
        options.forceSync = 1;

    // convert all the options to integer
	options.audioBitRate = Math.round(Number(options.audioBitRate));
	options.audioSampleRate = Math.round(Number(options.audioSampleRate));
	options.videoBitRate = Math.round(Number(options.videoBitRate));
	options.frameRate = Math.round(Number(options.frameRate));
	options.width = Math.round(Number(options.width));	
	options.height = Math.round(Number(options.height));

    return nw.callObjectMethodSync(this, 'Start', [filename,
        options.audioBitRate, options.audioSampleRate, options.videoBitRate,
        options.frameRate, options.width, options.height, options.forceSync])[0];
};

MediaRecorder.prototype.stop = function () {
    return nw.callObjectMethodSync(this, 'Stop', [])[0];
};

exports.MediaRecorder = MediaRecorder;
