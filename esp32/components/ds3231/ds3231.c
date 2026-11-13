#include "ds3231.h"

#include <assert.h>
#include <string.h>

#include "driver/i2c_master.h"
#include "esp_check.h"

static const char *TAG = "ds3231";

#define TIMEOUT_MS 50

static i2c_master_dev_handle_t s_dev;

static esp_err_t read_regs(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    return i2c_master_transmit_receive(s_dev, &reg, 1, data, len, TIMEOUT_MS);
}

// Writes len bytes from reg onwards in one transaction: the register address, then the data.
static esp_err_t write_regs(uint8_t reg, const uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(s_dev, ESP_ERR_INVALID_STATE, TAG, "not initialised");
    uint8_t buf[1 + DS3231_TIME_LEN];
    assert(len <= DS3231_TIME_LEN);
    buf[0] = reg;
    memcpy(buf + 1, data, len);
    return i2c_master_transmit(s_dev, buf, 1 + len, TIMEOUT_MS);
}

esp_err_t ds3231_init(int sda_gpio, int scl_gpio)
{
    const i2c_master_bus_config_t bus_config = {
        .i2c_port = -1,
        .sda_io_num = sda_gpio,
        .scl_io_num = scl_gpio,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
        // The ZS-042 module has its own 4.7 kΩ pull-ups; these only help when it is missing.
        .flags.enable_internal_pullup = true,
    };
    i2c_master_bus_handle_t bus;
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus), TAG, "creating the I2C bus");
    ESP_RETURN_ON_ERROR(i2c_master_probe(bus, DS3231_I2C_ADDRESS, TIMEOUT_MS), TAG,
                        "no answer at address 0x%02x on SDA GPIO %d, SCL GPIO %d",
                        DS3231_I2C_ADDRESS, sda_gpio, scl_gpio);

    const i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = DS3231_I2C_ADDRESS,
        .scl_speed_hz = 100000,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &dev_config, &s_dev), TAG, "adding the device");

    uint8_t control;
    ESP_RETURN_ON_ERROR(read_regs(DS3231_REG_CONTROL, &control, 1), TAG, "reading control");
    uint8_t wanted = control & ~(DS3231_CONTROL_EOSC | DS3231_CONTROL_A2IE | DS3231_CONTROL_A1IE);
    wanted |= DS3231_CONTROL_INTCN;
    if (wanted != control) {
        ESP_LOGI(TAG, "control register 0x%02x -> 0x%02x", control, wanted);
        ESP_RETURN_ON_ERROR(write_regs(DS3231_REG_CONTROL, &wanted, 1), TAG, "writing control");
    }
    return ESP_OK;
}

esp_err_t ds3231_read_time(ds3231_datetime_t *t, bool *stopped)
{
    uint8_t regs[DS3231_REGS_LEN];
    ESP_RETURN_ON_ERROR(read_regs(DS3231_REG_TIME, regs, sizeof regs), TAG, "reading the time");
    *stopped = (regs[DS3231_REG_STATUS] & DS3231_STATUS_OSF) != 0;
    return ds3231_decode_time(regs, t) ? ESP_OK : ESP_ERR_INVALID_RESPONSE;
}

esp_err_t ds3231_write_time(const ds3231_datetime_t *t)
{
    ESP_RETURN_ON_FALSE(ds3231_datetime_valid(t), ESP_ERR_INVALID_ARG, TAG, "invalid date or time");
    uint8_t regs[DS3231_TIME_LEN];
    ds3231_encode_time(t, regs);
    ESP_RETURN_ON_ERROR(write_regs(DS3231_REG_TIME, regs, sizeof regs), TAG, "writing the time");

    // Clear only OSF. Writing 1 to the alarm flags leaves them as they are.
    uint8_t status;
    ESP_RETURN_ON_ERROR(read_regs(DS3231_REG_STATUS, &status, 1), TAG, "reading status");
    status &= ~DS3231_STATUS_OSF;
    return write_regs(DS3231_REG_STATUS, &status, 1);
}

esp_err_t ds3231_read_alarm2(uint8_t regs[DS3231_ALARM2_LEN])
{
    return read_regs(DS3231_REG_ALARM2, regs, DS3231_ALARM2_LEN);
}

esp_err_t ds3231_write_alarm2(const uint8_t regs[DS3231_ALARM2_LEN])
{
    return write_regs(DS3231_REG_ALARM2, regs, DS3231_ALARM2_LEN);
}
