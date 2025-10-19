#include <M5CoreS3.h>
#include <mutex>
#include <WiFi.h>
#include "display.h"
#include "config.h"

// Gauge parameters
const int gaugeX = 250, gaugeY = 30, gaugeW = 60, gaugeH = 180;

float prevMeasured = NAN, prevEstimated = NAN;
unsigned long prevDuration = ULONG_MAX;
float prevCuveVide = NAN, prevCuvePleine = NAN;
int prevPercent = -1;

// --- Helpers Wi-Fi display ---
static void drawWifiStatusLine()
{
  // Zone de statut Wi-Fi en bas de l’écran
  M5.Display.fillRect(0, M5.Display.height() - 24, M5.Display.width(), 24, TFT_BLACK);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(8, M5.Display.height() - 22);

  // Priorité : si STA connecté -> afficher STA + RSSI, sinon si AP actif -> afficher AP, sinon OFF
  if (WiFi.status() == WL_CONNECTED)
  {
    String ip = WiFi.localIP().toString();
    int rssi = WiFi.RSSI(); // dBm
    M5.Display.printf("STA: %s (%d dBm)", ip.c_str(), rssi);
    return;
  }

  wifi_mode_t mode = WiFi.getMode();
  if (mode == WIFI_MODE_AP || mode == WIFI_MODE_APSTA)
  {
    String ip = WiFi.softAPIP().toString();
    M5.Display.printf("AP: %s", ip.c_str());
    return;
  }

  M5.Display.print("WiFi: OFF");
}

void initDisplay()
{
  M5.begin();
  M5.Display.setRotation(1);
  std::lock_guard<std::mutex> lk(displayMutex);
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextSize(2);
  M5.Display.setCursor(6, 6);
  M5.Display.println("Boot...");
}

void drawGaugeBackground()
{
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  M5.Display.setTextSize(1);
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY - 4);
  M5.Display.print("Pleine");
  M5.Display.setCursor(gaugeX + gaugeW + 6, gaugeY + gaugeH - 8);
  M5.Display.print("Vide");
}

void drawGaugeFill(int percent)
{
  M5.Display.fillRect(gaugeX + 1, gaugeY + 1, gaugeW - 2, gaugeH - 2, TFT_BLACK);
  int fillH = (int)((percent / 100.0f) * (gaugeH - 4));
  if (fillH > 0)
  {
    int yFill = gaugeY + gaugeH - 2 - fillH;
    M5.Display.fillRect(gaugeX + 2, yFill, gaugeW - 4, fillH, TFT_BLUE);
  }
  M5.Display.drawRect(gaugeX, gaugeY, gaugeW, gaugeH, TFT_WHITE);
  char buf[8];
  snprintf(buf, sizeof(buf), "%d%%", percent);
  int tx = gaugeX + (gaugeW / 2) - 12;
  int ty = gaugeY + (gaugeH / 2) - 8;
  M5.Display.setTextSize(2);
  M5.Display.setTextColor(TFT_WHITE, TFT_BLACK);
  M5.Display.setCursor(tx, ty);
  M5.Display.print(buf);
}

void updateDisplay(float measured, float estimated, unsigned long duration, float cuveVide, float cuvePleine)
{
  bool needText = false;
  if (isnan(prevMeasured) || (measured >= 0 && fabs(measured - prevMeasured) > 0.2f))
    needText = true;
  if (isnan(prevEstimated) || (estimated >= 0 && fabs(estimated - prevEstimated) > 0.2f))
    needText = true;
  if (duration != prevDuration)
    needText = true;
  if (cuveVide != prevCuveVide || cuvePleine != prevCuvePleine)
    needText = true;

  if (needText)
  {
    M5.Display.fillRect(0, 0, gaugeX - 8, 120, TFT_BLACK);
    M5.Display.setTextSize(3);
    M5.Display.setTextColor(TFT_WHITE);
    M5.Display.setCursor(8, 8);
    if (measured > 0)
      M5.Display.printf("Mes: %.1f cm\n", measured);
    else
      M5.Display.printf("Mes: --\n");
    M5.Display.setTextSize(2);
    if (estimated > -0.5f)
      M5.Display.printf("Ht: %.1f cm\n", estimated);
    else
      M5.Display.printf("Ht: --\n");
    M5.Display.setTextSize(2);
    M5.Display.printf("Dur: %lu us\n", duration);
    M5.Display.setTextSize(2);
    M5.Display.printf("Vide: %.1f\n", cuveVide);
    M5.Display.printf("Pleine: %.1f\n", cuvePleine);

    prevMeasured = measured;
    prevEstimated = estimated;
    prevDuration = duration;
    prevCuveVide = cuveVide;
    prevCuvePleine = cuvePleine;
  }

  // compute percent for gauge
  int percent = 0;
  if (measured > 0)
  {
    float denom = (cuveVide - cuvePleine);
    if (fabs(denom) >= 1e-3)
    {
      float ratio = (cuveVide - measured) / denom;
      ratio = constrain(ratio, 0.0f, 1.0f);
      percent = (int)round(ratio * 100.0f);
    }
  }

  if (percent != prevPercent)
  {
    drawGaugeFill(percent);
    prevPercent = percent;
  }

  // Wi-Fi status line (STA/AP + IP [+ RSSI])
  drawWifiStatusLine();
}

void displayTask(void *pv)
{
  // initial draw
  M5.update();
  // protect display writes with displayMutex
  {
    std::lock_guard<std::mutex> lk(displayMutex);
    M5.Display.fillScreen(TFT_BLACK);
    drawGaugeBackground();
  }
  prevPercent = -1;
  prevMeasured = NAN;
  prevEstimated = NAN;
  prevDuration = ULONG_MAX;
  prevCuveVide = cuveVide;
  prevCuvePleine = cuvePleine;

  for (;;)
  {
    float measured, estimated;
    unsigned long duration;
    {
      std::lock_guard<std::mutex> lock(distMutex);
      measured = lastMeasuredCm;
      estimated = lastEstimatedHeight;
      duration = lastDurationUs;
    }
    // left text zone update?
    bool needText = false;
    if (isnan(prevMeasured) || (measured >= 0 && fabs(measured - prevMeasured) > 0.2f))
      needText = true;
    if (isnan(prevEstimated) || (estimated >= 0 && fabs(estimated - prevEstimated) > 0.2f))
      needText = true;
    if (duration != prevDuration)
      needText = true;
    if (cuveVide != prevCuveVide || cuvePleine != prevCuvePleine)
      needText = true;

    if (needText)
    {
      std::lock_guard<std::mutex> lk(displayMutex);
      M5.Display.fillRect(0, 0, gaugeX - 8, 120, TFT_BLACK);
      M5.Display.setTextSize(3);
      M5.Display.setTextColor(TFT_WHITE);
      M5.Display.setCursor(8, 8);
      if (measured > 0)
        M5.Display.printf("Mes: %.1f cm\n", measured);
      else
        M5.Display.printf("Mes: --\n");
      M5.Display.setTextSize(2);
      if (estimated > -0.5f)
        M5.Display.printf("Ht: %.1f cm\n", estimated);
      else
        M5.Display.printf("Ht: --\n");
      M5.Display.setTextSize(2);
      M5.Display.printf("Dur: %lu us\n", duration);
      M5.Display.setTextSize(2);
      M5.Display.printf("Vide: %.1f\n", cuveVide);
      M5.Display.printf("Pleine: %.1f\n", cuvePleine);
      prevMeasured = measured;
      prevEstimated = estimated;
      prevDuration = duration;
      prevCuveVide = cuveVide;
      prevCuvePleine = cuvePleine;
    }

    // gauge percent computation
    int percent = 0;
    if (measured > 0)
    {
      float denom = (cuveVide - cuvePleine);
      if (fabs(denom) < 1e-3)
        percent = 0;
      else
      {
        float ratio = (cuveVide - measured) / denom;
        ratio = constrain(ratio, 0.0f, 1.0f);
        percent = (int)round(ratio * 100.0f);
      }
    }
    else
      percent = 0;

    if (percent != prevPercent)
    {
      std::lock_guard<std::mutex> lk(displayMutex);
      drawGaugeFill(percent);
      prevPercent = percent;
    }

    // Wi-Fi status line (STA/AP + IP [+ RSSI]), toujours mise à jour
    {
      std::lock_guard<std::mutex> lk(displayMutex);
      drawWifiStatusLine();
    }

    M5.update();

    vTaskDelay(pdMS_TO_TICKS(DISPLAY_PERIOD_MS));
  }
}
