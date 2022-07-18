/* Copyright (c) Microsoft Corporation. All rights reserved.
   Licensed under the MIT License. */

#pragma once

#include "dx_deferred_update.h"
#include "dx_gpio.h"
#include "dx_json_serializer.h"
#include "dx_i2c.h"
#include "dx_pwm.h"
#include "dx_terminate.h"
#include "dx_timer.h"
#include "dx_utilities.h"
#include "dx_version.h"

#include "hw/co2_monitor.h" // Hardware definition
#include "app_exit_codes.h" // application specific exit codes
#include "Onboard/azure_status.h"
#include "co2_sensor.h"

#include <applibs/applications.h>
#include <applibs/log.h>
#include <applibs/powermanagement.h>

// https://docs.microsoft.com/en-us/azure/iot-pnp/overview-iot-plug-and-play
#define IOT_PLUG_AND_PLAY_MODEL_ID "dtmi:com:example:azuresphere:co2monitor;2"
#define NETWORK_INTERFACE "wlan0"
#define CO2_MONITOR_FIRMWARE_VERSION "3.02"

/***********************************************************************************************************
 * Forward declarations
 **********************************************************************************************************/

DX_DECLARE_TIMER_HANDLER(azure_status_led_off_handler);
DX_DECLARE_TIMER_HANDLER(azure_status_led_on_handler);
static DX_DECLARE_DEVICE_TWIN_HANDLER(set_co2_alert_level);
static DX_DECLARE_DEVICE_TWIN_HANDLER(set_device_altitude);
static DX_DECLARE_TIMER_HANDLER(co2_alert_buzzer_off_handler);
static DX_DECLARE_TIMER_HANDLER(co2_alert_handler);
static DX_DECLARE_TIMER_HANDLER(delayed_restart_device_handler);
static DX_DECLARE_TIMER_HANDLER(publish_telemetry_handler);
static DX_DECLARE_TIMER_HANDLER(read_buttons_handler);
static DX_DECLARE_TIMER_HANDLER(read_telemetry_handler);
static DX_DECLARE_TIMER_HANDLER(status_rgb_off_handler);
static DX_DECLARE_TIMER_HANDLER(update_device_twins);
static DX_DECLARE_TIMER_HANDLER(watchdog_handler);
static DX_DIRECT_METHOD_RESPONSE_CODE restart_device_handler(JSON_Value *json, DX_DIRECT_METHOD_BINDING *directMethodBinding, char **responseMsg);

/***********************************************************************************************************
 * Declare global variables
 **********************************************************************************************************/

#define Log_Debug(f_, ...) dx_Log_Debug((f_), ##__VA_ARGS__)
static char Log_Debug_Time_buffer[128];

#define JSON_MESSAGE_BYTES 256
static char msgBuffer[JSON_MESSAGE_BYTES] = {0};

// Set alert level to a reasonable default. This is updated by CO2PPMAlertLevel device twin
static int32_t co2_alert_level = 1000;

static bool be_quiet = false;

ENVIRONMENT telemetry;

static DX_USER_CONFIG dx_config;
bool azure_connected = false;

static const struct itimerspec watchdogInterval = {{60, 0}, {60, 0}};
static timer_t watchdogTimer;

/***********************************************************************************************************
 * Common content properties for publish messages to IoT Hub/Central
 **********************************************************************************************************/

/// <summary>
/// Publish sensor telemetry using the following properties for efficient IoT Hub routing
/// https://docs.microsoft.com/en-us/azure/iot-hub/iot-hub-devguide-messages-d2c
/// </summary>
static DX_MESSAGE_PROPERTY *messageProperties[] = {&(DX_MESSAGE_PROPERTY){.key = "appid", .value = "co2monitor"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "type", .value = "telemetry"},
                                                   &(DX_MESSAGE_PROPERTY){.key = "schema", .value = "1"}};

static DX_MESSAGE_CONTENT_PROPERTIES contentProperties = {.contentEncoding = "utf-8", .contentType = "application/json"};

/***********************************************************************************************************
 * declare device twin bindings
 **********************************************************************************************************/

static DX_DEVICE_TWIN_BINDING dt_co2_ppm_alert_level = {.propertyName = "AlertLevel", .twinType = DX_DEVICE_TWIN_INT, .handler = set_co2_alert_level};
static DX_DEVICE_TWIN_BINDING dt_altitude_in_meters = {.propertyName = "AltitudeInMeters", .twinType = DX_DEVICE_TWIN_INT, .handler = set_device_altitude};

static DX_DEVICE_TWIN_BINDING dt_humidity = {.propertyName = "Humidity", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_pressure = {.propertyName = "Pressure", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_temperature = {.propertyName = "Temperature", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_carbon_dioxide = {.propertyName = "CarbonDioxide", .twinType = DX_DEVICE_TWIN_INT};

static DX_DEVICE_TWIN_BINDING dt_max_humidity = {.propertyName = "MaxHumidity", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_max_pressure = {.propertyName = "MaxPressure", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_max_temperature = {.propertyName = "MaxTemperature", .twinType = DX_DEVICE_TWIN_INT};
static DX_DEVICE_TWIN_BINDING dt_max_carbon_dioxide = {.propertyName = "MaxCarbonDioxide", .twinType = DX_DEVICE_TWIN_INT};

static DX_DEVICE_TWIN_BINDING dt_startup_utc = {.propertyName = "StartupUtc", .twinType = DX_DEVICE_TWIN_STRING};
static DX_DEVICE_TWIN_BINDING dt_sw_version = {.propertyName = "SoftwareVersion", .twinType = DX_DEVICE_TWIN_STRING};

static DX_DEVICE_TWIN_BINDING dt_defer_requested = {.propertyName = "DeferredUpdateRequest", .twinType = DX_DEVICE_TWIN_STRING};

/***********************************************************************************************************
 * declare direct methods
 **********************************************************************************************************/

static DX_DIRECT_METHOD_BINDING dm_restart_device = {.methodName = "RestartDevice", .handler = restart_device_handler};

/***********************************************************************************************************
 * declare gpio bindings
 **********************************************************************************************************/

DX_GPIO_BINDING gpio_network_led = {
    .pin = AZURE_CONNECTED_LED, .name = "network_led", .direction = DX_OUTPUT, .initialState = GPIO_Value_Low, .invertPin = true};
static DX_GPIO_BINDING gpio_button_b = {.pin = BUTTON_B, .name = "button_b", .direction = DX_INPUT, .detect = DX_GPIO_DETECT_LOW};

/***********************************************************************************************************
 * declare timer bindings
 **********************************************************************************************************/

DX_TIMER_BINDING tmr_azure_status_led_off = {.name = "tmr_azure_status_led_off", .handler = azure_status_led_off_handler};
DX_TIMER_BINDING tmr_azure_status_led_on = {
    .repeat = &(struct timespec){0, 500 * ONE_MS}, .name = "tmr_azure_status_led_on", .handler = azure_status_led_on_handler};
static DX_TIMER_BINDING tmr_co2_alert_buzzer_off_oneshot = {.name = "tmr_co2_alert_buzzer_off_oneshot", .handler = co2_alert_buzzer_off_handler};
static DX_TIMER_BINDING tmr_co2_alert_timer = {.repeat = &(struct timespec){8, 0}, .name = "tmr_co2_alert_timer", .handler = co2_alert_handler};
static DX_TIMER_BINDING tmr_delayed_restart_device = {.name = "tmr_delayed_restart_device", .handler = delayed_restart_device_handler};
static DX_TIMER_BINDING tmr_publish_telemetry = {.delay = &(struct timespec){60, 0}, .name = "tmr_publish_telemetry", .handler = publish_telemetry_handler};
static DX_TIMER_BINDING tmr_read_buttons = {.repeat = &(struct timespec){0, 100 * ONE_MS}, .name = "tmr_read_buttons", .handler = read_buttons_handler};
static DX_TIMER_BINDING tmr_read_telemetry = {.delay = &(struct timespec){0, 500 * ONE_MS}, .name = "tmr_read_telemetry", .handler = read_telemetry_handler};
static DX_TIMER_BINDING tmr_status_rgb_off = {.name = "tmr_status_rgb_off", .handler = status_rgb_off_handler};
static DX_TIMER_BINDING tmr_update_device_twins = {.repeat = &(struct timespec){30, 0}, .name = "tmr_update_device_twins", .handler = update_device_twins};
static DX_TIMER_BINDING tmr_watchdog = {.repeat = &(struct timespec){30, 0}, .name = "tmr_publish_telemetry", .handler = watchdog_handler};

/***********************************************************************************************************
 * declare pwm bindings
 **********************************************************************************************************/

static DX_PWM_CONTROLLER pwm_click_controller = {.controllerId = PWM_CLICK_CONTROLLER, .name = "PWM Click Controller"};
static DX_PWM_CONTROLLER pwm_rgb_controller = {.controllerId = PWM_RGB_CONTROLLER, .name = "PWM RBG Controller"};

static DX_PWM_BINDING pwm_buzz_click = {.pwmController = &pwm_click_controller, .channelId = 0, .name = "click 1 buzz"};
static DX_PWM_BINDING pwm_led_red = {.pwmController = &pwm_rgb_controller, .channelId = 0, .name = "pwm red led"};
static DX_PWM_BINDING pwm_led_green = {.pwmController = &pwm_rgb_controller, .channelId = 1, .name = "pwm green led"};
static DX_PWM_BINDING pwm_led_blue = {.pwmController = &pwm_rgb_controller, .channelId = 2, .name = "pwm green led"};

/***********************************************************************************************************
 * declare i2c bindings
 **********************************************************************************************************/

DX_I2C_BINDING i2c_co2_sensor = {.interfaceId = I2C_ISU2, .speedInHz = I2C_BUS_SPEED_STANDARD, .name = "i2c co2 sensor"};
DX_I2C_BINDING i2c_onboard_sensors = {.interfaceId = I2C_ISU2, .speedInHz = I2C_BUS_SPEED_STANDARD, .name = "i2c co2 sensor"};

/***********************************************************************************************************
 * declare binding sets
 **********************************************************************************************************/

// All bindings referenced in the following binding sets are initialised in the InitPeripheralsAndHandlers function
static DX_DEVICE_TWIN_BINDING *device_twin_bindings[] = {
    &dt_co2_ppm_alert_level, &dt_startup_utc,        &dt_sw_version,   &dt_temperature,  &dt_pressure,        &dt_humidity,           &dt_carbon_dioxide,
    &dt_defer_requested,     &dt_altitude_in_meters, &dt_max_humidity, &dt_max_pressure, &dt_max_temperature, &dt_max_carbon_dioxide,

};

static DX_PWM_BINDING *pwm_bindings[] = {&pwm_buzz_click, &pwm_led_green, &pwm_led_red, &pwm_led_blue};

DX_I2C_BINDING *i2c_bindings[] = {&i2c_co2_sensor, &i2c_onboard_sensors};

static DX_GPIO_BINDING *gpio_bindings[] = {&gpio_network_led, &gpio_button_b};
static DX_DIRECT_METHOD_BINDING *direct_method_bindings[] = {&dm_restart_device};

static DX_TIMER_BINDING *timer_bindings[] = {
    &tmr_read_telemetry,
    &tmr_azure_status_led_off,
    &tmr_azure_status_led_on,
    &tmr_co2_alert_buzzer_off_oneshot,
    &tmr_co2_alert_timer,
    &tmr_delayed_restart_device,
    &tmr_publish_telemetry,
    &tmr_read_buttons,
    &tmr_status_rgb_off,
    &tmr_update_device_twins,
    &tmr_watchdog,

};
