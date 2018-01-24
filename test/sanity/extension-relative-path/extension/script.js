// Listens for external messages coming from pages that match url pattern defined in manifest.json
chrome.runtime.onMessageExternal.addListener(
    function(request, sender, sendResponse) {
        console.log("Got request", request, sender);
		sendResponse({ id: chrome.runtime.id});
        return false; // Dispose of sendResponse
    }
);
console.log(chrome.runtime.id);
