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

void convertUint16ToBooleans(int value, bool bits[16])
{
  for (int i = 15; i >= 0; --i)
  {
    bits[i] = (value & (1 << i)) != 0;
  }
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
