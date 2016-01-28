#include "content/nw/src/api/nw_app_api.h"

#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "chrome/browser/browsing_data/browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/browsing_data/browsing_data_remover_factory.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/devtools_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "content/nw/src/nw_base.h"
#include "content/nw/src/nw_content.h"
#include "content/public/browser/render_process_host.h"
#include "content/public/browser/render_view_host.h"
#include "content/public/browser/storage_partition.h"
#include "content/public/browser/web_contents.h"
#include "extensions/browser/app_window/app_window.h"
#include "extensions/browser/app_window/app_window_registry.h"
#include "extensions/browser/app_window/native_app_window.h"
#include "extensions/browser/extension_system.h"
#include "extensions/browser/event_router.h"
#include "extensions/common/error_utils.h"
#include "net/base/layered_network_delegate.h"
#include "net/http/http_transaction_factory.h"
#include "net/http/http_auth.h"
#include "net/proxy/proxy_config.h"
#include "net/proxy/proxy_config_service_fixed.h"
#include "net/proxy/proxy_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"

namespace {
void SetProxyConfigCallback(
    base::WaitableEvent* done,
    const scoped_refptr<net::URLRequestContextGetter>& url_request_context_getter,
    const net::ProxyConfig& proxy_config) {
  net::ProxyService* proxy_service =
      url_request_context_getter->GetURLRequestContext()->proxy_service();
  proxy_service->ResetConfigService(base::WrapUnique(new net::ProxyConfigServiceFixed(proxy_config)));
  done->Signal();
}
} // namespace

namespace extensions {
NwAppQuitFunction::NwAppQuitFunction() {

}

NwAppQuitFunction::~NwAppQuitFunction() {
}

bool NwAppQuitFunction::RunAsync() {
  ExtensionService* service =
    ExtensionSystem::Get(browser_context())->extension_service();
  base::MessageLoop::current()->PostTask(
        FROM_HERE,
        base::Bind(&ExtensionService::TerminateExtension,
                   service->AsWeakPtr(),
                   extension_id()));
  return true;
}

bool NwAppCloseAllWindowsFunction::RunAsync() {
  AppWindowRegistry* registry = AppWindowRegistry::Get(browser_context());
  if (!registry)
    return false;

  AppWindowRegistry::AppWindowList windows =
    registry->GetAppWindowsForApp(extension()->id());

  for (AppWindow* window : windows) {
    if (window->NWCanClose())
      window->GetBaseWindow()->Close();
  }
  SendResponse(true);
  return true;
}

NwAppGetArgvSyncFunction::NwAppGetArgvSyncFunction() {
}

NwAppGetArgvSyncFunction::~NwAppGetArgvSyncFunction() {
}

bool NwAppGetArgvSyncFunction::RunNWSync(base::ListValue* response, std::string* error) {

  nw::Package* package = nw::package();
  base::CommandLine* command_line = base::CommandLine::ForCurrentProcess();
  base::CommandLine::StringVector args = command_line->GetArgs();
  base::CommandLine::StringVector argv = command_line->original_argv();

  // Ignore first non-switch arg if it's not a standalone package.
  bool ignore_arg = !package->self_extract();
  for (unsigned i = 1; i < argv.size(); ++i) {
    if (ignore_arg && args.size() && argv[i] == args[0]) {
      ignore_arg = false;
      continue;
    }

    response->AppendString(argv[i]);
  }
  return true;
}

NwAppClearAppCacheFunction::NwAppClearAppCacheFunction() {
}

NwAppClearAppCacheFunction::~NwAppClearAppCacheFunction() {
}

bool NwAppClearAppCacheFunction::RunNWSync(base::ListValue* response, std::string* error) {
  std::string manifest;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &manifest));

  GURL manifest_url(manifest);
  scoped_refptr<CannedBrowsingDataAppCacheHelper> helper(
        new CannedBrowsingDataAppCacheHelper(Profile::FromBrowserContext(context_)));

  helper->DeleteAppCacheGroup(manifest_url);
  return true;
}

NwAppClearCacheFunction::NwAppClearCacheFunction() {
}

NwAppClearCacheFunction::~NwAppClearCacheFunction() {
}

bool NwAppClearCacheFunction::RunNWSync(base::ListValue* response, std::string* error) {
  BrowsingDataRemover* remover = BrowsingDataRemoverFactory::GetForBrowserContext(
      Profile::FromBrowserContext(context_));

  remover->AddObserver(this);
  remover->Remove(BrowsingDataRemover::Unbounded(),
                  BrowsingDataRemover::REMOVE_CACHE,
                  BrowsingDataHelper::ALL);
  // BrowsingDataRemover deletes itself.
  base::MessageLoop::ScopedNestableTaskAllower allow(
        base::MessageLoop::current());
  run_loop_.Run();
  remover->RemoveObserver(this);
  return true;
}

void NwAppClearCacheFunction::OnBrowsingDataRemoverDone() {
  DCHECK_CURRENTLY_ON(content::BrowserThread::UI);
  run_loop_.Quit();
}

NwAppSetProxyConfigFunction::NwAppSetProxyConfigFunction() {
}

NwAppSetProxyConfigFunction::~NwAppSetProxyConfigFunction() {
}

bool NwAppSetProxyConfigFunction::RunNWSync(base::ListValue* response, std::string* error) {
  std::string proxy_config;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &proxy_config));

  base::ThreadRestrictions::ScopedAllowWait allow_wait;

  net::ProxyConfig config;
  config.proxy_rules().ParseFromString(proxy_config);
  content::RenderProcessHost* render_process_host = GetSenderWebContents()->GetRenderProcessHost();
  net::URLRequestContextGetter* context_getter =
    render_process_host->GetStoragePartition()->GetURLRequestContext();

  base::WaitableEvent done(false, false);
  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&SetProxyConfigCallback, &done,
                 make_scoped_refptr(context_getter), config));
  done.Wait();
  return true;
}

static void DispatchEvent(
  const std::string token,
  const scoped_refptr<UIThreadExtensionFunction>& caller,
  base::ListValue* results) {

  std::unique_ptr<base::ListValue> arguments(results);
  std::unique_ptr<extensions::Event> event(
    new extensions::Event(extensions::events::UNKNOWN,
      token, std::move(arguments)));

  EventRouter::Get(caller->browser_context())->DispatchEventToExtension(
    caller->extension_id(), std::move(event));
}

class NWJSNetworkDelegate : public net::LayeredNetworkDelegate {
private:
  struct EventToken {
    std::string proxy_;
    typedef std::pair<std::string,scoped_refptr<UIThreadExtensionFunction>> TokenCaller;
    std::list<TokenCaller> tokens_callers_;
  };
  std::map<const std::string, EventToken> url_proxy_map_;
  
  void HandleURLProxyListener(const net::URLRequest* request, const net::HostPortPair* proxy = NULL, bool run_callback = false) {
    const std::string urlSpec = request->original_url().spec();
    std::map<const std::string, EventToken>::iterator i = url_proxy_map_.find(urlSpec);

    if (i != url_proxy_map_.end()) {
      if (proxy == NULL) proxy = &request->proxy_server();
      if (proxy->HostForURL().length()) i->second.proxy_ = proxy->ToString();
      if (run_callback) {
        //send final proxy result to JS
        for (auto c : i->second.tokens_callers_) {
          base::ListValue* results = new base::ListValue();
          results->AppendString(urlSpec);
          results->AppendString(i->second.proxy_);

          content::BrowserThread::PostTask(
            content::BrowserThread::UI, FROM_HERE,
            base::Bind(&DispatchEvent, c.first, c.second,
                       results));
        }

        url_proxy_map_.erase(i);
      }
    }
  }
  
  void OnHeadersReceivedInternal(
      net::URLRequest* request,
      const net::CompletionCallback& callback,
      const net::HttpResponseHeaders* original_response_headers,
      scoped_refptr<net::HttpResponseHeaders>* override_response_headers,
      GURL* allowed_unsafe_redirect_url) override {
    HandleURLProxyListener(request);
  }
  
  void OnCompletedInternal(net::URLRequest* request, bool started) override {
    HandleURLProxyListener(request, NULL, true);
  }
  
  void OnAuthRequiredInternal(
      net::URLRequest* request,
      const net::AuthChallengeInfo& auth_info,
      const AuthCallback& callback,
      net::AuthCredentials* credentials) override {
    net::HostPortPair proxy = net::HostPortPair::FromString(auth_info.challenger.Serialize());
    HandleURLProxyListener(request, &proxy);
  }
  
  NWJSNetworkDelegate(std::unique_ptr<net::NetworkDelegate> network_delegate) :
    LayeredNetworkDelegate(std::move(network_delegate)) {
  }
  
  ~NWJSNetworkDelegate() override {}
  
  bool AddURLProxyListener(  UIThreadExtensionFunction* caller, const std::string& url, const std::string& token) {
    std::map<const std::string, EventToken>::iterator i = url_proxy_map_.find(url);
    if (i == url_proxy_map_.end()) {
      EventToken eventToken;
      eventToken.proxy_ = "";
      eventToken.tokens_callers_.push_back(EventToken::TokenCaller(token,caller));
      url_proxy_map_.insert(std::pair<const std::string, EventToken>(url, eventToken));
      return true;
    }
    // url aready exist, replace the token event in the map
    i->second.tokens_callers_.push_back(EventToken::TokenCaller(token,caller));
    return false;
  }

public:

  static bool AddURLProxyListener(net::URLRequestContext* url_request_context, UIThreadExtensionFunction* caller,
                                  const std::string& url, const std::string& token) {
    net::NetworkDelegate* network_delegate = url_request_context->network_delegate();
    static std::set<net::NetworkDelegate*> all_instance;

    if(all_instance.find(network_delegate) == all_instance.end()) {
      std::unique_ptr<net::NetworkDelegate> nd(network_delegate);
      network_delegate = new NWJSNetworkDelegate(std::move(nd));
      all_instance.insert(network_delegate);
      //this is where we inject our NWJSNetworkDelegate
      url_request_context->set_network_delegate(network_delegate);
    }

    NWJSNetworkDelegate* nd = static_cast<NWJSNetworkDelegate*>(network_delegate);
    return nd->AddURLProxyListener(caller, url, token);
  }
};
  
NwAppGetHttpProxyFunction::NwAppGetHttpProxyFunction() {
}
  
NwAppGetHttpProxyFunction::~NwAppGetHttpProxyFunction() {
}

static void GetHttpProxyCallbackIO(
  const scoped_refptr<UIThreadExtensionFunction>& caller,
  const scoped_refptr<net::URLRequestContextGetter>& url_request_context_getter,
  const std::string url, const std::string token) {
  net::URLRequestContext* rc = url_request_context_getter->GetURLRequestContext();
  NWJSNetworkDelegate::AddURLProxyListener(rc, caller.get(), url, token);

  base::ListValue* results = new base::ListValue();
  results->AppendString(url);

  //send first event to JS to make the http header URL request
  content::BrowserThread::PostTask(
    content::BrowserThread::UI, FROM_HERE,
    base::Bind(&DispatchEvent, token, caller,
              results));
}

bool NwAppGetHttpProxyFunction::RunNWSync(base::ListValue *response, std::string *error) {
  std::string url;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));
  const GURL gurl(url);
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
    response->AppendBoolean(false);
    return true;
  }
  
  const std::string token = "getHttpProxy " + url;
  // this is important for to "format" the url
  url = gurl.spec();

  content::RenderProcessHost* render_process_host = GetSenderWebContents()->GetRenderProcessHost();
  net::URLRequestContextGetter* context_getter =
    render_process_host->GetStoragePartition()->GetURLRequestContext();

  content::BrowserThread::PostTask(
      content::BrowserThread::IO, FROM_HERE,
      base::Bind(&GetHttpProxyCallbackIO, make_scoped_refptr(this), make_scoped_refptr(context_getter),
                 url, token));

  response->AppendBoolean(true);
  return true;
}
  
NwAppGetHttpAuthFunction::NwAppGetHttpAuthFunction() {
}

NwAppGetHttpAuthFunction::~NwAppGetHttpAuthFunction() {
}

static void GetHttpAuthCallbackIO(
    const scoped_refptr<net::URLRequestContextGetter>& url_request_context_getter,
    const scoped_refptr<UIThreadExtensionFunction>& caller,
    const GURL gurl, std::string realm, std::string scheme, int deep, std::string token) {
  net::HttpAuthCache* auth_cache =
    url_request_context_getter->GetURLRequestContext()->http_transaction_factory()->GetSession()->http_auth_cache();
  
  net::HttpAuth::Scheme httpScheme;
  for(int i=0; i<net::HttpAuth::AUTH_SCHEME_MAX; i++) {
    httpScheme = static_cast<net::HttpAuth::Scheme>(i);
    if(scheme.compare(net::HttpAuth::SchemeToString(httpScheme))==0)
      break;
  }
  
  net::AuthCredentials credentials;
  net::HttpAuthCache::Entry* entry = auth_cache->Lookup(gurl.GetOrigin(), realm, httpScheme);
  if (entry == NULL)
    entry = auth_cache->LookupByPath(gurl.GetOrigin(), gurl.path());

  if (entry) {
    credentials = entry->credentials();
    realm = entry->realm();
    scheme = net::HttpAuth::SchemeToString(entry->scheme());
  }

  base::ListValue* results = new base::ListValue();
  if (credentials.username().length() || credentials.password().length()) {
    results->AppendString(gurl.spec());
    results->AppendString(credentials.username());
    results->AppendString(credentials.password());
  } else {
    results->AppendString(token);
    results->AppendString(gurl.spec());
    results->AppendString(realm);
    results->AppendString(scheme);
    results->AppendInteger(deep);
  }

  content::BrowserThread::PostTask(
    content::BrowserThread::UI, FROM_HERE,
    base::Bind(&DispatchEvent, token, caller,
              results));
}

bool NwAppGetHttpAuthFunction::RunNWSync(base::ListValue* response, std::string* error) {
  std::string url, realm, scheme, token;
  int deep;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));
  const GURL gurl(url);
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
    response->AppendBoolean(false);
    return false;
  }

  EXTENSION_FUNCTION_VALIDATE(args_->GetString(1, &realm));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(2, &scheme));
  EXTENSION_FUNCTION_VALIDATE(args_->GetInteger(4, &deep));
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(5, &token));
  
  content::RenderProcessHost* render_process_host = GetSenderWebContents()->GetRenderProcessHost();
  net::URLRequestContextGetter* context_getter =
    render_process_host->GetStoragePartition()->GetURLRequestContext();
  
  content::BrowserThread::PostTask(
    content::BrowserThread::IO, FROM_HERE,
    base::Bind(&GetHttpAuthCallbackIO, make_scoped_refptr(context_getter),
      make_scoped_refptr(this), gurl, realm, scheme, deep, token));
  response->AppendBoolean(true);

  return true;
}
  
bool NwAppGetDataPathFunction::RunNWSync(base::ListValue* response, std::string* error) {
  response->AppendString(browser_context()->GetPath().value());
  return true;
}

bool NwAppCrashBrowserFunction::RunAsync() {
  int* ptr = nullptr;
  *ptr = 1;
  return true;
}


} // namespace extensions
