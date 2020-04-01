#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/event_groups.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"
#include "http_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"
#include "math.h"

#include "esp_system.h"
#include "driver/gpio.h"
#include "driver/i2c.h"
#include "sdkconfig.h"
#include "rom/uart.h"
#include "i2c-lcd1602.h"
#include "smbus.h"

#include "esp_peripherals.h"
#include "periph_wifi.h"
#include "board.h"
#include "mcp23017.h"
#include "mcp23017.c"
#include "audio.h"
#include "sntp_sync.h"
#include "menu_init.h"
#include "menu_init.c"

void app_main(void)
{
    start_application();
}