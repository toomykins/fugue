// Answers to the browser-page handshakes that DreamSeeker's JS runtime performs and a client with no web view
// never would. Without them the server waits: its chat is queued and an asset-cache flush times out.
//
//   goonchat  : the "browseroutput" browse window means the chat datum waits for `?_src_=chat&proc=doneLoading`.
//               Until it arrives, every to_chat() is queued server side.
//   navshim   : a machine browse page (asset_cache_browser, tooltip*) is a script that navigates to a literal
//               `byond://?...` URL. The Topic that navigation produces releases a server loop.
//
// The headless client applies both (collect). The GUI shell uses only navAck, for the machine pages it never hosts
// (hostlessBrowser, shell/browsehost.cpp). Its web pages perform the goonchat handshake themselves, so posting it here too would
// double-post.
#pragma once
#include <map>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace fugue::model {

// asset_cache_browser and tooltip* windows: the pages the headless client answers for.
bool machineBrowser(const std::string& window);
// The machine pages the GUI shell answers and never hosts: asset_cache_browser only. A tooltip* page is a real page
// (vg's tooltip.js draws the tooltip in the browser control the world created for it).
bool hostlessBrowser(const std::string& window);

// The literal destination an unconditional inline-script navigation asks for, or nullopt.
std::optional<std::string> navShimHref(const std::string& html);

class HeadlessAcks {
public:
    // Topic hrefs to POST now, given the live browse windows (name -> html). Each href is returned once.
    std::vector<std::string> collect(const std::map<std::string, std::string>& windowHtml);
    // The navshim answer for ONE machine page being opened (the GUI's per-browse() entry point); nullopt when the page asks
    // nothing, is not a machine page, or that exact href was already answered.
    std::optional<std::string> navAck(const std::string& window, const std::string& html);

private:
    bool goonChatAcked_ = false;
    std::set<std::string> navAcked_;
};

}  // namespace fugue::model
