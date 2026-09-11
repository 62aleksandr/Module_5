// adc: ADC1_IN0 PA0
// timer_pwm: TIM1_CH1 Prescaler = 0 Period = 65535 PA8
// timer_IT: TIM2 Prescaler = 99 Period = 9999
// timer_IT: TIM3 Prescaler = 9999 Period = 9999
// uart: USART2_TX PA2 RX PA3

#include "stdio.h"
#include "string.h"
#include "main.h"

extern ADC_HandleTypeDef hadc1;
extern UART_HandleTypeDef huart2;

extern volatile uint8_t pid_flag;
extern volatile uint8_t print_flag;

char trans_str[128] = {
	0,
};

uint16_t adc = 0;
int setpoint = 2400;
float dt = 0.01;
float Kp = 5.0;	  // 5.0
float Ki = 0.003; // 0.0001
float Kd = 0.000; // 0.005
int P = 0;
int I = 0;
int D = 0;
int err = 0;
int err0 = 0;
int errSUM = 0;
int out = 0;
int pwm = 0;
int ccr = 0;

void main_app_run(void)
{
	if (pid_flag)
	{
		pid_flag = 0;

		HAL_ADC_Start(&hadc1);

		if (HAL_ADC_PollForConversion(&hadc1, 1) == HAL_OK)
		{
			adc = HAL_ADC_GetValue(&hadc1);
		}

		// Розрахунок помилки
		err = setpoint - adc;

		// Інтегральна сумма помилок
		errSUM += err;

		// Пропорційна складова
		P = Kp * err;

		// Інтегральна складова
		I = Ki * errSUM;

		// Диференційна складова
		D = Kd * ((err - err0) / dt);

		// PID
		pwm = P + I + D;

		// Обмеження PWM
		if (pwm < 0)
			pwm = 0;

		if (pwm > 4095)
			pwm = 4095;

		// PWM
		TIM1->CCR1 = pwm * 16;
		// Для контроля
		ccr = TIM1->CCR1;
		// предыдущая ошибка
		err0 = err;
	}

	if (print_flag)
	{
		print_flag = 0;
		snprintf(trans_str, sizeof(trans_str), "adc = %d, err = %d,  pwm = %d, ccr = %d, P = %d, I = %d\r\n", adc, err, pwm, ccr, P, I);
		HAL_UART_Transmit(&huart2, (uint8_t *)trans_str, strlen(trans_str), 1000);
	}
}
