/*
    Headers have been self-written
*/
#ifndef AUDIO_INIT
#define AUDIO_INIT

#include "board.h"

#include "esp_log.h"

#ifdef __cplusplus
extern "C" 
{
    #endif

    #define ALARM_MAX_STRING 40
    #define ALARM_ITEMS 3
    #define ALARM_1 0
    #define ALARM_2 1
    #define ALARM_3 2

    QueueHandle_t alarm_queue;
    static char alarm_file[ALARM_ITEMS][ALARM_MAX_STRING] = 
    {
        "/sdcard/wekker 1.mp3",
        "/sdcard/wekker 2.mp3",
        "/sdcard/wekker 3.mp3",
    };

    void initialize_audio_chip();
    void setup_wifi();
    void create_pipelines_read();
    void link_elements_alarm();
    void listen_events_read();
    void run_alarm(int selected_file); //Written by Maarten Kessels
    void start_alarm(void *selected_file); //Written by Maarten Kessels
    void stop_audio(); //Written by Maarten Kessels
    int _http_stream_event_handle(http_stream_event_msg_t *msg);
    esp_err_t alarm_queue_init();
    esp_err_t alarm_fill_queue(int selected_file); //Written by Maarten Kessels
    void stmp_timesync_event(struct timeval *tv);
    
    #ifdef __cplusplus
}
#endif


#endif