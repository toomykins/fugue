// The skin live_test.cpp builds its window from (in a header so moc never has to lex the raw string).
#pragma once
inline const char* kLiveSkin = R"SKIN(macro "macro"
	elem "tab"
		name = "TAB"
		command = ".winset \"mainwindow.macro=hotkeymode mapwindow.map.focus=true\""

	elem "f2"
		name = "F2"
		command = "f2cmd"

macro "hotkeymode"
	elem "tab"
		name = "TAB"
		command = ".winset \"mainwindow.macro=macro input.focus=true\""

menu "menu"
	elem "filem"
		name = "&File"
		command = ""
	elem "hi"
		name = "Say &Hi\tF3"
		command = "hi"
		category = "&File"
	elem "quit"
		name = "&Quit"
		command = ".quit"
		category = "&File"
	elem "srv"
		name = "&Server"
		command = ""
		is-disabled = true
	elem "reboot"
		name = "&Reboot"
		command = "reboot"
		category = "&Server"

window "mainwindow"
	elem "mainwindow"
		type = MAIN
		pos = 0,0
		size = 640x480
		is-default = true
		macro = "macro"
		menu = "menu"
		on-size = "sz [[inner-size]]"
	elem "input"
		type = INPUT
		pos = 0,456
		size = 640x24
		anchor1 = 0,100
		anchor2 = 100,100
		is-default = true
	elem "mainsplit"
		type = CHILD
		pos = 0,0
		size = 640x456
		anchor1 = 0,0
		anchor2 = 100,100
		left = "mapwindow"
		right = "rpane"
		is-vert = true
		splitter = 60

window "mapwindow"
	elem "mapwindow"
		type = MAIN
		size = 400x456
		is-pane = true
	elem "map"
		type = MAP
		pos = 0,0
		size = 400x456
		anchor1 = 0,0
		anchor2 = 100,100
		is-default = true
		right-click = false

window "rpane"
	elem "rpane"
		type = MAIN
		size = 240x456
		is-pane = true
	elem "btn"
		type = BUTTON
		pos = 0,210
		size = 80x20
		text = "Hide"
		command = ".winset \"rpane.lbl.text=clicked\""
	elem "lbl"
		type = LABEL
		pos = 0,240
		size = 200x20
		text = "idle"
)SKIN";
