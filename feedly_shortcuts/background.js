chrome.runtime.onMessage.addListener(function (request, sender) {
    if (request.method == "show_page_action") {
        chrome.pageAction.show(sender.tab.id);
    }
});
