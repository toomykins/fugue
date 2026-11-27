#pragma once
#include "shell/tests/replayfixture.h"
#include "shell/mapinput.h"
namespace fugue::shell::test {
struct TurfLocalFixture : ReplayFixture {
    bool load() {
        if (!ReplayFixture::load("frames.txt","turf-local-world")) return false;
        auto& w=session.worldMut();
        for (auto& [cell,start]:w.tileIconStart) start.ns=0;
        for (auto& [id,atom]:w.atoms) atom.iconStartNs=0;
        refresh(0); return true;
    }
    std::optional<MapPointer> at(QPoint point) const {
        return mapPointerAt(point,frame.source.size(),{9,9},frame.source,frame.geometry,"map",true,sprites);
    }
};
}
