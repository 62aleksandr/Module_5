#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "sdkconfig.h"
#include "onewire_bus.h"
#include "ds18b20.h"
#include "app.h"

#if CONFIG_EXAMPLE_ONEWIRE_ENABLE_INTERNAL_PULLUP
#define EXAMPLE_ONEWIRE_ENABLE_INTERNAL_PULLUP 1
#else
#define EXAMPLE_ONEWIRE_ENABLE_INTERNAL_PULLUP 0
#endif

#if CONFIG_EXAMPLE_ONEWIRE_BACKEND_UART
#define EXAMPLE_ONEWIRE_UART_PORT_NUM CONFIG_EXAMPLE_ONEWIRE_UART_PORT_NUM
#endif

#define EXAMPLE_ONEWIRE_BUS_GPIO CONFIG_EXAMPLE_ONEWIRE_BUS_GPIO
#define EXAMPLE_ONEWIRE_MAX_DS18B20 CONFIG_EXAMPLE_ONEWIRE_MAX_DS18B20

static const char *TAG = "example";

// Глобальные static переменные
static onewire_bus_handle_t bus = NULL;

static onewire_bus_config_t bus_config = {
	.bus_gpio_num = EXAMPLE_ONEWIRE_BUS_GPIO,
	.flags = {
		.en_pull_up = EXAMPLE_ONEWIRE_ENABLE_INTERNAL_PULLUP,
	}};

static int ds18b20_device_num = 0;
static ds18b20_device_handle_t ds18b20s[EXAMPLE_ONEWIRE_MAX_DS18B20];

static onewire_device_iter_handle_t iter = NULL;

static float temperature; // Змінна для зберігання температури

static esp_err_t ds18b20_scan(void)
{
	esp_err_t ret = ESP_OK;
	onewire_device_t next_onewire_device;

	do
	{
		// Пошук наступного пристрою на 1-Wire шині
		ret = onewire_device_iter_get_next(iter, &next_onewire_device);

		if (ret != ESP_OK)
		{
			break;
		}

		ds18b20_config_t ds_cfg = {};	  // Конфігурація DS18B20
		onewire_device_address_t address; // Адреса (ROM ID) пристрою

		// Перевірка та створення handle DS18B20
		ret = ds18b20_new_device_from_enumeration(&next_onewire_device, &ds_cfg, &ds18b20s[ds18b20_device_num]);
		if (ret != ESP_OK)
		{
			// Пристрій не DS18B20 — продовжуємо пошук
			ESP_LOGI(TAG, "Found an unknown device, address: %016llX",
					 next_onewire_device.address);
			ret = ESP_OK;
			continue;
		}
		// Отримання унікальної адреси DS18B20
		ret = ds18b20_get_device_address(ds18b20s[ds18b20_device_num], &address);
		if (ret != ESP_OK)
		{
			break;
		}

		ESP_LOGI(TAG, "Found a DS18B20[%d], address: %016llX", ds18b20_device_num, address);
		ds18b20_device_num++; // Збільшення кількості знайдених датчиків

	} while (ds18b20_device_num < EXAMPLE_ONEWIRE_MAX_DS18B20);

	// Видалення ітератора
	esp_err_t del_ret = onewire_del_device_iter(iter);

	if (ret == ESP_OK && del_ret != ESP_OK)
	{
		ret = del_ret;
	}

	if (ret == ESP_ERR_NOT_FOUND)
	{
		ret = ESP_OK;
	}

	ESP_LOGI(TAG, "Searching done, %d DS18B20 device(s) found", ds18b20_device_num);
	return ret;
}

void app_init(void)
{
// RMT (Remote Control Transceiver) APB_CLK — Advanced Peripheral Bus Clock
#if CONFIG_EXAMPLE_ONEWIRE_BACKEND_RMT
	onewire_bus_rmt_config_t rmt_config = {
		.max_rx_bytes = 10, // 1byte ROM command + 8byte ROM number + 1byte device command
	};
	ESP_ERROR_CHECK(onewire_new_bus_rmt(&bus_config, &rmt_config, &bus));
	ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d by RMT backend", EXAMPLE_ONEWIRE_BUS_GPIO);
#elif CONFIG_EXAMPLE_ONEWIRE_BACKEND_UART
	onewire_bus_uart_config_t uart_config = {
		.uart_port_num = EXAMPLE_ONEWIRE_UART_PORT_NUM,
	};
	ESP_ERROR_CHECK(onewire_new_bus_uart(&bus_config, &uart_config, &bus));
	ESP_LOGI(TAG, "1-Wire bus installed on GPIO%d by UART backend (UART%d)",
			 EXAMPLE_ONEWIRE_BUS_GPIO, EXAMPLE_ONEWIRE_UART_PORT_NUM);
#else
#error "No 1-Wire backend selected in menuconfig"
#endif // сюда постепенно перенесём код

	// create 1-wire device iterator, which is used for device search
	// Створення ітератора для пошуку пристроїв на шині
	ESP_ERROR_CHECK(onewire_new_device_iter(bus, &iter));
	ESP_LOGI(TAG, "Device iterator created, start searching...");
	ESP_ERROR_CHECK(ds18b20_scan());
}

void app_run(void)
{

	vTaskDelay(pdMS_TO_TICKS(1000));
	// trigger temperature conversion for all sensors on the bus
	ESP_ERROR_CHECK(ds18b20_trigger_temperature_conversion_for_all(bus));
	for (int i = 0; i < ds18b20_device_num; i++)
	{
		// Отримання температури конкретного DS18B20
		ESP_ERROR_CHECK(ds18b20_get_temperature(ds18b20s[i], &temperature));
		ESP_LOGI(TAG, "temperature read from DS18B20[%d]: %.2fC", i, temperature);
	}
}
