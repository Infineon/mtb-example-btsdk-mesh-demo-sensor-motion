/*
 * Copyright 2016-2022, Cypress Semiconductor Corporation (an Infineon company) or
 * an affiliate of Cypress Semiconductor Corporation.  All rights reserved.
 *
 * This software, including source code, documentation and related
 * materials ("Software") is owned by Cypress Semiconductor Corporation
 * or one of its affiliates ("Cypress") and is protected by and subject to
 * worldwide patent protection (United States and foreign),
 * United States copyright laws and international treaty provisions.
 * Therefore, you may use this Software only as provided in the license
 * agreement accompanying the software package from which you
 * obtained this Software ("EULA").
 * If no EULA applies, Cypress hereby grants you a personal, non-exclusive,
 * non-transferable license to copy, modify, and compile the Software
 * source code solely for use in connection with Cypress's
 * integrated circuit products.  Any reproduction, modification, translation,
 * compilation, or representation of this Software except as specified
 * above is prohibited without the express written permission of Cypress.
 *
 * Disclaimer: THIS SOFTWARE IS PROVIDED AS-IS, WITH NO WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING, BUT NOT LIMITED TO, NONINFRINGEMENT, IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE. Cypress
 * reserves the right to make changes to the Software without notice. Cypress
 * does not assume any liability arising out of the application or use of the
 * Software or any product or circuit described in the Software. Cypress does
 * not authorize its products for use in any products where a malfunction or
 * failure of the Cypress product may reasonably be expected to result in
 * significant property damage, injury or death ("High Risk Product"). By
 * including Cypress's product in a High Risk Product, the manufacturer
 * of such system or application assumes all risk of such use and in doing
 * so agrees to indemnify Cypress against all liability.
 */

/** @file
 *
 * This demo application shows an implementation of a motion sensor.
 * The app is based on the snip/mesh/mesh_sensor_server sample which
 * implements generic LE Mesh Sensor Server model.
 *
 * Features demonstrated
 * - Configuring and receiving interrupts from the PIR motion sensor
 * - Publishing motion data over LE mesh
 *
 * See chip specific readme.txt for more information about the Bluetooth SDK.
 *
 * To demonstrate the app, work through the following steps.
 * 1. Build and download the application to 213043 mesh kit.
 * 2. Use Android MeshController or Windows Mesh Client and provision the motion sensor
 * 3. After successful provisioning, user can use the Android MeshController/ Windows Mesh
 *    Client to configure the below parameters of the sensor:
 *         When sensor is provisioned, it is configured to publish data to a group, if current
 *         group was configured, or to "all-nodes", if there were no group. Modify publication
 *         to configuration to publish to "all-nodes".  Also set up the sensor to publish data
 *         with period 320000msec(5 minutes). The default configuration of the sensor is to
 *         publish data with publish period when presence is not detected. When presence is
 *         detected the period is divided by 32, i.e. presence detected message is sent every
 *         10 seconds. In addition to that the message will be sent as soon as if there were no
 *         presence for more than 10 seconds.
 * 4. Wave your hand in front of the CYBT-213043-MESH board to show some motion.
 */
#include "wiced_bt_uuid.h"
#include "wiced_bt_ble.h"
#include "wiced_bt_gatt.h"
#include "wiced_bt_mesh_models.h"
#include "wiced_bt_trace.h"
#include "wiced_timer.h"
#include "wiced_bt_mesh_app.h"
#include "wiced_hal_nvram.h"
#include "wiced_platform.h"
#include "e93196.h"
#include "wiced_sleep.h"
#include "wiced_bt_cfg.h"
#include "wiced_hal_mia.h"
#include "wiced_hal_mia.h"
#include "GeneratedSource/cycfg_pins.h"

extern wiced_bt_cfg_settings_t wiced_bt_cfg_settings;

/******************************************************
 *          Constants
 ******************************************************/
#define MESH_PID                0x3123
#define MESH_VID                0x0002

#define MESH_SENSOR_PROPERTY_ID                         WICED_BT_MESH_PROPERTY_PRESENCE_DETECTED
#define MESH_SENSOR_VALUE_LEN                           WICED_BT_MESH_PROPERTY_LEN_PRESENCE_DETECTED

#define MESH_MOTION_SENSOR_POSITIVE_TOLERANCE           WICED_BT_MESH_SENSOR_TOLERANCE_UNSPECIFIED
#define MESH_MOTION_SENSOR_NEGATIVE_TOLERANCE           WICED_BT_MESH_SENSOR_TOLERANCE_UNSPECIFIED

#define MESH_MOTION_SENSOR_SAMPLING_FUNCTION            WICED_BT_MESH_SENSOR_SAMPLING_FUNCTION_UNKNOWN
#define MESH_MOTION_SENSOR_MEASUREMENT_PERIOD           WICED_BT_MESH_SENSOR_VAL_UNKNOWN
#define MESH_MOTION_SENSOR_UPDATE_INTERVAL              WICED_BT_MESH_SENSOR_VAL_UNKNOWN

#define MESH_MOTION_SENSOR_CADENCE_VSID_START           WICED_NVRAM_VSID_START

// After presence is detected, interrupts are disabled for 7 seconds
#define MESH_PRESENCE_DETECTED_BLIND_TIME               7

/******************************************************
 *          Structures
 ******************************************************/
e93196_usr_cfg_t e93196_usr_cfg =
{
    .doci_pin           = CYBSP_INT_DOCI,                             /* Interrupt/Data output Clock input configure pin          */
    .serin_pin          = CYBSP_SERIN,                                /* Serial Input configure pin                               */
    .e93196_init_reg    =
    {
        .sensitivity    = 0x10,                                     /* [24:17]sensitivity,   [Register Value] * 6.5uV           */
        .blind_time     = MESH_PRESENCE_DETECTED_BLIND_TIME * 2,    /* [16:13]blind time,    [Register Value] * 0.5s, max is 8s */
        .pulse_cnt      = 0x01,                                     /* [12:11]pulse count                                       */
        .window_time    = 0x01,                                     /* [10:9]window time                                        */
        .move_dete_en   = 0x01,                                     /* [8]move detect enable                                    */
        .int_src        = 0x00,                                     /* [7]irq source                                            */
        .adc_filter     = 0x01,                                     /* [6:5]ADC filter                                          */
        .power_en       = 0x00,                                     /* [4]power enable                                          */
        .self_test_en   = 0x00,                                     /* [3]selftest                                              */
        .capa           = 0x00,                                     /* [2]selftest capacity                                     */
        .test_mode      = 0x00,                                     /* [1:0]reserved                                            */
    }
};

#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
typedef struct
{
    wiced_sleep_config_t   lpn_sleep_config;

// Device LPN state
#define MESH_LPN_STATE_NOT_IDLE   0
#define MESH_LPN_STATE_IDLE       1
    uint8_t                lpn_state;    // LPN state: IDLE or NOT_IDLE
} mesh_sensor_motion_t;

mesh_sensor_motion_t app_state = { 0 };
#endif

/******************************************************
 *          Function Prototypes
 ******************************************************/
static void         mesh_app_init(wiced_bool_t is_provisioned);
static wiced_bool_t mesh_app_notify_period_set(uint8_t element_idx, uint16_t company_id, uint16_t model_id, uint32_t period);

static void         mesh_sensor_server_restart_timer(wiced_bt_mesh_core_config_sensor_t *p_sensor);
static void         mesh_sensor_server_report_handler(uint16_t event, uint8_t element_idx, void *p_get_data, void *p_ref_data);
static void         mesh_sensor_server_config_change_handler(uint8_t element_idx, uint16_t event, void* p_data);
static void         mesh_sensor_server_process_cadence_changed(uint8_t element_idx, wiced_bt_mesh_sensor_cadence_status_data_t* p_data);
static void         mesh_sensor_server_process_setting_changed(uint8_t element_idx, wiced_bt_mesh_sensor_setting_status_data_t* p_data);
static void         mesh_sensor_publish_timer_callback(TIMER_PARAM_TYPE arg);
static void         e93196_int_proc(void *data, uint8_t port_pin);
static void         mesh_sensor_presence_detected_timer_callback(TIMER_PARAM_TYPE arg);
static void         mesh_sensor_publish(void);
static void         mesh_sensor_value_changed(wiced_bt_mesh_core_config_sensor_t* p_sensor);
static int32_t      mesh_sensor_get_current_value(void);
static void         mesh_app_factory_reset(void);


#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
void mesh_sensor_motion_lpn_sleep(uint32_t max_sleep_duration);
static uint32_t mesh_sensor_motion_sleep_poll(wiced_sleep_poll_type_t type);
#endif
/******************************************************
 *          Variables Definitions
 ******************************************************/
uint8_t mesh_mfr_name[WICED_BT_MESH_PROPERTY_LEN_DEVICE_MANUFACTURER_NAME]          = { 'C', 'y', 'p', 'r', 'e', 's', 's', 0 };
uint8_t mesh_model_num[WICED_BT_MESH_PROPERTY_LEN_DEVICE_MODEL_NUMBER]              = { '1', '2', '3', '4', 0, 0, 0, 0 };
uint8_t mesh_prop_fw_version[WICED_BT_MESH_PROPERTY_LEN_DEVICE_FIRMWARE_REVISION] =   { '0', '6', '.', '0', '2', '.', '0', '5' }; // this is overwritten during init
uint8_t mesh_system_id[8]                                                           = { 0xbb, 0xb8, 0xa1, 0x80, 0x5f, 0x9f, 0x91, 0x71 };

int32_t       mesh_sensor_sent_value = 0;          // Value that was sent, it can be different than pub_value due to GET
int32_t       mesh_sensor_pub_value;               // value that has been published
uint32_t      mesh_sensor_pub_time;                // time stamp when data was published
uint32_t      mesh_sensor_publish_period = 0;      // publish "no presence" every ~5 minutes, with fast cadence 32. This is reset to 0 after provisioning.  Set here for testing.
                                                   // we will publish "presence" every 10 seconds.
uint32_t      mesh_sensor_fast_publish_period = 0; // publish period in msec when values are outside of limit
wiced_timer_t mesh_sensor_cadence_timer;
wiced_timer_t mesh_sensor_presence_detected_timer;
wiced_bool_t  presence_detected = WICED_FALSE;
uint32_t      mesh_sensor_sleep_max_time = 0;       // motion sensor max sleep time. unit is ms.

// We define optional setting for the motion sensor, the Motion Threshold. Default is 80%.
uint8_t       mesh_motion_sensor_threshold_val = 0x50;

wiced_bt_mesh_core_config_model_t mesh_element1_models[] =
{
    WICED_BT_MESH_DEVICE,
    WICED_BT_MESH_MODEL_SENSOR_SERVER,
};
#define MESH_APP_NUM_MODELS  (sizeof(mesh_element1_models) / sizeof(wiced_bt_mesh_core_config_model_t))

wiced_bt_mesh_core_config_sensor_t mesh_element1_sensors[] =
{
    {
        .property_id    = MESH_SENSOR_PROPERTY_ID,
        .prop_value_len = MESH_SENSOR_VALUE_LEN,
        .descriptor =
        {
            .positive_tolerance = MESH_MOTION_SENSOR_POSITIVE_TOLERANCE,
            .negative_tolerance = MESH_MOTION_SENSOR_NEGATIVE_TOLERANCE,
            .sampling_function  = MESH_MOTION_SENSOR_SAMPLING_FUNCTION,
            .measurement_period = MESH_MOTION_SENSOR_MEASUREMENT_PERIOD,
            .update_interval    = MESH_MOTION_SENSOR_UPDATE_INTERVAL,
        },
        .data = (uint8_t*)&mesh_sensor_sent_value,
        .cadence =
        {
            // Value 0 indicates that cadence does not change depending on the measurements
            .fast_cadence_period_divisor = 1,           // Recommended publish period is 320sec, 32 will make fast period 10sec
            .trigger_type_percentage     = WICED_FALSE, // The Property is Bool, does not make sense to use percentage
            .trigger_delta_down          = 0,           // This will not cause message when presence changes from 1 to 0
            .trigger_delta_up            = 0,           // This will cause immediate message when presence changes from 0 to 1
            .min_interval                = (1 << 10),   // Milliseconds. Conversion to SPEC values is done by the mesh models library
            .fast_cadence_low            = 0,           // If fast_cadence_low is greater than fast_cadence_high and the measured value is either is lower
                                                        // than fast_cadence_high or higher than fast_cadence_low, then the message shall be published
                                                        // with publish period (equals to mesh_sensor_publish_period divided by fast_cadence_divisor_period)
            .fast_cadence_high           = 0,           // is more or equal cadence_low or less then cadence_high. This is what we need.
        },
        .num_series     = 0,
        .series_columns = NULL,
        .num_settings   = 0,
        .settings       = NULL,
    },
};


#define MESH_APP_NUM_PROPERTIES (sizeof(mesh_element1_properties) / sizeof(wiced_bt_mesh_core_config_property_t))

#define MESH_SENSOR_SERVER_ELEMENT_INDEX    0
#define MESH_MOTION_SENSOR_INDEX            0

wiced_bt_mesh_core_config_element_t mesh_elements[] =
{
    {
        .location = MESH_ELEM_LOC_MAIN,                                  // location description as defined in the GATT Bluetooth Namespace Descriptors section of the Bluetooth SIG Assigned Numbers
        .default_transition_time = MESH_DEFAULT_TRANSITION_TIME_IN_MS,   // Default transition time for models of the element in milliseconds
        .onpowerup_state = WICED_BT_MESH_ON_POWER_UP_STATE_RESTORE,      // Default element behavior on power up
        .default_level = 0,                                              // Default value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .range_min = 1,                                                  // Minimum value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .range_max = 0xffff,                                             // Maximum value of the variable controlled on this element (for example power, lightness, temperature, hue...)
        .move_rollover = 0,                                              // If true when level gets to range_max during move operation, it switches to min, otherwise move stops.
        .properties_num = 0,                                             // Number of properties in the array models
        .properties = NULL,                                              // Array of properties in the element.
        .sensors_num = 1,                                                // Number of properties in the array models
        .sensors = mesh_element1_sensors,                                // Array of properties in the element.
        .models_num = MESH_APP_NUM_MODELS,                               // Number of models in the array models
        .models = mesh_element1_models,                                  // Array of models located in that element. Model data is defined by structure wiced_bt_mesh_core_config_model_t
    },
};

wiced_bt_mesh_core_config_t  mesh_config =
{
    .company_id         = MESH_COMPANY_ID_CYPRESS,                  // Company identifier assigned by the Bluetooth SIG
    .product_id         = MESH_PID,                                 // Vendor-assigned product identifier
    .vendor_id          = MESH_VID,                                 // Vendor-assigned product version identifier
#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    .features           = WICED_BT_MESH_CORE_FEATURE_BIT_LOW_POWER, // A bit field indicating the device features. In Low Power mode no Relay, no Proxy and no Friend
    .friend_cfg         =                                           // Empty Configuration of the Friend Feature
    {
        .receive_window = 0,                                        // Receive Window value in milliseconds supported by the Friend node.
        .cache_buf_len  = 0,                                        // Length of the buffer for the cache
        .max_lpn_num    = 0                                         // Max number of Low Power Nodes with established friendship. Must be > 0 if Friend feature is supported.
    },
    .low_power          =                                           // Configuration of the Low Power Feature
    {
        .rssi_factor           = 2,                                 // contribution of the RSSI measured by the Friend node used in Friend Offer Delay calculations.
        .receive_window_factor = 2,                                 // contribution of the supported Receive Window used in Friend Offer Delay calculations.
        .min_cache_size_log    = 3,                                 // minimum number of messages that the Friend node can store in its Friend Cache.
        .receive_delay         = 100,                               // Receive delay in 1 ms units to be requested by the Low Power node.
        .poll_timeout          = 36000                              // Poll timeout in 100ms units to be requested by the Low Power node.
    },
#else
    .features           = WICED_BT_MESH_CORE_FEATURE_BIT_FRIEND | WICED_BT_MESH_CORE_FEATURE_BIT_RELAY | WICED_BT_MESH_CORE_FEATURE_BIT_GATT_PROXY_SERVER,   // In Friend mode support friend, relay
    .friend_cfg         =                                           // Configuration of the Friend Feature(Receive Window in Ms, messages cache)
    {
        .receive_window        = 20,
        .cache_buf_len         = 300,                               // Length of the buffer for the cache
        .max_lpn_num           = 4                                  // Max number of Low Power Nodes with established friendship. Must be > 0 if Friend feature is supported.
    },
    .low_power          =                                           // Configuration of the Low Power Feature
    {
        .rssi_factor           = 0,                                 // contribution of the RSSI measured by the Friend node used in Friend Offer Delay calculations.
        .receive_window_factor = 0,                                 // contribution of the supported Receive Window used in Friend Offer Delay calculations.
        .min_cache_size_log    = 0,                                 // minimum number of messages that the Friend node can store in its Friend Cache.
        .receive_delay         = 0,                                 // Receive delay in 1 ms units to be requested by the Low Power node.
        .poll_timeout          = 0                                  // Poll timeout in 100ms units to be requested by the Low Power node.
    },
#endif
    .gatt_client_only          = WICED_FALSE,                       // Can connect to mesh over GATT or ADV
    .elements_num  = (uint8_t)(sizeof(mesh_elements) / sizeof(mesh_elements[0])),   // number of elements on this device
    .elements      = mesh_elements                                  // Array of elements for this device
};

/*
 * Mesh application library will call into application functions if provided by the application.
 */
wiced_bt_mesh_app_func_table_t wiced_bt_mesh_app_func_table =
{
    mesh_app_init,                  // application initialization
    NULL,                           // Default SDK platform button processing
    NULL,                           // GATT connection status
    NULL,                           // attention processing
    mesh_app_notify_period_set,     // notify period set
    NULL,                           // WICED HCI command
#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    mesh_sensor_motion_lpn_sleep,   // LPN sleep
#else
    NULL,
#endif
    mesh_app_factory_reset          // factory reset
};

wiced_bool_t do_not_init_again = WICED_FALSE;

 /******************************************************
 *               Function Definitions
 ******************************************************/
void mesh_app_init(wiced_bool_t is_provisioned)
{
#if 0
    // Set Debug trace level for mesh_models_lib and mesh_provisioner_lib
    wiced_bt_mesh_models_set_trace_level(WICED_BT_MESH_CORE_TRACE_INFO);
#endif
#if 0
    // Set Debug trace level for all modules but Info level for CORE_AES_CCM module
    wiced_bt_mesh_core_set_trace_level(WICED_BT_MESH_CORE_TRACE_FID_ALL, WICED_BT_MESH_CORE_TRACE_DEBUG);
    wiced_bt_mesh_core_set_trace_level(WICED_BT_MESH_CORE_TRACE_FID_CORE_AES_CCM, WICED_BT_MESH_CORE_TRACE_INFO);
#endif
    wiced_result_t result;
    wiced_bt_mesh_core_config_sensor_t *p_sensor;

    // This means that device came out of HID off mode and it is not a power cycle
    if(wiced_hal_mia_is_reset_reason_por())
    {
        WICED_BT_TRACE("start reason: reset\n");
    }
    else
    {
#if CYW20819A1
        if(wiced_hal_mia_is_reset_reason_hid_timeout())
        {
            WICED_BT_TRACE("Wake from HID off: timed wake\n");
        }
        else
#endif
        {
            // Check if we wake up by GPIO
            WICED_BT_TRACE("Wake from HID off, interrupt:%d\n", wiced_hal_gpio_get_pin_interrupt_status(e93196_usr_cfg.doci_pin));
        }
    }

#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    wiced_bt_cfg_settings.device_name = (uint8_t *)"Motion Sensor LPN";
#else
    wiced_bt_cfg_settings.device_name = (uint8_t *)"Motion Sensor";
#endif

    wiced_bt_cfg_settings.gatt_cfg.appearance = APPEARANCE_SENSOR_MOTION;

    mesh_prop_fw_version[0] = 0x30 + (WICED_SDK_MAJOR_VER / 10);
    mesh_prop_fw_version[1] = 0x30 + (WICED_SDK_MAJOR_VER % 10);
    mesh_prop_fw_version[2] = 0x30 + (WICED_SDK_MINOR_VER / 10);
    mesh_prop_fw_version[3] = 0x30 + (WICED_SDK_MINOR_VER % 10);
    mesh_prop_fw_version[4] = 0x30 + (WICED_SDK_REV_NUMBER / 10);
    mesh_prop_fw_version[5] = 0x30 + (WICED_SDK_REV_NUMBER % 10);
    // convert 12 bits of BUILD_NUMMBER to two base64 characters big endian
    mesh_prop_fw_version[6] = wiced_bt_mesh_base64_encode_6bits((uint8_t)(WICED_SDK_BUILD_NUMBER >> 6) & 0x3f);
    mesh_prop_fw_version[7] = wiced_bt_mesh_base64_encode_6bits((uint8_t)WICED_SDK_BUILD_NUMBER & 0x3f);

    // Adv Data is fixed. Spec allows to put URI, Name, Appearance and Tx Power in the Scan Response Data.
    if (!is_provisioned)
    {
        wiced_bt_ble_advert_elem_t  adv_elem[3];
        uint8_t                     buf[2];
        uint8_t                     num_elem = 0;

        adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_NAME_COMPLETE;
        adv_elem[num_elem].len         = (uint16_t)strlen((const char*)wiced_bt_cfg_settings.device_name);
        adv_elem[num_elem].p_data      = wiced_bt_cfg_settings.device_name;
        num_elem++;

        adv_elem[num_elem].advert_type = BTM_BLE_ADVERT_TYPE_APPEARANCE;
        adv_elem[num_elem].len         = 2;
        buf[0]                         = (uint8_t)wiced_bt_cfg_settings.gatt_cfg.appearance;
        buf[1]                         = (uint8_t)(wiced_bt_cfg_settings.gatt_cfg.appearance >> 8);
        adv_elem[num_elem].p_data      = buf;
        num_elem++;

        wiced_bt_mesh_set_raw_scan_response_data(num_elem, adv_elem);

        wiced_bt_mesh_model_sensor_server_init(MESH_SENSOR_SERVER_ELEMENT_INDEX, mesh_sensor_server_report_handler, mesh_sensor_server_config_change_handler, is_provisioned);
        return;
    }

    p_sensor = &mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_MOTION_SENSOR_INDEX];

    e93196_init(&e93196_usr_cfg, e93196_int_proc, NULL);

    // initialize the cadence timer.  Need a timer for each element because each sensor model can be
    // configured for different publication period.  This app has only one sensor.
    wiced_init_timer(&mesh_sensor_cadence_timer, &mesh_sensor_publish_timer_callback, (TIMER_PARAM_TYPE)&mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_MOTION_SENSOR_INDEX], WICED_MILLI_SECONDS_TIMER);

    wiced_init_timer(&mesh_sensor_presence_detected_timer, mesh_sensor_presence_detected_timer_callback, (TIMER_PARAM_TYPE)&mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_MOTION_SENSOR_INDEX], WICED_SECONDS_TIMER);

    //restore the cadence from NVRAM
    wiced_hal_read_nvram( MESH_MOTION_SENSOR_CADENCE_VSID_START, sizeof(wiced_bt_mesh_sensor_config_cadence_t), (uint8_t*)(&p_sensor->cadence), &result);

    wiced_bt_mesh_model_sensor_server_init(MESH_SENSOR_SERVER_ELEMENT_INDEX, mesh_sensor_server_report_handler, mesh_sensor_server_config_change_handler, is_provisioned);

#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
    if (!do_not_init_again)
    {
        WICED_BT_TRACE("Init once \n");

        // Configure to sleep as the device is idle now
        app_state.lpn_sleep_config.sleep_mode = WICED_SLEEP_MODE_NO_TRANSPORT;
        app_state.lpn_sleep_config.device_wake_mode = WICED_GPIO_BUTTON_WAKE_MODE;
        app_state.lpn_sleep_config.device_wake_source = WICED_SLEEP_WAKE_SOURCE_GPIO;
        app_state.lpn_sleep_config.device_wake_gpio_num = e93196_usr_cfg.doci_pin;
        app_state.lpn_sleep_config.host_wake_mode = WICED_SLEEP_WAKE_ACTIVE_HIGH;
        app_state.lpn_sleep_config.sleep_permit_handler = mesh_sensor_motion_sleep_poll;
#if defined(CYW20819A1) || defined(CYW20820A1)
        app_state.lpn_sleep_config.post_sleep_cback_handler = NULL;
#endif

        if (WICED_BT_SUCCESS != wiced_sleep_configure(&app_state.lpn_sleep_config))
        {
            WICED_BT_TRACE("Sleep Configure failed\r\n");
        }

        do_not_init_again = WICED_TRUE;
    }
#endif
}

/*
 * New publication period is set. If it is for the sensor model, this application should take care of it.
 * The period may need to be adjusted based on the divisor.
 */
wiced_bool_t mesh_app_notify_period_set(uint8_t element_idx, uint16_t company_id, uint16_t model_id, uint32_t period)
{
    if ((element_idx != MESH_MOTION_SENSOR_INDEX) || (company_id != MESH_COMPANY_ID_BT_SIG) || (model_id != WICED_BT_MESH_CORE_MODEL_ID_SENSOR_SRV))
    {
        return WICED_FALSE;
    }
    mesh_sensor_publish_period = period;
    WICED_BT_TRACE("Sensor data send period:%dms\n", mesh_sensor_publish_period);
    mesh_sensor_server_restart_timer(&mesh_config.elements[element_idx].sensors[MESH_MOTION_SENSOR_INDEX]);

    // as we are restarting time, we will publish on the first expiration regardless on when the value was previously published
    mesh_sensor_pub_time = 0;
    return WICED_TRUE;
}

/*
 * Start periodic timer depending on the publication period, fast cadence divisor and minimum interval
 */
void mesh_sensor_server_restart_timer(wiced_bt_mesh_core_config_sensor_t *p_sensor)
{
    // If there are no specific cadence settings, publish every publish period.
    uint32_t timeout = mesh_sensor_publish_period;

    wiced_stop_timer(&mesh_sensor_cadence_timer);
    if (timeout == 0)
    {
        WICED_BT_TRACE("sensor restart timer period:%d\n", mesh_sensor_publish_period);
        return;
    }
    // If fast cadence period divisor is set, we need to check data more
    // often than publication period.  Publish if measurement is in specified range
    if (p_sensor->cadence.fast_cadence_period_divisor > 1)
    {
        timeout = mesh_sensor_publish_period / p_sensor->cadence.fast_cadence_period_divisor;
        mesh_sensor_fast_publish_period = timeout ;
        WICED_BT_TRACE("sensor fast cadence:%d\n", mesh_sensor_fast_publish_period);
    }
    else
    {
        mesh_sensor_fast_publish_period = 0;
        WICED_BT_TRACE("sensor fast pub period:0 cadence devisor:%d\n", p_sensor->cadence.fast_cadence_period_divisor);
    }
    // should not send data more often than min_interval
    if ((p_sensor->cadence.min_interval != 0) && (p_sensor->cadence.min_interval > timeout) &&
        ((p_sensor->cadence.trigger_delta_up != 0) || (p_sensor->cadence.trigger_delta_down != 0)))
    {
        timeout = p_sensor->cadence.min_interval;
        WICED_BT_TRACE("sensor min interval:%d\n", timeout);
    }
    WICED_BT_TRACE("sensor restart timer:%d\n", timeout);
    mesh_sensor_sleep_max_time = timeout;
    wiced_start_timer(&mesh_sensor_cadence_timer, timeout);
}

/*
 * Process the configuration changes set by the Sensor Client.
 */
void mesh_sensor_server_config_change_handler(uint8_t element_idx, uint16_t event, void *p_data)
{
    WICED_BT_TRACE("mesh_sensor_server_config_change_handler msg: %d\n", event);

    switch (event)
    {
    case WICED_BT_MESH_SENSOR_CADENCE_STATUS:
        mesh_sensor_server_process_cadence_changed(element_idx, (wiced_bt_mesh_sensor_cadence_status_data_t*) p_data);
        break;

    case WICED_BT_MESH_SENSOR_SETTING_STATUS:
        mesh_sensor_server_process_setting_changed(element_idx, (wiced_bt_mesh_sensor_setting_status_data_t*) p_data);
        break;
    }
}

/*
 * Process get request from Sensor Client and respond with sensor data
 */
void mesh_sensor_server_report_handler(uint16_t event, uint8_t element_idx, void *p_get, void *p_ref_data)
{
    wiced_bt_mesh_sensor_get_t *p_sensor_get = (wiced_bt_mesh_sensor_get_t *)p_get;
    WICED_BT_TRACE("mesh_sensor_server_report_handler msg: %d\n", event);

    switch (event)
    {
    case WICED_BT_MESH_SENSOR_GET:
        // tell mesh models library that data is ready to be shipped out, the library will get data from mesh_config
        mesh_sensor_sent_value = presence_detected;
        wiced_bt_mesh_model_sensor_server_data(element_idx, p_sensor_get->property_id, p_ref_data);
        break;

    case WICED_BT_MESH_SENSOR_COLUMN_GET:
        break;

    case WICED_BT_MESH_SENSOR_SERIES_GET:
        break;

    default:
        WICED_BT_TRACE("unknown\n");
        break;
    }
}

/*
 * Process cadence change
 */
void mesh_sensor_server_process_cadence_changed(uint8_t element_idx, wiced_bt_mesh_sensor_cadence_status_data_t* p_data)
{
    wiced_bt_mesh_core_config_sensor_t *p_sensor;
    uint8_t written_byte = 0;
    wiced_result_t status;
    p_sensor = &mesh_config.elements[element_idx].sensors[MESH_MOTION_SENSOR_INDEX];

    WICED_BT_TRACE("cadence changed property id:%04x\n", p_data->property_id);
    WICED_BT_TRACE("Fast cadence period divisor:%d\n", p_sensor->cadence.fast_cadence_period_divisor);
    WICED_BT_TRACE("Is trigger type percent:%d\n", p_sensor->cadence.trigger_type_percentage);
    WICED_BT_TRACE("Trigger delta up:%d\n", p_sensor->cadence.trigger_delta_up);
    WICED_BT_TRACE("Trigger delta down:%d\n", p_sensor->cadence.trigger_delta_down);
    WICED_BT_TRACE("Min Interval:%d\n", p_sensor->cadence.min_interval);
    WICED_BT_TRACE("Fast cadence low:%d\n", p_sensor->cadence.fast_cadence_low);
    WICED_BT_TRACE("Fast cadence high:%d\n", p_sensor->cadence.fast_cadence_high);

    /* save cadence to NVRAM */
    written_byte = wiced_hal_write_nvram( MESH_MOTION_SENSOR_CADENCE_VSID_START, sizeof(wiced_bt_mesh_sensor_config_cadence_t), (uint8_t*)(&p_sensor->cadence), &status);
    WICED_BT_TRACE("NVRAM write: %d\n", written_byte);

    mesh_sensor_server_restart_timer(p_sensor);

    // as we are restarting time, we will publish on the first expiration regardless on when the value was previously published
    mesh_sensor_pub_time = 0;
}

/*
 * Publication timer callback.  Need to send data if publish period expired, or
 * if value has changed more than specified in the triggers, or if value is in range
 * of fast cadence values and fast cadence interval expired.
 */
void mesh_sensor_publish_timer_callback(TIMER_PARAM_TYPE arg)
{
    wiced_bt_mesh_event_t *p_event;
    wiced_bt_mesh_core_config_sensor_t *p_sensor = (wiced_bt_mesh_core_config_sensor_t *)arg;
    wiced_bool_t pub_needed = WICED_FALSE;
    uint32_t current_time = wiced_bt_mesh_core_get_tick_count();
    int32_t current_value = mesh_sensor_get_current_value();

    if ((p_sensor->cadence.min_interval != 0) && ((current_time - mesh_sensor_pub_time) < p_sensor->cadence.min_interval))
    {
        WICED_BT_TRACE("time since last pub:%d less then cadence interval:%d\n", current_time - mesh_sensor_pub_time, p_sensor->cadence.min_interval);
    }
    else
    {
        // check if publication timer expired
        if ((mesh_sensor_publish_period != 0) && (current_time - mesh_sensor_pub_time >= mesh_sensor_publish_period))
        {
            WICED_BT_TRACE("Pub needed period\n");
            pub_needed = WICED_TRUE;
        }
        // still need to send if publication timer has not expired, but triggers are configured, and value
        // changed too much
        if (!pub_needed && ((p_sensor->cadence.trigger_delta_up != 0) || (p_sensor->cadence.trigger_delta_down != 0)))
        {
            if (!p_sensor->cadence.trigger_type_percentage)
            {
                WICED_BT_TRACE("Native cur value:%d sent:%d delta:%d/%d\n",
                        current_value, mesh_sensor_pub_value, p_sensor->cadence.trigger_delta_up, p_sensor->cadence.trigger_delta_down);

                if (((p_sensor->cadence.trigger_delta_up != 0)   && (current_value >= (mesh_sensor_pub_value + p_sensor->cadence.trigger_delta_up)))
                 || ((p_sensor->cadence.trigger_delta_down != 0) && (current_value <= (mesh_sensor_pub_value - p_sensor->cadence.trigger_delta_down))))
                {
                    WICED_BT_TRACE("Pub needed native value\n");
                    pub_needed = WICED_TRUE;
                }
            }
            else
            {
                // need to calculate percentage of the increase or decrease.  The deltas are in 0.01%.
                if ((p_sensor->cadence.trigger_delta_up != 0) && (current_value > mesh_sensor_pub_value))
                {
                    WICED_BT_TRACE("Delta up:%d\n", ((uint32_t)(current_value - mesh_sensor_pub_value) * 10000 / current_value));
                    if (((uint32_t)(current_value - mesh_sensor_pub_value) * 10000 / current_value) > p_sensor->cadence.trigger_delta_up)
                    {
                        WICED_BT_TRACE("Pub needed percent delta up:%d\n", ((current_value - mesh_sensor_pub_value) * 10000 / current_value));
                        pub_needed = WICED_TRUE;
                    }
                }
                else if ((p_sensor->cadence.trigger_delta_down != 0) && (current_value < mesh_sensor_pub_value))
                {
                    WICED_BT_TRACE("Delta down:%d\n", ((uint32_t)(mesh_sensor_pub_value - current_value) * 10000 / current_value));
                    if (((uint32_t)(mesh_sensor_pub_value - current_value) * 10000 / current_value) > p_sensor->cadence.trigger_delta_down)
                    {
                        WICED_BT_TRACE("Pub needed percent delta down:%d\n", ((mesh_sensor_pub_value - current_value) * 10000 / current_value));
                        pub_needed = WICED_TRUE;
                    }
                }
            }
        }
        // may still need to send if fast publication is configured
        if (!pub_needed && (mesh_sensor_fast_publish_period != 0))
        {
            // check if fast publish period expired
            if (current_time - mesh_sensor_pub_time >= mesh_sensor_fast_publish_period)
            {
                // if cadence high is more than cadence low, to publish, the value should be in range
                if (p_sensor->cadence.fast_cadence_high > p_sensor->cadence.fast_cadence_low)
                {
                    if ((current_value > p_sensor->cadence.fast_cadence_low) &&
                        (current_value <= p_sensor->cadence.fast_cadence_high))
                    {
                        WICED_BT_TRACE("Pub needed in range\n");
                        pub_needed = WICED_TRUE;
                    }
                }
                else if (p_sensor->cadence.fast_cadence_high < p_sensor->cadence.fast_cadence_low)
                {
                    if ((current_value >= p_sensor->cadence.fast_cadence_low) ||
                        (current_value < p_sensor->cadence.fast_cadence_high))
                    {
                        WICED_BT_TRACE("Pub needed out of range\n");
                        pub_needed = WICED_TRUE;
                    }
                }
                else // p_sensor->cadence.fast_cadence_high == p_sensor->cadence.fast_cadence_low
                {
                    // publish if current value is the same as cadence high/low
                    if (current_value == p_sensor->cadence.fast_cadence_low)
                    {
                        WICED_BT_TRACE("Pub needed equal\n");
                        pub_needed = WICED_TRUE;
                    }
                }
            }
        }
        if (pub_needed)
        {
            mesh_sensor_publish();
        }
    }
    mesh_sensor_server_restart_timer(p_sensor);
}

/*
 * Process setting change
 */
void mesh_sensor_server_process_setting_changed(uint8_t element_idx, wiced_bt_mesh_sensor_setting_status_data_t* p_data)
{
    WICED_BT_TRACE("settings changed sensor, prop_id:%x, setting prop_id:%x\n", p_data->property_id, p_data->setting.setting_property_id);

}

void e93196_int_proc(void* data, uint8_t port_pin)
{
    WICED_BT_TRACE("presence detected TRUE\n");
    e93196_int_clean(port_pin);

    // We disable interrupts for MESH_PRESENCE_DETECTED_BLIND_TIME.  If interrupt does not happen within
    // MESH_PRESENCE_DETECTED_BLIND_TIME * 2, we assume that there is no presence anymore
    wiced_start_timer(&mesh_sensor_presence_detected_timer, 2 * MESH_PRESENCE_DETECTED_BLIND_TIME);

    if (!presence_detected)
    {
        presence_detected = WICED_TRUE;
        mesh_sensor_value_changed(&mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_MOTION_SENSOR_INDEX]);
    }
}

void mesh_sensor_presence_detected_timer_callback(TIMER_PARAM_TYPE arg)
{
    WICED_BT_TRACE("presence detected FALSE\n");

    if (presence_detected)
    {
        presence_detected = WICED_FALSE;
        mesh_sensor_value_changed(&mesh_config.elements[MESH_SENSOR_SERVER_ELEMENT_INDEX].sensors[MESH_MOTION_SENSOR_INDEX]);
    }
}

/*
 * This funciton is executed when Sensor Value changes
 */
void mesh_sensor_value_changed(wiced_bt_mesh_core_config_sensor_t* p_sensor)
{
    int32_t current_value;
    uint32_t current_time;

    // If sensor is configured for periodic publication, don't need to do anything because
    // value will be published on schedule
    if (mesh_sensor_publish_period != 0)
    {
        WICED_BT_TRACE("sensor value change ignored will publish on timeout\n");
        return;
    }

    // When periodic publishing is disabled, however, the behavior triggered by a change in
    // the Sensor Data state shall depend on whether the Sensor Cadence state has been configured
    if ((p_sensor->cadence.fast_cadence_period_divisor == 1) && (p_sensor->cadence.trigger_delta_up == 0) && (p_sensor->cadence.trigger_delta_down == 0))
    {
        // If Cadence is not configured we should publish on every change. Implementation needs to make sure that
        // the value is not published too often, but in Motion Sensor it is not a problem because there is a blind timer involved.
        mesh_sensor_publish();
        return;
    }

    current_time = wiced_bt_mesh_core_get_tick_count();
    if (mesh_sensor_pub_time + p_sensor->cadence.min_interval > current_time)
    {
        WICED_BT_TRACE("sensor value change min_interval not expired pub_time:%d current_time:%d\n", mesh_sensor_pub_time, current_time);
        return;
    }

    // If cadence is configured, we will publish if conditions are setisifed
    current_value = mesh_sensor_get_current_value();

    if (((p_sensor->cadence.trigger_delta_down != 0) && (current_value < mesh_sensor_pub_value - p_sensor->cadence.trigger_delta_down)) ||
        ((p_sensor->cadence.trigger_delta_up != 0) && (current_value > mesh_sensor_pub_value - p_sensor->cadence.trigger_delta_up)))
    {
        mesh_sensor_publish();
        mesh_sensor_server_restart_timer(p_sensor);
        return;
    }
}

/*
 * Publish Sensor Data
 */
void mesh_sensor_publish(void)
{
    mesh_sensor_sent_value = mesh_sensor_get_current_value();
    mesh_sensor_pub_value = mesh_sensor_sent_value;
    mesh_sensor_pub_time = wiced_bt_mesh_core_get_tick_count();

    WICED_BT_TRACE("*** Pub value:%d time:%d\n", mesh_sensor_sent_value, mesh_sensor_pub_time);
    wiced_bt_mesh_model_sensor_server_data(MESH_SENSOR_SERVER_ELEMENT_INDEX, MESH_SENSOR_PROPERTY_ID, NULL);
}

int32_t mesh_sensor_get_current_value(void)
{
    return presence_detected;
}

/*
 * Application is notified that factory reset is executed.
 */
void mesh_app_factory_reset(void)
{
    wiced_hal_delete_nvram(MESH_MOTION_SENSOR_CADENCE_VSID_START, NULL);
}

#if defined(LOW_POWER_NODE) && (LOW_POWER_NODE == 1)
void mesh_sensor_motion_lpn_sleep(uint32_t max_sleep_duration)
{
    WICED_BT_TRACE("Mesh core allow max_sleep_duration:%ds configured:%ds presence:%d\n", max_sleep_duration / 1000, mesh_sensor_sleep_max_time / 1000, presence_detected);

    // Currently cannot sleep for more than a minute. It's for better demo.
    if (max_sleep_duration > 60000)
        max_sleep_duration = 60000;

    if (mesh_sensor_sleep_max_time != 0)
    {
        // If presence is detected we cannot sleep for more than configured period.
        // Otherwise we can sleep until need to send the next LPN poll
        if (presence_detected && (mesh_sensor_sleep_max_time < max_sleep_duration))
            max_sleep_duration = mesh_sensor_sleep_max_time;
    }

    // We think if sleep timer is bigger than 30mins, then hid-off will save more power. But it's up to your design.
    if (max_sleep_duration < 1800000)// 30mins
    {
        WICED_BT_TRACE("Get ready to go into ePDS sleep, duration=%d\n\r", max_sleep_duration);
        app_state.lpn_state = MESH_LPN_STATE_IDLE;
    }
    else
    {
        WICED_BT_TRACE("Get ready to go into HID-OFF, duration=%d\n\r", max_sleep_duration);
        wiced_sleep_enter_hid_off(max_sleep_duration, e93196_usr_cfg.doci_pin, WICED_GPIO_ACTIVE_HIGH);
        WICED_BT_TRACE("Entering HID-Off failed\n\r");
    }
}


/*
 * Sleep permission polling time to be used by firmware
 */
static uint32_t mesh_sensor_motion_sleep_poll(wiced_sleep_poll_type_t type)
{
    uint32_t ret = WICED_SLEEP_NOT_ALLOWED;

    switch (type)
	{
    case WICED_SLEEP_POLL_TIME_TO_SLEEP:
        if (app_state.lpn_state == MESH_LPN_STATE_NOT_IDLE)
        {
            WICED_BT_TRACE("!");
            ret = WICED_SLEEP_NOT_ALLOWED;
        }
        else
        {
            WICED_BT_TRACE("@\n");
            ret = WICED_SLEEP_MAX_TIME_TO_SLEEP;
        }
        break;
    case WICED_SLEEP_POLL_SLEEP_PERMISSION:
        if (app_state.lpn_state == MESH_LPN_STATE_IDLE)
        {
            WICED_BT_TRACE("#\n");
            ret = WICED_SLEEP_ALLOWED_WITHOUT_SHUTDOWN;
        }

        break;
    }
    return ret;
}
#endif
