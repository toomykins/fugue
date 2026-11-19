// Appearance anomaly log (dev instrument): FUGUE_ANOMALY_LOG=<path> makes the model write one line per event that a
// player would see as an object flickering or turning into another object. Off (and free) when the variable is unset.
//   bind-unheld     a bind to an appearance id the client does not hold (it draws blank until the definition or a poll)
//   late-def        the definition of an id that was bound while unheld arrived N frames after the bind
//   redef-diff      an Appearance for a held id carrying different content (stock never does this unless the id was freed)
//   redef-same      the same content again for a held id
//   slot-rebind     a held slot received a different Key: a new atom on the slot with no Gone first
//   flipflop        an atom went A -> B -> A within a short time
#pragma once
#include <cstdint>
#include <string>

namespace fugue::model::anomaly {
bool on();
void frame(uint16_t type, uint64_t resyncTick, int64_t ns);  // each ingested frame
void note(const char* kind, const std::string& detail);
}  // namespace fugue::model::anomaly
