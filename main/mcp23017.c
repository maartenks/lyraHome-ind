#include "mcp23017.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "nvs_flash.h"
#include "sdkconfig.h"
#include "driver/gpio.h"
#include "driver/i2c.h"

#include "esp_log.h"
mcp23017_t mcp23017;
static const char* TAG = "MCP23017";

// disable buffers
static const size_t I2C_MASTER_TX_BUF_DISABLE = 0;
static const size_t I2C_MASTER_RX_BUF_DISABLE = 0;
static const int INTR_FLAGS = 0;

/**
 * Converts generic register and group (A/B) to register address
 * @param reg the generic register index
 * @param group the group (A/B) to compute offset
 * @return The register address specified by the parameters
*/
uint8_t mcp23017_register(mcp23017_reg_t reg, mcp23017_gpio_t group) 
{
   return (group == GPIOA)?(reg << 1):(reg << 1) | 1;
}

/**
 * Initializes the MCP23017
 * @param mcp the MCP23017 interface structure
 * @return an error code or MCP23017_ERR_OK if no error encountered
*/
mcp23017_err_t mcp23017_init_GPIO(mcp23017_t *mcp) 
{
	// setup i2c controller
	ESP_LOGV(TAG,"I2C DRIVER INSTALLED");
	// make A output, B input
	mcp23017_write_register(mcp, MCP23017_IODIR, GPIOA, 0x00);
	mcp23017_write_register(mcp, MCP23017_IODIR, GPIOB, 0xFF);

	return MCP23017_ERR_OK;
} 


/**
 * Writes a value to an MCP23017 register
 * @param mcp the MCP23017 interface structure
 * @param reg A generic register index
 * @param group the group (A/B) to compute register address offset
 * @param v the value to write to the register
 * @return an error code or MCP23017_ERR_OK if no error encountered
*/
mcp23017_err_t mcp23017_write_register(mcp23017_t *mcp, mcp23017_reg_t reg, mcp23017_gpio_t group, uint8_t v) 
{
   uint8_t r = mcp23017_register(reg, group);
   i2c_cmd_handle_t cmd = i2c_cmd_link_create();
   i2c_master_start(cmd);
   i2c_master_write_byte(cmd, mcp->i2c_addr << 1 | WRITE_BIT, ACK_CHECK_EN);
   i2c_master_write_byte(cmd, r, ACK_CHECK_EN);
   i2c_master_write_byte(cmd, v, ACK_CHECK_EN);
   i2c_master_stop(cmd);
   esp_err_t ret = i2c_master_cmd_begin(mcp->port, cmd, 1000 / portTICK_RATE_MS);
   i2c_cmd_link_delete(cmd);
   if (ret == ESP_FAIL) 
   {
      ESP_LOGE(TAG,"ERROR: unable to write to register");
      return MCP23017_ERR_FAIL;
   }
   return MCP23017_ERR_OK;
}

/**
 * Reads a value to an MCP23017 register
 * @param mcp the MCP23017 interface structure
 * @param reg A generic register index
 * @param group the group (A/B) to compute register address offset
 * @param data a pointer to an 8 bit value to be read from the device
 * @return an error code or MCP23017_ERR_OK if no error encountered
*/
mcp23017_err_t mcp23017_read_register(mcp23017_t *mcp, mcp23017_reg_t reg, mcp23017_gpio_t group, uint8_t *data) 
{
   // from the generic register and group, derive register address
   uint8_t r = mcp23017_register(reg, group);
   
	i2c_cmd_handle_t cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (mcp->i2c_addr << 1) | I2C_MASTER_WRITE, ACK_CHECK_EN);
	i2c_master_write_byte(cmd, r, 1);
	i2c_master_stop(cmd);
	esp_err_t ret =i2c_master_cmd_begin(mcp->port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if( ret == ESP_FAIL ) 
   {
	   ESP_LOGE(TAG,"ERROR: unable to write address %02x to read reg %02x",mcp->i2c_addr,r);
	   return MCP23017_ERR_FAIL;
	}
	
	cmd = i2c_cmd_link_create();
	i2c_master_start(cmd);
	i2c_master_write_byte(cmd, (mcp->i2c_addr << 1) | I2C_MASTER_READ, ACK_CHECK_EN);
	i2c_master_read_byte(cmd, data, 0);
	ret =i2c_master_cmd_begin(mcp->port, cmd, 1000 / portTICK_RATE_MS);
	i2c_cmd_link_delete(cmd);
	if( ret == ESP_FAIL ) 
   {
	   ESP_LOGE(TAG,"ERROR: unable to read reg %02x from address %02x",r,mcp->i2c_addr);
	   return MCP23017_ERR_FAIL;
	}
	return MCP23017_ERR_OK;
}

/**
 * Clears a bit from a current register value
 * @param mcp address of the MCP23017 data structure
 * @param bit The number of the bit to set
 * @param reg A generic register index
 * @param group the group (A/B) to compute register address offset
 * @return an error code or MCP23017_ERR_OK if no error encountered
*/
mcp23017_err_t mcp23017_set_bit(mcp23017_t *mcp, uint8_t bit, mcp23017_reg_t reg, mcp23017_gpio_t group) 
{   
   uint8_t current_value;
   if( mcp23017_read_register(mcp, reg, group, &current_value) != MCP23017_ERR_OK ) 
   {
      uint8_t r = mcp23017_register(reg, group);
      ESP_LOGE(TAG, "ERROR: unable to read current value of register %02x",r);
      return MCP23017_ERR_FAIL;
   }
   current_value |= 1 << bit;
   if( mcp23017_write_register(mcp, reg, group, current_value) != MCP23017_ERR_OK ) 
   {
      uint8_t r = mcp23017_register(reg, group);
      ESP_LOGE(TAG, "ERROR: unable to write new value %02X to register %02x",current_value, r);
      return MCP23017_ERR_FAIL;
   }
   return MCP23017_ERR_OK;
}

/**
 * Clears a bit from a current register value
 * @param mcp address of the MCP23017 data structure
 * @param bit The number of the bit to set
 * @param reg A generic register index
 * @param group the group (A/B) to compute register address offset
 * @return an error code or MCP23017_ERR_OK if no error encountered
*/
mcp23017_err_t mcp23017_clear_bit(mcp23017_t *mcp, uint8_t bit, mcp23017_reg_t reg, mcp23017_gpio_t group) 
{   
   uint8_t current_value;
   if( mcp23017_read_register(mcp, reg, group, &current_value) != MCP23017_ERR_OK ) 
   {
      uint8_t r = mcp23017_register(reg, group);
      ESP_LOGE(TAG, "ERROR: unable to read current value of register %02x",r);
      return MCP23017_ERR_FAIL;
   }
   current_value &= ~(1 << bit);
   if( mcp23017_write_register(mcp, reg, group, current_value) != MCP23017_ERR_OK ) 
   {
      uint8_t r = mcp23017_register(reg, group);
      ESP_LOGE(TAG, "ERROR: unable to write new value %02X to register %02x",current_value, r);
      return MCP23017_ERR_FAIL;
   }
   return MCP23017_ERR_OK;
}

void setupMCP(unsigned char addr, int sda_pin, int scl_pin)
{
    mcp23017.i2c_addr = addr;
	mcp23017.sda_pin = sda_pin;
	mcp23017.scl_pin = scl_pin;
	mcp23017.sda_pullup_en = GPIO_PULLUP_ENABLE;
	mcp23017.scl_pullup_en = GPIO_PULLUP_ENABLE;
	ESP_ERROR_CHECK(mcp23017_init_GPIO(&mcp23017));
}

void setupPorts()
{
   mcp23017_write_register(&mcp23017, MCP23017_IODIR, GPIOA, 0x00); // full port on OUTPUT
   mcp23017_write_register(&mcp23017, MCP23017_GPIO, GPIOA, 0x00); // full port on OUTPUT
   mcp23017_write_register(&mcp23017, MCP23017_IODIR, GPIOB, 0xFF); // full port on INPUT
	mcp23017_write_register(&mcp23017, MCP23017_GPIO, GPIOB, 0xFF); // full port in INPUT
}

void flash_init()
{
        esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES) 
    {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
}


