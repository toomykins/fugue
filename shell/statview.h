// Worker-side INFO publication and last-published row revalidation.
#pragma once
#include "shell/statpanelwidget.h"
#include "shell/statthumbnails.h"
#include "shell/mapinput.h"
#include <optional>
#include <string>
namespace fugue::shell {
// A pick of the pointer over the INFO control's rows, as StatPanelWidget::rowHovered reports it.
struct StatHover {
    int row = -1;            // an atom row of the view it was picked on, or -1: the panel's empty space
    std::string control;     // the INFO control's id, "<window>.<control>"
    quint64 generation = 0;  // that view's publication
    bool icon = false;       // the row draws an icon; iconX/iconY count in its cell as for a click
    int iconX = 0, iconY = 0;
    proto::enc::Mods mods;
};
// The hover pick MapHover takes for it (MapPointer::info), or nothing when the view it was picked on is not the one
// the panel shows now or the row no longer names the atom it showed. Stock DreamSeeker 516.1689 names a row's atom with
// the selected tab as location, or nothing over the empty space with the same location (tests/data/hover-world
// panel.json); proto::enc::infoHoverParams has the params.
std::optional<MapPointer> statHoverPointer(const model::World& world, const StatView& published, const StatHover& hover,
                                           uint64_t session);
StatView buildStatView(const model::World& world, StatThumbnails& thumbnails, AtomTrace* diagnostic = nullptr);
bool statRowCurrent(const model::World& world, const StatView& published, int row, quint64 generation);
}
