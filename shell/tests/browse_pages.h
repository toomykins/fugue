// Page bodies and a skin for browse_test.cpp (in a header so moc never has to lex the raw strings).
#pragma once
inline const char* kPopupPage =
    R"HTML(<html><head><link rel="stylesheet" href="app.css"><script src="app.js"></script></head>
<body><img id="im" src="px.png"><div id="out">init</div><a id="lnk" href="?src=7;action=clicked">go</a></body></html>)HTML";
inline const char* kAppJs = R"JS(
window.Byond = { __callbacks__: [] };
document.addEventListener('DOMContentLoaded', function () { document.getElementById('out').textContent = 'js-ran'; });
function update(a, b) { document.getElementById('out').textContent = 'upd:' + a + ',' + b; }
function gotWinget(o) { window.wg = JSON.stringify(o); }
var evil = 0;
)JS";
inline const char* kAppCss = "#out { color: rgb(1, 2, 3); }";

inline const char* kDockSkin = R"SKIN(window "mainwindow"
	elem "mainwindow"
		type = MAIN
		pos = 0,0
		size = 640x480
		is-default = true
	elem "brw"
		type = BROWSER
		pos = 0,0
		size = 640x240
		anchor1 = 0,0
		anchor2 = 100,50
	elem "browseroutput"
		type = BROWSER
		pos = 0,240
		size = 640x240
		anchor1 = 0,50
		anchor2 = 100,100
	elem "ghost"
		type = BROWSER
		pos = 0,0
		size = 100x100
		is-visible = false
	elem "ghost2"
		type = BROWSER
		pos = 0,0
		size = 1x1
		is-visible = false

macro "macro"
	elem "F1"
		name = "F1"
		command = "f1"

macro "hotkeymode"
	elem "T"
		name = "T"
		command = "say"
	elem "CTRL+K"
		name = "CTRL+K"
		command = "kk"
)SKIN";
