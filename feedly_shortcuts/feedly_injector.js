var fontSizeStyle;
var divWidthStyle;

document.getElementsByTagNameAndClassName = function(tag, clazz) {
    var el = [],
        _el = document.getElementsByTagName(tag);
    for (var i = 0; i < _el.length; i++) {
        if (_el[i].className == clazz) {
            el[el.length] = _el[i];
        }
    }

    return el;
};

var createStyle = function() {
    if (!fontSizeStyle) {
        fontSizeStyle = document.createElement('style');
        fontSizeStyle.type = 'text/css';
        document.getElementsByTagName('HEAD')[0].appendChild(fontSizeStyle);
    }
    if (!divWidthStyle) {
        divWidthStyle = document.createElement('style');
        divWidthStyle.type = 'text/css';
        document.getElementsByTagName('HEAD')[0].appendChild(divWidthStyle);
    }
}

var adjustFontSize = function (v) {
    if (localStorage.getItem('fontSize') == null) {
        localStorage.setItem('fontSize', 14);
    }
    fontSize = parseInt(localStorage.getItem('fontSize')) + v;
    localStorage.setItem('fontSize', fontSize);

    fontSizeStyle.textContent = 
        'div.content {' +
            'font-size: ' + fontSize + 'px' +
        '}' +
        'div.content span {' +
            'font-size: ' + fontSize + 'px' +
        '}' +
        'div.content pre {' +
            'font-size: 15px' +
        '}' +
        'div.content pre span {' +
            'font-size: 15px' +
        '}' +
        'div.content h1 {' +
            'font-size: ' + (fontSize + 6) + 'px' +
        '}' +
        'div.content h2 {' +
            'font-size: ' + (fontSize + 4) + 'px' +
        '}' +
        'div.content h3 {' +
            'font-size: ' + (fontSize + 2) + 'px' +
        '}' +
        'div.content h4 {' +
            'font-size: ' + (fontSize + 1) + 'px' +
        '}';
};

var adjustWidth = function (v) {
    if (localStorage.getItem('width') == null) {
        localStorage.setItem('width', 680);
    }
    width = parseInt(localStorage.getItem('width')) + v;
    localStorage.setItem('width', width);

    divWidthStyle.textContent = 
        'div.selectedEntry div.entryholder div.u100Entry {' +
            'max-width: ' + width + 'px' +
        '}' +
        'div.entryBody {' +
            'max-width: ' + width + 'px' +
        '}';
};

createStyle();
adjustFontSize(0);
adjustWidth(0);

chrome.runtime.sendMessage({method: "show_page_action"});
chrome.runtime.onMessage.addListener(function (request, sender, sendResponse) {
    if (request.method == "get_info") {
        sendResponse({
            fontSize:   localStorage.fontSize,
            width:      localStorage.width
        });
    }
    else if (request.method == "adjust_font_size") {
        adjustFontSize(request.value);
    }
    else if (request.method == "adjust_width") {
        adjustWidth(request.value);
    }
});

onkeypress = document.onkeypress;
document.onkeypress = function (e) {
    switch (String.fromCharCode(e.keyCode)) {
    case '-':
        adjustFontSize(-2);
        break;
    case '=':
        adjustFontSize(+2);
        break;
    case '_':
        adjustWidth(-20);
        break;
    case '+':
        adjustWidth(+20);
        break;
    default:
        if (onkeypress != null) {
            return onkeypress(e);
        }
    }
};

