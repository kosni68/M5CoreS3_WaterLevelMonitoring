// config.cpp
#include "config.h"

std::atomic<uint32_t> interactiveLastTouchMs;

std::mutex distMutex;
std::mutex mqttMutex;
std::mutex displayMutex;
