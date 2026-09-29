#pragma once

#include <string>

#include "retromanager/services/DownloadQueueManager.hpp"

namespace rm::ui {

// Localized texts for a finished download (downloads screen, notifications).

// "Terminé : le jeu est sur votre carte SD.", "Application installée dans
// /switch/RetroArch/", "Annulé", or why it failed.
std::string outcomeHeadline(const DownloadOutcome& outcome);

// One line per follow-up step (playlist, box art, cheats, icon...).
std::string outcomeDetails(const DownloadOutcome& outcome);

// "45 %  ·  12 MB / 26 MB  ·  2.1 MB/s", plus where a resume started.
std::string progressLine(const QueueItem& item);

}  // namespace rm::ui
