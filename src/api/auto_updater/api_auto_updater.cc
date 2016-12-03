// Copyright (c) 2014 Jefry Tedjokusumo <jtg_gg@yahoo.com.sg>
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


#include "base/values.h"
#include "content/nw/src/api/auto_updater/api_auto_updater.h"
#include "content/nw/src/api/dispatcher_host.h"
#include "content/nw/src/api/event/event.h"
#include "content/nw/src/browser/auto_updater.h"
#include <string>

namespace nwapi {
  
  class AutoUpdaterObserver : BaseEvent, public auto_updater::Delegate {
    friend class EventListener;
    EventListener* object_;

    // An error happened.
    void OnError(const std::string& error) override {
      base::ListValue arguments;
      arguments.AppendString(error);
      object_->dispatcher_host()->SendEvent(object_, "error", arguments);
    }
    
    // Checking to see if there is an update
    void OnCheckingForUpdate() override {
      base::ListValue arguments;
      object_->dispatcher_host()->SendEvent(object_, "checking-for-update", arguments);
    }
    
    // There is an update available and it is being downloaded
    void OnUpdateAvailable() override {
      base::ListValue arguments;
      object_->dispatcher_host()->SendEvent(object_, "update-available", arguments);
    }
    
    // There is no available update.
    void OnUpdateNotAvailable() override {
      base::ListValue arguments;
      object_->dispatcher_host()->SendEvent(object_, "update-not-available", arguments);
    }
    
    // There is a new update which has been downloaded.
    void OnUpdateDownloaded(const std::string& release_notes,
                                    const std::string& release_name,
                                    const base::Time& release_date,
                                    const std::string& update_url) override {
      base::ListValue arguments;
      arguments.AppendString(release_notes);
      arguments.AppendString(release_name);
      arguments.AppendDouble(release_date.ToJsTime());
      arguments.AppendString(update_url);
      object_->dispatcher_host()->SendEvent(object_, "update-downloaded", arguments);
    }

    static const int id;
    
    AutoUpdaterObserver(EventListener* object) : object_(object) {
    }
    
    ~AutoUpdaterObserver() override {
    }

  };
  
  const int AutoUpdaterObserver::id = EventListener::getUID();
  
    // static
  void AutoUpdater::Call(DispatcherHost* dispatcher_host,
                    const std::string& method,
                    const base::ListValue& arguments,
                    base::ListValue* result) {

    if (!auto_updater::AutoUpdater::GetDelegate()) {
      int object_id = 0;
      arguments.GetInteger(0, &object_id);
      EventListener* event_listener = dispatcher_host->GetApiObject<EventListener>(object_id);
      AutoUpdaterObserver* delegate = event_listener->AddListener<AutoUpdaterObserver>();
      auto_updater::AutoUpdater::SetDelegate(delegate);
    }
    
    if (method == "SetFeedURL") {
      auto_updater::AutoUpdater::HeaderMap headers;
      std::string url;
      arguments.GetString(1, &url);
      auto_updater::AutoUpdater::SetFeedURL(url, headers);
    } else if (method == "GetFeedURL") {
      std::string url = auto_updater::AutoUpdater::GetFeedURL();
      result->AppendString(url);
    } else if (method == "QuitAndInstall") {
      auto_updater::AutoUpdater::QuitAndInstall();
    } else if (method == "CheckForUpdates") {
      auto_updater::AutoUpdater::CheckForUpdates();
    }
    
  }
  
} // namespace nwapi
