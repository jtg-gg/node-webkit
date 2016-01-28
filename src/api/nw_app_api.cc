#include "content/nw/src/api/nw_app_api.h"

#include "chrome/browser/lifetime/browser_close_manager.h"
#include "chrome/browser/lifetime/application_lifetime.h"
#include "content/public/common/content_features.h"

#include "base/command_line.h"
#include "base/memory/ptr_util.h"
#include "base/task/post_task.h"
#include "content/public/browser/browser_task_traits.h"
#include "chrome/browser/browsing_data/browsing_data_appcache_helper.h"
#include "chrome/browser/browsing_data/browsing_data_helper.h"
#include "chrome/browser/devtools/devtools_window.h"
#include "chrome/browser/extensions/devtools_util.h"
#include "chrome/browser/extensions/extension_service.h"
#include "chrome/browser/net/profile_network_context_service.h"
#include "chrome/browser/net/profile_network_context_service_factory.h"
#include "content/nw/src/api/nw_app.h"
#include "content/nw/src/nw_base.h"
#include "content/nw/src/nw_content.h"
#include "content/public/browser/render_frame_host.h"
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
#include "net/proxy_resolution/proxy_config.h"
#include "net/proxy_resolution/proxy_config_service_fixed.h"
#include "net/proxy_resolution/proxy_resolution_service.h"
#include "net/url_request/url_request_context.h"
#include "net/url_request/url_request_context_getter.h"
#include "net/traffic_annotation/network_traffic_annotation_test_helper.h"

using namespace extensions::nwapi::nw__app;

namespace extensions {
NwAppQuitFunction::NwAppQuitFunction() {

}

NwAppQuitFunction::~NwAppQuitFunction() {
}

void NwAppQuitFunction::DoJob(ExtensionService* service, std::string extension_id) {
  if (base::FeatureList::IsEnabled(::features::kNWNewWin)) {
    chrome::CloseAllBrowsersAndQuit(true);
    return;
  }
  base::ThreadTaskRunnerHandle::Get().get()->PostTask(
                                                      FROM_HERE,
                                                      base::Bind(&ExtensionService::TerminateExtension,
                                                                   service->AsWeakPtr(),
                                                                   extension_id));
}

ExtensionFunction::ResponseAction
NwAppQuitFunction::Run() {
  ExtensionService* service =
    ExtensionSystem::Get(browser_context())->extension_service();
  base::ThreadTaskRunnerHandle::Get().get()->PostTask(
        FROM_HERE,
        base::Bind(&NwAppQuitFunction::DoJob,
                   service,
                   extension_id()));
  return RespondNow(NoArguments());
}

void NwAppCloseAllWindowsFunction::DoJob(AppWindowRegistry* registry, std::string id) {
  if (base::FeatureList::IsEnabled(::features::kNWNewWin)) {
    chrome::CloseAllBrowsers();
  }
  AppWindowRegistry::AppWindowList windows =
    registry->GetAppWindowsForApp(id);

  for (AppWindow* window : windows) {
    if (window->NWCanClose())
      window->GetBaseWindow()->Close();
  }
}

ExtensionFunction::ResponseAction
NwAppCloseAllWindowsFunction::Run() {
  AppWindowRegistry* registry = AppWindowRegistry::Get(browser_context());
  if (!registry)
    return RespondNow(Error(""));
  base::ThreadTaskRunnerHandle::Get().get()->PostTask(
        FROM_HERE,
        base::Bind(&NwAppCloseAllWindowsFunction::DoJob, registry, extension()->id()));

  return RespondNow(NoArguments());
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
                                                         new CannedBrowsingDataAppCacheHelper(content::BrowserContext::GetDefaultStoragePartition(context_)
                                                                                              ->GetAppCacheService()));

  helper->DeleteAppCaches(url::Origin::Create(manifest_url));
  return true;
}

NwAppClearCacheFunction::NwAppClearCacheFunction() {
}

NwAppClearCacheFunction::~NwAppClearCacheFunction() {
}

bool NwAppClearCacheFunction::RunNWSync(base::ListValue* response, std::string* error) {
  content::BrowsingDataRemover* remover = content::BrowserContext::GetBrowsingDataRemover(
      Profile::FromBrowserContext(context_));

  remover->AddObserver(this);
  remover->RemoveAndReply(base::Time(), base::Time::Max(),
                          content::BrowsingDataRemover::DATA_TYPE_CACHE,
                          content::BrowsingDataRemover::ORIGIN_TYPE_UNPROTECTED_WEB,
                          this);
  // BrowsingDataRemover deletes itself.
  base::MessageLoopCurrent::ScopedNestableTaskAllower allow;

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
  net::ProxyConfigWithAnnotation config;
  std::unique_ptr<nwapi::nw__app::SetProxyConfig::Params> params(
      nwapi::nw__app::SetProxyConfig::Params::Create(*args_));
  EXTENSION_FUNCTION_VALIDATE(params.get());

  std::string pac_url = params->pac_url.get() ? *params->pac_url : "";
  if (!pac_url.empty()) {
    if (pac_url == "<direct>")
      config = net::ProxyConfigWithAnnotation::CreateDirect();
    else if (pac_url == "<auto>")
      config = net::ProxyConfigWithAnnotation(net::ProxyConfig::CreateAutoDetect(), TRAFFIC_ANNOTATION_FOR_TESTS);
    else
      config = net::ProxyConfigWithAnnotation(net::ProxyConfig::CreateFromCustomPacURL(GURL(pac_url)), TRAFFIC_ANNOTATION_FOR_TESTS);
  } else {
    std::string proxy_config;
    net::ProxyConfig pc;
    EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &proxy_config));
    pc.proxy_rules().ParseFromString(proxy_config);
    config = net::ProxyConfigWithAnnotation(pc, TRAFFIC_ANNOTATION_FOR_TESTS);
  }

  Profile* profile = Profile::FromBrowserContext(context_);
  auto* profile_network_context =
    ProfileNetworkContextServiceFactory::GetForContext(profile);
  profile_network_context->UpdateProxyConfig(config);
  return true;
}

static void DispatchEvent(
  const std::string& handler,
  const scoped_refptr<ExtensionFunction>& caller,
  base::ListValue* results) {

  std::unique_ptr<base::ListValue> arguments(results);
  std::unique_ptr<extensions::Event> event(
    new extensions::Event(extensions::events::UNKNOWN,
      handler, std::move(arguments)));

  EventRouter::Get(caller->browser_context())->DispatchEventToExtension(
    caller->extension_id(), std::move(event));
}

class NWJSProxyLookupClient : public network::mojom::ProxyLookupClient {
public:
  NWJSProxyLookupClient(const GURL& gurl, const std::string& token,
      const scoped_refptr<ExtensionFunction> caller,
      network::mojom::NetworkContext* network_context) :
      binding_(this), gurl_(gurl), token_(token), caller_(caller) {
    network::mojom::ProxyLookupClientPtr proxy_lookup_client;
    binding_.Bind(mojo::MakeRequest(&proxy_lookup_client));
    network_context->LookUpProxyForURL(gurl_, std::move(proxy_lookup_client));
  }
private:
  ~NWJSProxyLookupClient() override = default;
  
  // mojom::ProxyLookupClient implementation:
  void OnProxyLookupComplete(int32_t net_error,
      const base::Optional<net::ProxyInfo>& proxy_info) override {
    base::ListValue* results = new base::ListValue();
    results->AppendString(gurl_.spec());
    std::string proxy = "";
    if(proxy_info && !proxy_info->is_empty()) {
      proxy = proxy_info->proxy_server().host_port_pair().ToString();
    }
    results->AppendString(proxy);
    results->AppendString(token_);
    DispatchEvent("getHttpProxy", caller_, results);
    delete this;
  }
  
  mojo::Binding<network::mojom::ProxyLookupClient> binding_;
  const GURL gurl_;
  const std::string token_;
  const scoped_refptr<ExtensionFunction> caller_;
};

NwAppGetHttpProxyFunction::NwAppGetHttpProxyFunction() {
}

NwAppGetHttpProxyFunction::~NwAppGetHttpProxyFunction() {
}

bool NwAppGetHttpProxyFunction::RunNWSync(base::ListValue *response, std::string *error) {
  std::string url;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(0, &url));
  const GURL gurl(url);
  if (!gurl.is_valid() || !gurl.SchemeIsHTTPOrHTTPS()) {
    response->AppendBoolean(false);
    return true;
  }
  
  std::string token;
  EXTENSION_FUNCTION_VALIDATE(args_->GetString(2, &token));

  // this is important for to "format" the url
  url = gurl.spec();

  content::RenderProcessHost* render_process_host = GetSenderWebContents()->GetMainFrame()->GetProcess();
  network::mojom::NetworkContext* network_context = render_process_host->GetStoragePartition()->GetNetworkContext();
  NWJSProxyLookupClient* client = new NWJSProxyLookupClient(gurl, token, base::WrapRefCounted(this), network_context);
  response->AppendBoolean(client != nullptr);
  return true;
}
  
bool NwAppGetDataPathFunction::RunNWSync(base::ListValue* response, std::string* error) {
  response->AppendString(browser_context()->GetPath().value());
  return true;
}

ExtensionFunction::ResponseAction
NwAppCrashBrowserFunction::Run() {
  int* ptr = nullptr;
  *ptr = 1;
  return RespondNow(NoArguments());
}


} // namespace extensions
