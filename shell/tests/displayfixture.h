#pragma once
// Display entries/commands from current VG interface/skin.dmf; only layout is reduced.
static const char* displaySkin = R"(menu "menu"
    elem
        name = "&Display"
        command = ""
    elem "distort"
        name = "&Distort"
        command = ".winset \"mapwindow.map.zoom-mode=distort\""
        category = "&Display"
        group = "scaling"
        can-check = true
    elem "Normal"
        name = "&Normal"
        command = ".winset \"mapwindow.map.zoom-mode=normal\""
        category = "&Display"
        is-checked = true
        can-check = true
        group = "scaling"
    elem "blur"
        name = "&Blur"
        command = ".winset \"mapwindow.map.zoom-mode=blur\""
        category = "&Display"
        group = "scaling"
        can-check = true
window "mapwindow"
    elem "mapwindow"
        type = MAIN
        size = 192x192
        is-default = true
        menu = "menu"
    elem "map"
        type = MAP
        size = 192x192
        is-default = true
        zoom = 2
        zoom-mode = normal
        letterbox = true
    elem "secondary"
        type = MAP
        size = 32x32
        is-visible = false
        zoom-mode = distort
        icon-size = 16
)";
