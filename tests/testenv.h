// The environment for a process a test starts: the caller's, with HOME and the XDG base dirs moved under home, audio
// muted, and the client's state-path overrides and opt-in switches cleared, so a child never touches the caller's files.
#pragma once
#include <QDir>
#include <QProcessEnvironment>
#include <QString>

inline QProcessEnvironment isolatedEnv(const QString& home) {
    QDir().mkpath(home);
    QProcessEnvironment e = QProcessEnvironment::systemEnvironment();
    e.insert("HOME", home);
    e.insert("XDG_CONFIG_HOME", home + "/.config");
    e.insert("XDG_CACHE_HOME", home + "/.cache");
    e.insert("XDG_DATA_HOME", home + "/.local/share");
    e.insert("XDG_STATE_HOME", home + "/.local/state");
    e.insert("FUGUE_AUDIO", "none");
    for (const char* v : {"FUGUE_RESYNC_SWEEP", "FUGUE_RESYNC_GC", "FUGUE_PAGER_CONFIG", "FUGUE_IDENTITY",
                          "FUGUE_KEYTXT", "FUGUE_BYOND_SID"})
        e.remove(v);
    return e;
}
