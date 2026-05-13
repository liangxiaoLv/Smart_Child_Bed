#include "wifi_connect.h"
// #include "red_temp.h"
// #include "mm_wave.h"
#include "ens210.h"
#include "ens160.h"
#include "rgb_led.h"

void app_main(void)
{
    // mm_wave_radar_info();
    // redTemp_start();
    wifiConnect_init();  /* 设置连接wifi的task  */
    rgbLed_work();       /* 启动RGB LED循环任务 */ 
    ens210_temp_info();
    ens160_info();
    
}
