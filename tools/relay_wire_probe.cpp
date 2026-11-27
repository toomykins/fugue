// Narrow, guest-only synthetic-world evidence recorder. Never used by MAP.
#include "net/session.h"
#include "model/world.h"
#include "model/json.h"
#include <fstream>
#include <iostream>
int main(int argc, char** argv) {
    if (argc != 4) { std::cerr << "usage: relay_wire_probe PORT STATE_DIR OUTPUT\n"; return 2; }
    const int port = std::stoi(argv[1]);
    if (port < 1024 || port > 65535 || port == 1337) return 2;
    fugue::net::SessionConfig cfg; cfg.port = uint16_t(port); cfg.stateDir = argv[2];
    cfg.wireLogPath = std::string(argv[3]) + ".wire";
    fugue::net::Session session(cfg);
    fugue::model::Session model;
    session.setObserver([&](uint16_t type, fugue::ByteSpan body) { model.ingest(type, body); });
    const auto error = session.connectAndLogin();
    if (!error.empty()) { std::cerr << error; return 1; }
    session.run(std::chrono::seconds(4));
    const auto& world = model.world();
    std::ofstream out(argv[3]);
    for (const auto& [id, ap] : world.appearances) out << "appearance " << id << ' ' << ap.fieldsJson << '\n';
    for (const auto& [id, image] : world.images) out << "image " << id << ' ' << image << '\n';
    return session.stats().sawWorldInit ? 0 : 1;
}
