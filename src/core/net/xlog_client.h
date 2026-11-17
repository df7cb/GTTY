// Hands a logged QSO to Xlog the way fldigi and gMFSK do: one "remote data"
// record, fields of the form `name:value` separated by '\1', sent to Xlog's
// TCP listener on port 7311. Xlog then logs the QSO, or only fills its QSO
// entry fields, depending on its own "remote data" preference.
//
// fldigi puts the same record on a System V message queue (key 1238, type 88);
// Xlog reads both, but the socket also works on macOS and across the network.
#pragma once
#include <cstdint>
#include <string>

#include "core/log/logbook.h"

namespace gtty {

// The record for one QSO. `program` names the sender in Xlog's status bar.
// Empty fields are left out, and the record is kept under the 1024 bytes Xlog
// reads in one go (the notes, then the name and QTH are shortened if needed).
std::string xlogRemoteRecord(const Qso& q, const std::string& program);

// Blocking, with a time-out of a few seconds: call it off the UI thread.
// Xlog does not answer, so true means only that the record was delivered.
bool xlogSend(const std::string& host, uint16_t port, const std::string& record, std::string& err);

}  // namespace gtty
