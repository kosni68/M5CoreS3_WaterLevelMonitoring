#pragma once

// Renvoie true si le point d'accès (AP) est actif (AP ou AP+STA)
bool isApModeActive();

// Tente d'entrer en deep sleep (refusé si AP actif)
void goDeepSleep();
