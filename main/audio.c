#include "generic.h"
#include <string.h>
#include "nvs_flash.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "sdkconfig.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"

#include "audio_element.h"
#include "audio_pipeline.h"
#include "audio_event_iface.h"
#include "audio_common.h"

#include "http_stream.h"
#include "fatfs_stream.h"
#include "i2s_stream.h"
#include "mp3_decoder.h"

#include "esp_peripherals.h"
#include "periph_sdcard.h"
#include "periph_touch.h"
#include "periph_button.h"
#include "periph_wifi.h"
#include "esp_heap_caps.h"

#include "input_key_service.h"
#include "board.h"
#include "mcp23017.h"
#include "audio.h"
#include "sntp_sync.h"

const char *TAG_AUDIO = "AUDIO_INIT";
esp_err_t ret;
int data;
char *url = NULL;

audio_pipeline_handle_t pipeline;
audio_element_handle_t http_stream_reader, i2s_stream_writer, mp3_decoder, fatfs_stream_reader;
esp_periph_set_handle_t set;
audio_event_iface_handle_t evt;
audio_board_handle_t board_handle;
esp_periph_handle_t wifi_handle;

/*
    Initialize peripherals management and start codec chip
*/
void initialize_audio_chip()
{
    ESP_LOGI(TAG_AUDIO, "Initialize peripherals management and start codec chip");
    esp_periph_config_t periph_cfg = DEFAULT_ESP_PERIPH_SET_CONFIG();
    set = esp_periph_set_init(&periph_cfg);
    audio_board_key_init(set);
    audio_board_sdcard_init(set);
    board_handle = audio_board_init();
    audio_hal_ctrl_codec(board_handle->audio_hal, AUDIO_HAL_CODEC_MODE_DECODE, AUDIO_HAL_CTRL_START);
}

/*
    setup connection and connect with the wifi
*/
void setup_wifi()
{
    flash_init();
    tcpip_adapter_init();
    esp_log_level_set("*", ESP_LOG_INFO);
    esp_log_level_set(TAG_AUDIO, ESP_LOG_INFO);

    ESP_LOGI(TAG_AUDIO, "Start and wait for Wi-Fi network");
    periph_wifi_cfg_t wifi_cfg = {
        .ssid = CONFIG_WIFI_SSID,
        .password = CONFIG_WIFI_PASSWORD,
    };
    wifi_handle = periph_wifi_init(&wifi_cfg);
    esp_periph_start(set, wifi_handle);
    periph_wifi_wait_for_connected(wifi_handle, portMAX_DELAY);
}


/**
    Handles the start of the alarm
    @param selected_file is the audio file that has been selected by the user
*/
void start_alarm(void *selected_file)
{
    int final_selected_file = *((int *)selected_file);
    create_pipelines_read();
    link_elements_alarm();
    listen_events_read();
    alarm_fill_queue(final_selected_file);
    run_alarm(final_selected_file);
}

/*
    Create audio pipeline, i2s stream, http stream, fatfs stream and mp3 decoder
*/
void create_pipelines_read()
{
    ESP_LOGI(TAG_AUDIO, "Create audio pipeline, i2s stream, http stream, fatfs stream and mp3 decoder");
    audio_pipeline_cfg_t pipeline_cfg = DEFAULT_AUDIO_PIPELINE_CONFIG();
    pipeline = audio_pipeline_init(&pipeline_cfg);
    mem_assert(pipeline);

    i2s_stream_cfg_t i2s_cfg = I2S_STREAM_CFG_DEFAULT();
    i2s_cfg.type = AUDIO_STREAM_WRITER;
    i2s_stream_writer = i2s_stream_init(&i2s_cfg);


    fatfs_stream_cfg_t fatfs_cfg = FATFS_STREAM_CFG_DEFAULT();
    fatfs_cfg.type = AUDIO_STREAM_READER;
    fatfs_stream_reader = fatfs_stream_init(&fatfs_cfg);
    audio_element_set_uri(fatfs_stream_reader, NULL);
    
    mp3_decoder_cfg_t mp3_cfg = DEFAULT_MP3_DECODER_CONFIG();
    mp3_decoder = mp3_decoder_init(&mp3_cfg);
}

/*
    Register all elements to audio pipeline and link them
*/
void link_elements_alarm()
{ 
    ESP_LOGI(TAG_AUDIO, "Register all elements to audio pipeline and link them");
    audio_pipeline_register(pipeline, fatfs_stream_reader, "file");
    audio_pipeline_register(pipeline, mp3_decoder, "mp3");
    audio_pipeline_register(pipeline, i2s_stream_writer, "i2s");

    audio_pipeline_link(pipeline, (const char *[]) {"file", "mp3", "i2s"}, 3);
}

/*
    Set up  event listener and listen
*/
void listen_events_read()
{
    ESP_LOGI(TAG_AUDIO, "Set up  event listener and listen");
    audio_event_iface_cfg_t evt_cfg = AUDIO_EVENT_IFACE_DEFAULT_CFG();
    evt = audio_event_iface_init(&evt_cfg);
    audio_pipeline_set_listener(pipeline, evt);
}

/**
    Running the alarm
    @param selected_file is to set the uri for the first time of the pipeline
*/
void run_alarm(int selected_file)
{    
	audio_element_set_uri(fatfs_stream_reader, alarm_file[selected_file]);
    while (1) 
    {
        audio_element_info_t music_info = {};
        audio_element_getinfo(mp3_decoder, &music_info);
        audio_element_setinfo(i2s_stream_writer, &music_info);
        audio_pipeline_run(pipeline);
        audio_element_state_t el_state = audio_element_get_state(i2s_stream_writer);

        if (el_state == AEL_STATE_FINISHED) 
        {
        int element = 0;
		if (uxQueueMessagesWaiting(alarm_queue) > 0 && 
			xQueueReceive(alarm_queue, &element, portMAX_DELAY)) 
        {
			    url = alarm_file[element];
			    audio_element_set_uri(fatfs_stream_reader, url);
			    audio_pipeline_reset_ringbuffer(pipeline);
			    audio_pipeline_reset_elements(pipeline);
			    audio_pipeline_change_state(pipeline, AEL_STATE_INIT);
			    audio_pipeline_run(pipeline);
		} else 
            {
			    audio_pipeline_pause(pipeline);
		    }
        }
        continue;
    }
}

/*
    Terminating the pipeline, unregistering and deinit the different elements in the pipeline
*/
void stop_audio()
{
        audio_pipeline_terminate(pipeline);

        audio_pipeline_unregister(pipeline, http_stream_reader);
        audio_pipeline_unregister(pipeline, i2s_stream_writer);
        audio_pipeline_unregister(pipeline, mp3_decoder);

        audio_pipeline_deinit(pipeline);
        audio_element_deinit(i2s_stream_writer);
        audio_element_deinit(mp3_decoder);
}

/*
    Synchronizing time with the local time in Amsterdam
*/
void stmp_timesync_event(struct timeval *tv)
{
	time_t now;
    struct tm timeinfo;
    time(&now);
	
	char strftime_buf[64];
	localtime_r(&now, &timeinfo);
    strftime(strftime_buf, sizeof(strftime_buf), "%c", &timeinfo);
    ESP_LOGI(TAG_AUDIO, "The current date/time in Amsterdam is: %s", strftime_buf);
}


/**
    Setting up the Queue where the audio files will be added to
    @return returns an OK if the Queue has been created
*/
esp_err_t alarm_queue_init() 
{
	alarm_queue = xQueueCreate( 10, sizeof( int ) );
	
	if (alarm_queue == NULL)
	{
		ESP_LOGE(TAG_AUDIO, "Error creating queue");
		return ESP_FAIL;
	}
	return ESP_OK;
}

/**
    Filling in the Queue with an alarm
    @param selected_file is the file that needs to be played
    @return returns an OK if the function did his job
*/
esp_err_t alarm_fill_queue(int selected_file) 
{
    ESP_LOGE(TAG_AUDIO, "%s", alarm_file[selected_file]);
	ret = xQueueReset(alarm_queue);

    for (int i = 0; i < 9; ++i) 
    {
	    ret = xQueueSend(alarm_queue, &selected_file, portMAX_DELAY);
    }
    return ESP_OK;
}


/**
    Handles the stream events for the radio channels
    @return returns an OK if the function did his job
*/
int _http_stream_event_handle(http_stream_event_msg_t *msg)
{
    if (msg->event_id == HTTP_STREAM_RESOLVE_ALL_TRACKS) 
    {
        return ESP_OK;
    }

    if (msg->event_id == HTTP_STREAM_FINISH_TRACK) 
    {
        return http_stream_next_track(msg->el);
    }

    if (msg->event_id == HTTP_STREAM_FINISH_PLAYLIST) 
    {
        return http_stream_restart(msg->el);
    }
    return ESP_OK;

}

