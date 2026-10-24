#pragma once
// Services the embedding application offers the game, all optional (a headless run
// leaves them unset and the game carries on without them).

#include <functional>
#include <string>

namespace as {

struct Host {
    // getURL() and links: open a URL the way the OS does (browser, mail client).
    std::function<void(const std::string& url)> openUrl;
};

Host& host();

// getURL(url, window): passes web and mail links to host().openUrl; others are ignored.
void openUrl(const std::string& url);

}  // namespace as
