var nwNatives = requireNative('nw_natives');
var NWEventTokens = {};
var getHttpProxyHandler = bindingUtil.createCustomEvent("getHttpProxy", undefined, false, false);
var getHttpAuthHandler = bindingUtil.createCustomEvent("getHttpAuth", undefined, false, false);
var fullArgv = null;
var dataPath;

var eventsMap = {
  'open':             'onOpen',
  'reopen':           'onReopen'
};

var filteredArgv = [
  /^--url=/,
  /^--remote-debugging-port=/,
  /^--renderer-cmd-prefix=/,
  /^--nwapp=/
];

apiBridge.registerCustomHook(function(bindingsAPI) {
  var apiFunctions = bindingsAPI.apiFunctions;
  apiFunctions.setHandleRequest('crashRenderer', function() {
    nwNatives.crashRenderer();
  });
  bindingsAPI.compiledApi.__defineGetter__('argv', function() {
    var fullArgv = this.fullArgv;
    var argv = fullArgv.filter(function(arg) {
      return !filteredArgv.some(function(f) {
        return f.test(arg);
      });
    });

    return argv;
  });
  bindingsAPI.compiledApi.__defineGetter__('fullArgv', function() {
    if (fullArgv)
      return fullArgv;
    fullArgv = nw.App.getArgvSync();
    return fullArgv;
  });
  bindingsAPI.compiledApi.__defineGetter__('filteredArgv', function() {
    return filteredArgv;
  });
  bindingsAPI.compiledApi.__defineSetter__('filteredArgv', function(newFilteredArgv) {
    return filteredArgv = newFilteredArgv;
  });
  bindingsAPI.compiledApi.__defineGetter__('manifest', function() {
    var ret= chrome.runtime.getManifest();
    if (ret.hasOwnProperty('__nwjs_manifest'))
      return ret['__nwjs_manifest'];
    return ret;
  });
  apiFunctions.setHandleRequest('getArgvSync', function() {
    return bindingUtil.sendRequestSync('nw.App.getArgvSync', [], undefined, undefined);
  });
  apiFunctions.setHandleRequest('setProxyConfig', function() {
    bindingUtil.sendRequestSync('nw.App.setProxyConfig', $Array.from(arguments), undefined, undefined);
  });
  function getNWEvent(token) {
    var event;
    if (NWEventTokens[token]) {
      event = NWEventTokens[token];
    } else {
      event = bindingUtil.createCustomEvent(undefined, undefined, false, false);
      NWEventTokens[token] = event;
    }
    return event;
  }
  function cleanNWEvent(token) {
    bindingUtil.invalidateEvent(NWEventTokens[token]);
    delete NWEventTokens[token];
  }
  getHttpProxyHandler.addListener(function() {
    var token = arguments[2];
    var event = getNWEvent(token);
    //remove "getHttpProxy" from token to get back the original user requested url
    var url = token.substring(13);
    event.dispatch(url, arguments[1]);
    cleanNWEvent(token);
  });
  apiFunctions.setHandleRequest('getHttpProxy', function(url, callback) {
    var token = 'getHttpProxy ' + url;
    //since chrome52 there is an cache issue with http request, however if we add '?', it is guaranteed the http request is not cached
    var queryIdx = url.indexOf('?');
    if(queryIdx == -1)
      url = url + '?';
    var res = false;
    try {
     var protocol = new URL(url).protocol;
     res = protocol == "http:" || protocol == "https:";
    } catch (_) {
      res = false;
    }
    if (res && typeof callback == 'function') {
      fetch(url, {method:"HEAD"})
      .then(function() {
        var event = getNWEvent(token);
        event.addListener(callback);
        res = bindingUtil.sendRequestSync('nw.App.getHttpProxy', [url, callback, token], undefined, undefined);
        if (!res) {
          // if success cleanNWEvent will be done by getHttpProxyHandler
          cleanNWEvent(token);
        }
      })
      .catch(function() {
        //error, means "empty" proxy
        //remove "getHttpProxy" from token to get back the original user requested url
        callback(token.substring(13), "");
      });
      return true;
    }
    return false;
  });
  apiFunctions.setHandleRequest('clearCache', function() {
    bindingUtil.sendRequestSync('nw.App.clearCache', $Array.from(arguments), undefined, undefined);
  });
  apiFunctions.setHandleRequest('clearAppCache', function() {
    bindingUtil.sendRequestSync('nw.App.clearAppCache', $Array.from(arguments), undefined, undefined);
  });
  getHttpAuthHandler.addListener(function() {
    var token = arguments[0];
    var callback = getNWEvent(token);
    callback.dispatch(arguments[1], arguments[2], arguments[3]);
    cleanNWEvent(token);
  });
  apiFunctions.setHandleRequest('getHttpAuth', function(url, realm, scheme, callback) {
    // realm, scheme is not used anymore
    var res = false;
    try {
     var protocol = new URL(url).protocol;
     res = protocol == "http:" || protocol == "https:";
    } catch (_) {
      res = false;
    }
    if (res && typeof callback == 'function') {
      var http = new window.XMLHttpRequest();
      http.open("head", url, true);
      http.onreadystatechange = function (aEvt) {
        if (http.readyState == 4) {
          if(http.status != 407) {
            var token = 'getHttpAuth ' + url;
            var event = getNWEvent(token);
            event.addListener(callback);
            res = bindingUtil.sendRequestSync('nw.App.getHttpAuth', [url, token], undefined, undefined);
            if (!res) {
              // if success cleanNWEvent will be done by getHttpAuthHandler
              cleanNWEvent(token);
            }
          } else {
            callback.dispatch(url, '', '');
          }
        }
      };
      http.send();      
      return true;
    }
    return false;
  });
  apiFunctions.setHandleRequest('getProxyForURL', function() {
    return nwNatives.getProxyForURL.apply(this, arguments);
  });
  apiFunctions.setHandleRequest('addOriginAccessWhitelistEntry', function() {
    nwNatives.addOriginAccessWhitelistEntry.apply(this, arguments);
  });
  apiFunctions.setHandleRequest('removeOriginAccessWhitelistEntry', function() {
    nwNatives.removeOriginAccessWhitelistEntry.apply(this, arguments);
  });
  apiFunctions.setHandleRequest('once', function(event, listener) { //FIXME: unify with nw.Window
    if (typeof listener !== 'function')
      throw new TypeError('listener must be a function');
    var fired = false;
    var self = this;

    function g() {
      self.removeListener(event, g);
      if (!fired) {
        fired = true;
        listener.apply(self, arguments);
      }
    }
    this.on(event, g);
    return this;
  });
  apiFunctions.setHandleRequest('on', function(event, callback) {
      if (eventsMap.hasOwnProperty(event)) {
        nw.App[eventsMap[event]].addListener(callback);
      }
  });
  apiFunctions.setHandleRequest('removeListener', function(event, callback) {
      if (eventsMap.hasOwnProperty(event)) {
        nw.App[eventsMap[event]].removeListener(callback);
      }
  });
  apiFunctions.setHandleRequest('removeAllListeners', function(event) {
    if (eventsMap.hasOwnProperty(event)) {
      for (let l of
           nw.App[eventsMap[event]].getListeners()) {
        nw.App[eventsMap[event]].removeListener(l.callback);
      }
    }
  });
  apiFunctions.setHandleRequest('getDataPath', function() {
    return bindingUtil.sendRequestSync('nw.App.getDataPath', [], undefined, undefined)[0];
  });
  apiFunctions.setHandleRequest('getStartPath', function() {
    return nwNatives.getOldCwd();
  });
  bindingsAPI.compiledApi.__defineGetter__('dataPath', function() {
    if (!dataPath)
      dataPath = nw.App.getDataPath();
    return dataPath;
  });
  bindingsAPI.compiledApi.__defineGetter__('startPath', function() {
    return nw.App.getStartPath();
  });
  bindingsAPI.compiledApi.registerGlobalHotKey = function() {
    return nw.Shortcut.registerGlobalHotKey.apply(nw.Shortcut, arguments);
  };
  bindingsAPI.compiledApi.unregisterGlobalHotKey = function() {
    return nw.Shortcut.unregisterGlobalHotKey.apply(nw.Shortcut, arguments);
  };

});

//exports.binding = nw_binding.generate();

