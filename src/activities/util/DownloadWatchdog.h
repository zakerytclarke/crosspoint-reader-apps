#pragma once

namespace DownloadWatchdog {
extern bool gotTimeout;
void start(unsigned long timeoutMs = 15000);
void stop();
}  // namespace DownloadWatchdog
