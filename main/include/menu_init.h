/*
    Headers have been self-written
*/
#ifndef MENU_INIT
#define MENU_INIT


#include "board.h"

#include "esp_log.h"

#ifdef __cplusplus
extern "C" 
{
    #endif

       // Error library
   #include "esp_err.h"

   // I2C driver
   #include "driver/i2c.h"

   // FreeRTOS (for delay)
   #include "freertos/task.h"

    #define MENU_KEY_ESC 254
    #define MENU_KEY_ENTER 253
    #define MENU_KEY_UP 251
    #define MENU_KEY_DOWN 247

    #define TIME_MAIN 0
    #define ALARM_MAIN 3

    #define MAX_MENU_KEY 4
    #define LCD_MAX_LINES 4

    #define I2C_MASTER_NUM           I2C_NUM_0
    #define I2C_MASTER_TX_BUF_LEN    0                     // disabled
    #define I2C_MASTER_RX_BUF_LEN    0                     // disabled
    #define I2C_MASTER_FREQ_HZ       100000
    #define I2C_MASTER_SDA_IO        18
    #define I2C_MASTER_SCL_IO        23
    #define LCD_NUM_ROWS			 4
    #define LCD_NUM_COLUMNS			 40
    #define LCD_NUM_VIS_COLUMNS		 20

    void start_application();//Written by Maarten Kessels
    void handle_button_press();//Written by Maarten Kessels
    void change_value(char *sort, int digit);//Written by Maarten Kessels
    void switch_screens(int no);
    void print_menu_item(char *lines[]);
    void init_lcd(void);
    void button_listener(void* pvParameters);
    void timer_1_sec_callback( TimerHandle_t xTimer);
    void divide_char(char *selected_alarm_file); //Written by Maarten Kessels



    #ifdef __cplusplus
}
#endif

#endif

