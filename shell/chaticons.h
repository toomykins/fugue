// shell/chaticons.h - `\icon[x]` in chat text.
//
// The server sends it as `<IMG CLASS=icon SRC=[0xc0000NN] ICONSTATE='state'>`: NN is an ordinal in the ResourceList and
// the state is the icon_state (absent: the unnamed state, else the icon's first state, as stock draws it). Neither
// Qt's rich text nor Chromium can resolve that source, so the tag is rewritten before it reaches a chat control: to the state's south-facing first frame at the
// icon's own pixel size, as an inline PNG. Stock DreamSeeker draws it at that size too.
#pragma once
#include <QString>
#include <cstdint>
#include <functional>
#include <string>

namespace fugue::shell {

struct DmiIcon;

struct ChatIconLookup {
    enum Status {
        Absent,
        Pending,
        Ready
    } status = Absent;              // Absent: no such ordinal; Pending: listed, bytes not here yet
    std::string bytes;              // Ready: the resource file (.dmi, .png, ...), unless
    const DmiIcon* icon = nullptr;  // the caller already holds it decoded (or evaluated from a DDMI recipe)
};

// `get` answers for a ResourceList ordinal. A tag whose resource is Pending is left as it was and sets *pending, so
// the caller can retry the line once the file arrives; an Absent or undecodable one is dropped (the text around it
// stays), a named state the icon does not have draws nothing, and so does a reference to no file (SRC=[0x0], a null).
QString expandChatIcons(const QString& html, const std::function<ChatIconLookup(uint32_t ordinal)>& get,
                        bool* pending = nullptr);

}  // namespace fugue::shell
