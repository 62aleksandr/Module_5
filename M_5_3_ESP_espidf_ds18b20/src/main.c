// https://components.espressif.com/components/espressif/onewire_bus/versions/1.1.1/readme
// https://components.espressif.com/components/espressif/ds18b20/versions/0.4.0/readme
// pio run -t menuconfig

#include "app.h"

void app_main(void)
{
	app_init();

	while (1)
	{
		app_run();
	}
}