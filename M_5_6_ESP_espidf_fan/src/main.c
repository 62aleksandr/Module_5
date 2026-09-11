#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_attr.h"
#include "esp_log.h"
#include <inttypes.h>
#include "driver/gptimer.h"
#include "driver/gpio.h"
#include "driver/ledc.h"

/* =========================
 * Настройки
 * ========================= */
// HALL_GPIO
#define HALL_GPIO GPIO_NUM_16
#define HALL_POLE_PAIRS 1
// Настройка окна усреднения: 2, 4 или 8
#define AVG_WINDOW_SIZE 4

//--------------- GPTimer -----------------------------
static gptimer_handle_t pid_timer = NULL;
volatile bool pid_tick = false;
#define PID_PERIOD_MS 100

//-------------- LEDC  PWM ----------------------------
#define FAN_PWM_GPIO GPIO_NUM_5
#define FAN_PWM_FREQ_HZ 20000
#define FAN_PWM_RESOLUTION LEDC_TIMER_10_BIT
#define FAN_PWM_CHANNEL LEDC_CHANNEL_0
#define FAN_PWM_MODE LEDC_LOW_SPEED_MODE

static const char *TAG = "HALL";

//----------------- Struct HALL_GPIO
typedef struct
{
	volatile bool is_done;		   // Прапорець наявності виміряного періоду
	volatile int64_t last_time_us; // Час попереднього імпульсу, мкс
	uint32_t avg_period;
	uint32_t buffer[AVG_WINDOW_SIZE];
	uint8_t head;

} gpio_result_t;

// Ініціалізація структури
static gpio_result_t gpio_result = {
	.is_done = false,
	.last_time_us = 0,
	.avg_period = 0,
	.buffer = {0},
	.head = 0};

//----------------- Структура PID
typedef struct
{
	float kp;
	float ki;
	float kd;

	float integral;
	float prev_error;

} pid_controller_t;

// Ініціалізація структури
static pid_controller_t pid = {
	.kp = 1.0f,
	.ki = 0.1f,
	.kd = 0.0f,

	.integral = 0.0f,
	.prev_error = 0.0f};

/* =========================
 * GPIO ISR
 * ========================= */
static void IRAM_ATTR hall_isr_handler(void *args)
{
	int64_t now = esp_timer_get_time();

	gpio_result_t *res = (gpio_result_t *)args;

	if (res->last_time_us != 0)
	{
		res->buffer[res->head] = (uint32_t)(now - res->last_time_us);
		res->head = (res->head + 1) % AVG_WINDOW_SIZE;
		res->is_done = true;
	}

	res->last_time_us = now;
}

/* =========================
 *GPTimer ISR
 * ========================= */
static bool IRAM_ATTR gptimer_callback(
	gptimer_handle_t timer,
	const gptimer_alarm_event_data_t *edata,
	void *user_ctx)

{
	pid_tick = true;

	return false;
}

/* =========================
 * Расчёт RPM
 * ========================= */
float hall_get_rpm(void)
{
	uint64_t sum = 0;

	for (int i = 0; i < AVG_WINDOW_SIZE; i++)
	{
		sum += gpio_result.buffer[i];
	}

	gpio_result.avg_period = sum / AVG_WINDOW_SIZE;

	if (gpio_result.avg_period == 0)
		return 0.0f;

	/*
	 * RPM = 60 000 000 / период_мкс
	 *
	 * Если на один оборот приходится
	 * несколько импульсов, учитываем HALL_POLE_PAIRS.
	 */

	return (60.0f * 1000000.0f) /
		   ((float)gpio_result.avg_period * HALL_POLE_PAIRS);
}

// установка duty
static void fan_pwm_set_duty(uint32_t duty)
{
	ledc_set_duty(
		FAN_PWM_MODE,
		FAN_PWM_CHANNEL,
		duty);

	ledc_update_duty(
		FAN_PWM_MODE,
		FAN_PWM_CHANNEL);
}

// Алгоритм PID
float pid_compute(
	pid_controller_t *pid,
	float setpoint,
	float measurement,
	float dt)
{
	float error = setpoint - measurement;

	pid->integral += error * dt;

	float derivative =
		(error - pid->prev_error) / dt;

	float output =
		pid->kp * error +
		pid->ki * pid->integral +
		pid->kd * derivative;

	pid->prev_error = error;

	return output;
}

void app_main(void)
{
	// Ініціалізація gpio
	gpio_config_t io_conf = {
		.pin_bit_mask = (1ULL << HALL_GPIO),
		.mode = GPIO_MODE_INPUT,
		.pull_up_en = GPIO_PULLUP_ENABLE,
		.pull_down_en = GPIO_PULLDOWN_DISABLE,
		.intr_type = GPIO_INTR_POSEDGE};

	gpio_config(&io_conf);

	gpio_install_isr_service(ESP_INTR_FLAG_IRAM);

	gpio_isr_handler_add(
		HALL_GPIO,
		hall_isr_handler,
		&gpio_result);

	// Ініціалізація GPTimer
	gptimer_config_t timer_config = {
		.clk_src = GPTIMER_CLK_SRC_DEFAULT,
		.direction = GPTIMER_COUNT_UP,
		.resolution_hz = 1000000, // 1 тик = 1 мкс
	};

	gptimer_new_timer(&timer_config, &pid_timer);

	gptimer_event_callbacks_t cbs = {
		.on_alarm = gptimer_callback,
	};

	gptimer_register_event_callbacks(pid_timer, &cbs, NULL);

	gptimer_alarm_config_t alarm_config = {
		.reload_count = 0,
		.alarm_count = PID_PERIOD_MS * 1000,
		.flags.auto_reload_on_alarm = true,
	};

	gptimer_set_alarm_action(pid_timer, &alarm_config);

	gptimer_enable(pid_timer);
	gptimer_start(pid_timer);

	// Ініціалізація LEDC  PWM
	ledc_timer_config_t ledc_config = {
		.speed_mode = FAN_PWM_MODE,
		.duty_resolution = FAN_PWM_RESOLUTION,
		.timer_num = LEDC_TIMER_0,
		.freq_hz = FAN_PWM_FREQ_HZ,
		.clk_cfg = LEDC_AUTO_CLK};

	ledc_timer_config(&ledc_config);

	ledc_channel_config_t channel_config = {
		.gpio_num = FAN_PWM_GPIO,
		.speed_mode = FAN_PWM_MODE,
		.channel = FAN_PWM_CHANNEL,
		.intr_type = LEDC_INTR_DISABLE,
		.timer_sel = LEDC_TIMER_0,
		.duty = 0,
		.hpoint = 0};

	ledc_channel_config(&channel_config);

	fan_pwm_set_duty(512);

	while (1)
	{
		if (gpio_result.is_done == true)
		{
			gpio_result.is_done = false;

			ESP_LOGI(TAG, "Period: %" PRIu32 " us", gpio_result.avg_period);
			ESP_LOGI(TAG, "RPM: %.1f", hall_get_rpm());
		}

		if (pid_tick)
		{
			pid_tick = false;

			// Здесь расчёт PID
			ESP_LOGI(TAG, "PID period");
		}

		vTaskDelay(pdMS_TO_TICKS(10));
	}
}