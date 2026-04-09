#pragma once

/// @brief Reconciles server state against the client's predicted history (not yet implemented).
///
/// When the server sends a correction, this system rewinds the ring buffer to the
/// authoritative tick and re-simulates forward to the current tick.
namespace systems
{

// TODO: implement runReconciliation()

} // namespace systems
