#include "thermal_mgr.h"
#include "errors.h"
#include "lm75bd.h"
#include "console.h"
#include "logging.h"

#include <FreeRTOS.h>
#include <os_task.h>
#include <os_queue.h>

#include <string.h>

#define THERMAL_MGR_STACK_SIZE 256U

static TaskHandle_t thermalMgrTaskHandle;
static StaticTask_t thermalMgrTaskBuffer;
static StackType_t thermalMgrTaskStack[THERMAL_MGR_STACK_SIZE];

#define THERMAL_MGR_QUEUE_LENGTH 10U
#define THERMAL_MGR_QUEUE_ITEM_SIZE sizeof(thermal_mgr_event_t)

static QueueHandle_t thermalMgrQueueHandle;
static StaticQueue_t thermalMgrQueueBuffer;
static uint8_t thermalMgrQueueStorageArea[THERMAL_MGR_QUEUE_LENGTH * THERMAL_MGR_QUEUE_ITEM_SIZE];

static void thermalMgr(void *pvParameters);

void initThermalSystemManager(lm75bd_config_t *config) {
  memset(&thermalMgrTaskBuffer, 0, sizeof(thermalMgrTaskBuffer));
  memset(thermalMgrTaskStack, 0, sizeof(thermalMgrTaskStack));
  
  thermalMgrTaskHandle = xTaskCreateStatic(
    thermalMgr, "thermalMgr", THERMAL_MGR_STACK_SIZE,
    config, 1, thermalMgrTaskStack, &thermalMgrTaskBuffer);

  memset(&thermalMgrQueueBuffer, 0, sizeof(thermalMgrQueueBuffer));
  memset(thermalMgrQueueStorageArea, 0, sizeof(thermalMgrQueueStorageArea));

  thermalMgrQueueHandle = xQueueCreateStatic(
    THERMAL_MGR_QUEUE_LENGTH, THERMAL_MGR_QUEUE_ITEM_SIZE,
    thermalMgrQueueStorageArea, &thermalMgrQueueBuffer);

}

error_code_t thermalMgrSendEvent(thermal_mgr_event_t *event) {
  /* Send an event to the thermal manager queue */

  if (event == NULL) {
    return ERR_CODE_INVALID_ARG;
  }

  if (!xQueueSend(thermalMgrQueueHandle, event, 10)) {
    return ERR_CODE_QUEUE_FULL;
  }

  return ERR_CODE_SUCCESS;
}

void osHandlerLM75BD(void) {
  error_code_t errCode;
  thermal_mgr_event_t event;
  event.type = THERMAL_MGR_EVENT_OS_INTERRUPT;
  LOG_IF_ERROR_CODE(thermalMgrSendEvent(&event));
}

static void thermalMgr(void *pvParameters) {
  while (1) {
    error_code_t errCode;
    thermal_mgr_event_t event;
    BaseType_t eventReceived = xQueueReceive(thermalMgrQueueHandle, &event, 10);

    if (eventReceived == pdTRUE) {
      // Switch case to easily allow for more events in the future
      switch (event.type) {
        case THERMAL_MGR_EVENT_MEASURE_TEMP_CMD:
        case THERMAL_MGR_EVENT_OS_INTERRUPT:
          // Both cases are practically the same, except we do more logging at the end if it's OS_INTERRUPT
          if (pvParameters == NULL) {
            LOG_ERROR_CODE(ERR_CODE_INVALID_ARG);
            continue;
          }
          lm75bd_config_t config = *(lm75bd_config_t *)(pvParameters);

          float temp = 0.0f;
          LOG_IF_ERROR_CODE(readTempLM75BD(config.devAddr, &temp));
          addTemperatureTelemetry(temp);

          if (event.type == THERMAL_MGR_EVENT_OS_INTERRUPT) {
            if (temp >= config.overTempThresholdCelsius) {
              overTemperatureDetected();
            } else if (temp <= config.hysteresisThresholdCelsius) {
              safeOperatingConditions();
            } else {
              LOG_ERROR_CODE(ERR_CODE_INVALID_STATE);
            }
          }
          break;
        default:
          LOG_ERROR_CODE(ERR_CODE_INVALID_STATE);
          break;
      }
    }
  }
}

void addTemperatureTelemetry(float tempC) {
  printConsole("Temperature telemetry: %f deg C\n", tempC);
}

void overTemperatureDetected(void) {
  printConsole("Over temperature detected!\n");
}

void safeOperatingConditions(void) { 
  printConsole("Returned to safe operating conditions!\n");
}
