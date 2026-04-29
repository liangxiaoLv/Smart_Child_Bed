#include "gptim.h"

static bool IRAM_ATTR timer_group_isr_callback(gptimer_handle_t timer, const gptimer_alarm_event_data_t *edata, void *user_ctx)
{
    timg_config_t *user_data = (timg_config_t *)user_ctx;  //用户信息强转
    user_data->timer_counter_value = 0;   // 更新用户信息中的计数值  
    gptimer_get_raw_count(timer, &(user_data->timer_counter_value));   // 获取当前计数

    LED_TOGGLE();   // 反转LED

    if (!user_data->auto_reload) {      // 手动更新下次闹钟时间（非自动重载模式下）
        gptimer_alarm_config_t alarm_config = {     // 设置新的闹钟值 = 当前计数值+间隔时间
            .alarm_count = user_data->timer_counter_value + user_data->timing_time,
        };

        // 上面两个配置等于是一个更新了用户信息，一个更新了计时器的闹钟值，配合起来实现了非自动重载模式下的周期性闹钟功能

        gptimer_set_alarm_action(timer, &alarm_config);   //  把新配置写入计时器
    }
    return true;
}

void timg_init(timg_config_t *timg_config)    // 接收用户传参 初始化定时器
{
    gptimer_handle_t gptimer = NULL;    // 定时器handle
    gptimer_config_t timer_config = {
        .clk_src = GPTIMER_CLK_SRC_DEFAULT,     // 使用 APB 时钟 (80MHz)
        .direction = GPTIMER_COUNT_UP,       // 向上计数
        .resolution_hz = 1000000,            // 1MHz, 1 tick = 1us
    };

    ESP_ERROR_CHECK(gptimer_new_timer(&timer_config, &gptimer));   // 创建定时器

    gptimer_event_callbacks_t cbs = {
        .on_alarm = timer_group_isr_callback,   // 回调函数体
    };
    ESP_ERROR_CHECK(gptimer_register_event_callbacks(gptimer, &cbs, timg_config));   // 配置回调函数

    gptimer_alarm_config_t alarm_config = {
        .alarm_count = timg_config->alarm_value,     //  计数器达到这个值出发回调函数
        .reload_count = 0,               // 触发后计数器重载值
        .flags.auto_reload_on_alarm = timg_config->auto_reload,    //  是否自动重载
    };
    ESP_ERROR_CHECK(gptimer_set_alarm_action(gptimer, &alarm_config));  // 配置定时参数

    ESP_ERROR_CHECK(gptimer_enable(gptimer));   // 启用定时器，分配硬件资源
    ESP_ERROR_CHECK(gptimer_start(gptimer));    // 启动定时器，计数器开始计数
}