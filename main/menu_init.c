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
#include "audio.h"
#include "audio.c"
#include "sntp_sync.h"

static unsigned int current_menu_index = 0;
static unsigned int current_menu_id;
bool alarm_on, alarm_going_off = false;
int selected_file = 0;
char printed_alarm[];

mcp23017_t mcp23017;

SemaphoreHandle_t xMutex;
TaskHandle_t xAlarmHandle;

TimerHandle_t timer_1_sec;

char display_alarm[] = {'0','0',':','0','0'};
int hour_tenth, hour_digit, min_tenth, min_digit, cursor_position = 0;
char strftime_buf[40], cmprtime_buf[40], alarm_buf[40];

uint8_t btnState = 0x00;

smbus_info_t * smbus_info_menu;
i2c_lcd1602_info_t * lcd_info;

typedef struct 
{
 	unsigned int id;			            /* ID for this item */
  	unsigned int newId[MAX_MENU_KEY];	    /* IDs to jump to on keypress */
 	char *text[LCD_MAX_LINES];		        /* Text for this item */
 	void (*fpOnKey[MAX_MENU_KEY])(void);	/* Function pointer for each key */
  	void (*fpOnEntry)(void);		        /* Function called on entry */
  	void (*fpOnExit)(void);			        /* Function called on exit */
} MENU_ITEM_STRUCT;

MENU_ITEM_STRUCT menu[] = 
{
	{
		/*TIME MENU*/
		TIME_MAIN,
		{ 
			TIME_MAIN, ALARM_MAIN, TIME_MAIN, TIME_MAIN, //(Esc, Ok, Up, Down)
		},
		{
			"", "", " ", " "
		},
		{
			NULL, NULL, NULL, NULL, NULL, NULL
		}, NULL, NULL
	},
	{
		/*ALARM MENU*/
		ALARM_MAIN,
		{ 
			ALARM_MAIN, ALARM_MAIN, ALARM_MAIN, ALARM_MAIN, //(Esc, Ok, Up, Down)
		},
		{
			"^^ ^^    ^^", "", "vv vv    vv", " "
		},
		{
			NULL, NULL, NULL, NULL, NULL, NULL
		}, NULL, NULL
	},
};

/*
	Setting up all the neccessary components to start the application
*/
void start_application()
{
	xMutex = xSemaphoreCreateMutex();
	initialize_audio_chip();
	setup_wifi();
	setupMCP(0x20, 18, 23);
	init_lcd();
	mcp23017_write_register(&mcp23017, MCP23017_IODIR, GPIOA, 0x00);
	mcp23017_write_register(&mcp23017, MCP23017_IODIR, GPIOB, 0xFF);
	mcp23017_write_register(&mcp23017, MCP23017_GPPU, GPIOB, 0xFF);
	alarm_queue_init();
	sntp_sync(stmp_timesync_event);
	int id = 1;
	timer_1_sec = xTimerCreate("MyTimer", pdMS_TO_TICKS(1000), pdTRUE, ( void * )id, &timer_1_sec_callback);
	if( xTimerStart(timer_1_sec, 10 ) != pdPASS )
	{
		ESP_LOGE("TIMER", "Cannot start 1 second timer");
	}

	xTaskCreate(button_listener, "button_press_task", 1024 * 2, NULL, 5, NULL);

	while (1) 
	{
		handle_button_press();
		if (strcmp(alarm_buf, cmprtime_buf) == 32 && !alarm_going_off)
		{
			if (alarm_on)
			{
				menu[0].text[2] = "ALARM GOING OFF!";
				xTaskCreate(&start_alarm, "Start alarm", 4096, &selected_file, 5, &xAlarmHandle);
				alarm_going_off = true;
			}
		}
		vTaskDelay(500/ portTICK_RATE_MS);
	}
}

/**
	Split a char array so only the part that needs to be printed will be printed
	@param selected_alarm_file is the array that is chosen and needs to be split
*/
void divide_char(char *selected_alarm_file)
{
  strncpy(printed_alarm, selected_alarm_file+strlen("/sdcard/"), 15);
  printed_alarm[15] = '\0';
}

/*
	Handling when a certain button is pressed.
	Action that will be taken are depending on the button that is pressed and the menu that is selected.
*/
void handle_button_press() 
{
	switch (btnState) 
	{
	case MENU_KEY_ESC:
		if(current_menu_id == TIME_MAIN)
		{
			if (alarm_going_off)
			{
				stop_audio();
				vTaskDelete(xAlarmHandle);
				menu[0].text[2] = "";
				alarm_going_off = false;
				alarm_on = false;
				break;
			}
			else if (alarm_on)
			{
				alarm_on = false;
				menu[0].text[3] = "OFF     Set alarm ->";
			} 
			else if (!alarm_on)
			{
				alarm_on = true;
				menu[0].text[3] = "ON      Set alarm ->";
			}
		}
	    if(current_menu_id == ALARM_MAIN)
        {
			if (cursor_position >= 0)
			{
				if (cursor_position == 3)
				{
					cursor_position--;
				}
				if (cursor_position == 7)
				{
					cursor_position = cursor_position - 2;
				}
				i2c_lcd1602_move_cursor_left(lcd_info);
				cursor_position--;
			} else 
			{
				cursor_position = 0;
				current_menu_id = TIME_MAIN;
			}
        } 
		break;
	case MENU_KEY_ENTER:
		if(current_menu_id == TIME_MAIN)
		{
			divide_char(alarm_file[selected_file]);
			sprintf(alarm_buf, "%d%d:%d%d  %s", hour_tenth, hour_digit, min_tenth, min_digit, printed_alarm);
			menu[1].text[1] = alarm_buf;
		}
		if(current_menu_id == ALARM_MAIN)
		{
			if (cursor_position <= 6)
			{
				i2c_lcd1602_move_cursor_right(lcd_info);
				if (cursor_position == 1)
				{
					cursor_position++;
				}
				if (cursor_position == 4)
				{
					cursor_position = cursor_position + 2;
				}
				cursor_position++;
			} else {
				cursor_position = 0;
				current_menu_id = TIME_MAIN;
			}
		}
        else
        {    
			switch_screens(1); 
        }
		break;
	case MENU_KEY_UP:
		if(current_menu_id == TIME_MAIN)
		{

		}
        if(current_menu_id == ALARM_MAIN)
        {
			change_value("+", cursor_position);
			sprintf(alarm_buf, "%d%d:%d%d  %s", hour_tenth, hour_digit, min_tenth, min_digit, printed_alarm);
			menu[1].text[1] = alarm_buf;
        }
        else
        {
			switch_screens(2);
        }
		break;
	case MENU_KEY_DOWN:
		if(current_menu_id == TIME_MAIN)
		{

		}
        if(current_menu_id == ALARM_MAIN)
        {
			change_value("-", cursor_position);
			sprintf(alarm_buf, "%d%d:%d%d  %s", hour_tenth, hour_digit, min_tenth, min_digit, printed_alarm);
			menu[1].text[1] = alarm_buf;
        }
        else
        {
			switch_screens(3);
        }
		break;
	default:
		break;
	}
	current_menu_index = 0;
	while (menu[current_menu_index].id != current_menu_id) 
	{
		current_menu_index += 1;
	}
	print_menu_item(menu[current_menu_index].text);
	if (NULL != menu[current_menu_index].fpOnEntry) 
	{
		(*menu[current_menu_index].fpOnEntry)();
	}
}

/**
	Changing a certain value of the alarm to a new digit
	@param sort is looking if it is needed to add or subtract with 1
	@param digit is looking which digit needs to change
*/
void change_value(char *sort, int digit)
{
	int value_change = 0;
	if (strcmp("+", sort) == 0)
	{
		value_change = 1;
	} 
	if (strcmp("-", sort) == 0)
	{
		value_change = -1;
	}
	switch (digit)
	{
		case 0:
			hour_tenth = hour_tenth + value_change;
			if (hour_tenth > 2 || hour_tenth < 0)
			{
				hour_tenth = 0;
			}
			if (hour_tenth == 2 && hour_digit > 3)
			{
				hour_digit = 3;
			}
			break;
		case 1:
				hour_digit = hour_digit + value_change;
			if (hour_tenth == 2 && hour_digit > 3)
			{
				hour_digit = 0;
			}
			if (hour_digit > 9 || hour_digit < 0)
			{
				hour_digit = 0;
			}
			break;
		case 3:
			min_tenth = min_tenth + value_change;
			if (min_tenth > 5 || min_tenth < 0)
			{
				min_tenth = 0;
			}
			break;
		case 4:
			min_digit = min_digit + value_change;
			if (min_digit > 9 || min_digit < 0)
			{
				min_digit = 0;
			}
			break;
		case 7:
			selected_file = selected_file + value_change;
			if (selected_file < 0 || selected_file > 2 )
			{
				selected_file = 0;
			}
			divide_char(alarm_file[selected_file]);
	}
}

/**
	Switching between the different screens
	@param button_pressed sends which button has been pressed
*/
void switch_screens(int button_pressed)
{
	if (NULL != menu[current_menu_index].fpOnKey[button_pressed]) 
	{
	 	(*menu[current_menu_index].fpOnKey[button_pressed])();
	}
	if (NULL != menu[current_menu_index].fpOnExit) 
	{
	    (*menu[current_menu_index].fpOnExit)();
	}
	current_menu_id = menu[current_menu_index].newId[button_pressed];
}

/**
	Printing the text of the selected menu on the LCD display
	@param lines looks on which menu the LCD display is currently in
*/
void print_menu_item(char *lines[]) 
{
	menu[6].text[1] = display_alarm;	
	i2c_lcd1602_clear(lcd_info);
	i2c_lcd1602_move_cursor(lcd_info, 0,0);
	i2c_lcd1602_write_string(lcd_info, lines[0]);
	i2c_lcd1602_move_cursor(lcd_info, 0,1);
	i2c_lcd1602_write_string(lcd_info, lines[1]);
	i2c_lcd1602_move_cursor(lcd_info, 0,2);
	i2c_lcd1602_write_string(lcd_info, lines[2]);
	i2c_lcd1602_move_cursor(lcd_info, 0,3);
	i2c_lcd1602_write_string(lcd_info, lines[3]);
}


/*
	Initializing the LCD display
*/
void init_lcd(void) 
{
    smbus_info_menu = smbus_malloc();
    smbus_init(smbus_info_menu, I2C_NUM_0, 0x27);
    smbus_set_timeout(smbus_info_menu, 1000 / portTICK_RATE_MS);
    lcd_info = i2c_lcd1602_malloc();
    i2c_lcd1602_init(lcd_info, smbus_info_menu, true, LCD_NUM_ROWS, LCD_NUM_COLUMNS, LCD_NUM_VIS_COLUMNS);
}

/*
	Reading the value of the buttons
*/
void button_listener(void* pvParameters)
{
    while (1) 
	{
	   vTaskDelay(500 / portTICK_RATE_MS);
	   xSemaphoreTake( xMutex, portMAX_DELAY );
	   mcp23017_read_register(&mcp23017, MCP23017_GPIO, GPIOB, &btnState);
	   xSemaphoreGive( xMutex );
	}
    vTaskDelete(NULL);
}

/*
	Updates the time every second and prints the new time on the LCD display
*/
void timer_1_sec_callback( TimerHandle_t xTimer)
{ 
	time_t now;	
    struct tm timeinfo;
    time(&now);
	localtime_r(&now, &timeinfo);
	sprintf(strftime_buf, "%02d:%02d:%02d %02d %02d", timeinfo.tm_hour, timeinfo.tm_min, timeinfo.tm_sec, timeinfo.tm_mday, timeinfo.tm_mon + 1);
	sprintf(cmprtime_buf, "%02d:%02d", timeinfo.tm_hour, timeinfo.tm_min);
	menu[0].text[0] = "The time is:";
	menu[0].text[1] = strftime_buf;
	menu[0].text[3] = "        Set alarm ->";
}