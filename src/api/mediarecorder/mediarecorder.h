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

#ifndef CONTENT_NW_SRC_API_MEDIARECORDER_MEDIARECORDER_H_
#define CONTENT_NW_SRC_API_MEDIARECORDER_MEDIARECORDER_H_

#include "base/compiler_specific.h"
#include "base/values.h"
#include "content/nw/src/api/base/base.h"
#include "content/public/browser/render_process_host.h"
#include "v8/include/v8.h"

namespace extensions {
  class ScriptContext;
  class Dispatcher;
}

namespace nw {

  class MediaRecorder : public Base {
  public:
    MediaRecorder(int id,
      const base::WeakPtr<ObjectManager>& object_manager,
      const base::DictionaryValue& option,
      const std::string& extension_id);
    ~MediaRecorder() override;

    static void Start(const v8::FunctionCallbackInfo<v8::Value>& args,
      const extensions::ScriptContext* context, const extensions::Dispatcher* dispatcher);

    static void Stop(const v8::FunctionCallbackInfo<v8::Value>& args);
    static void Call(const v8::FunctionCallbackInfo<v8::Value>& args);

  private:
    DISALLOW_COPY_AND_ASSIGN(MediaRecorder);
  };

}  // namespace nw

#endif  // CONTENT_NW_SRC_API_MEDIARECORDER_MEDIARECORDER_H_
