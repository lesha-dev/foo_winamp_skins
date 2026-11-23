#pragma once

namespace wsz::balance {

// Current balance, -1 (left) .. 1 (right). Persisted; read by the DSP on the playback thread.
double get();
void set(double value);

// Inserts the balance DSP at the end of the core DSP chain if it isn't there yet. Main thread only.
void ensure_dsp();

// Off: balance is forced to centre and the DSP is removed from the chain. Main thread only.
bool enabled();
void set_enabled(bool on);

} // namespace wsz::balance
