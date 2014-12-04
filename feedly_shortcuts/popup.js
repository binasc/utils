updatePopup = function() {
    chrome.tabs.query({active: true, currentWindow: true}, function (tabs) {
        chrome.tabs.sendMessage(tabs[0].id, {method: "get_info"}, function (response) {
            document.getElementById("size").innerHTML = response.fontSize;
            document.getElementById("width").innerHTML = response.width;
        });
    });
};

document.addEventListener('DOMContentLoaded', function () {
    updatePopup();

    var btn = document.getElementById('finc');
    btn.addEventListener('click', function () {
        chrome.tabs.query({active: true, currentWindow: true}, function (tabs) {
            adjustFontSize(tabs[0].id, +2);
        });
        updatePopup();
    });
    var btn = document.getElementById('fdec');
    btn.addEventListener('click', function () {
        chrome.tabs.query({active: true, currentWindow: true}, function (tabs) {
            adjustFontSize(tabs[0].id, -2);
        });
        updatePopup();
    });
    var btn = document.getElementById('winc');
    btn.addEventListener('click', function () {
        chrome.tabs.query({active: true, currentWindow: true}, function (tabs) {
            adjustWidth(tabs[0].id, +20);
        });
        updatePopup();
    });
    var btn = document.getElementById('wdec');
    btn.addEventListener('click', function () {
        chrome.tabs.query({active: true, currentWindow: true}, function (tabs) {
            adjustWidth(tabs[0].id, -20);
        });
        updatePopup();
    });
});

function adjustFontSize(id, val)
{
    chrome.tabs.sendMessage(id, {
        method: "adjust_font_size",
        value:  val
    });
};

function adjustWidth(id, val)
{
    chrome.tabs.sendMessage(id, {
        method: "adjust_width",
        value:  val
    });
};
