void printLogHeapStack()
{ /*
   size_t stackUsed = uxTaskGetStackHighWaterMark(task_handle_modbus);
   log_d("stackUsed task_handle_modbus  = %d", stackUsed);
   stackUsed = uxTaskGetStackHighWaterMark(task_handle_dbConnect);
   log_d("stackUsed task_handle_dbConnect  = %d", stackUsed);
   stackUsed = uxTaskGetStackHighWaterMark(task_handle_led);
   log_d("stackUsed task_handle_led  = %d", stackUsed);
 */

  uint32_t memory = esp_get_free_heap_size();
  log_d("memory = %d", memory);

  memory = esp_get_minimum_free_heap_size();
  log_d("memory mini = %d", memory);

  log_d("Total heap: %d", ESP.getHeapSize());
  log_d("Free heap: %d", ESP.getFreeHeap());
  log_d("Total PSRAM: %d", ESP.getPsramSize());
  log_d("Free PSRAM: %d", ESP.getFreePsram());
}

class R_TRIG
{
public:
  bool q;
  R_TRIG() : last_clk(false), q(false) {}

  bool operator()(bool clk)
  {
    if (clk && !last_clk)
    {
      q = true;
    }
    else
    {
      q = false;
    }
    last_clk = clk;
    return q;
  }

private:
  bool last_clk;
};

R_TRIG pressInBtn;

class TempBool
{
private:
  bool state;
  unsigned long activationTime;
  const unsigned long duration;

public:
  TempBool(unsigned long durationMillis) : state(false), duration(durationMillis) {}

  void activate()
  {
    state = true;
    activationTime = millis();
  }

  bool isActive() const
  {
    return state && (millis() - activationTime) < duration;
  }

  void deactivate()
  {
    state = false;
  }
};

TempBool tempPressInBtn(300);
TempBool tempPressOutBtn(300);
TempBool tempPressInMode(300);
TempBool tempPressOutMode(300);
TempBool tempChangeMode(500);

void convertUint16ToBooleans(int value, bool bits[16])
{
  for (int i = 15; i >= 0; --i)
  {
    bits[i] = (value & (1 << i)) != 0;
  }
}

String generateRandomString(uint length)
{
  const char characters[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789";
  const int numCharacters = sizeof(characters) - 1; // Exclude the null terminator

  char randomChars[length + 1]; // characters + 1 for null terminator

  for (int i = 0; i < length; i++)
  {
    int randomIndex = random(numCharacters);
    randomChars[i] = characters[randomIndex];
  }
  randomChars[length] = '\0'; // Add null terminator to make it a valid C-string

  return String(randomChars);
}

void updateMinMaxTime(unsigned int startTime, unsigned int &currentTime, unsigned int &minTime, unsigned int &maxTime)
{

  currentTime = millis() - startTime;

  if (currentTime < minTime)
  {
    minTime = currentTime;
  }
  else if (currentTime > maxTime)
  {
    maxTime = currentTime;
  }
}
