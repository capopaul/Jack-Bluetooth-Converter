// Author : Paul Capgras
// Date   : Oct 10, 2025

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>

/* log tag */
#define BT_APP_CORE_TAG "BT_APP_CORE"

/* signal for `bt_app_work_dispatch` */
#define BT_APP_SIG_WORK_DISPATCH (0x01)

/**
 * @brief  handler for the dispatched work
 *
 * @param [in] event  event id
 * @param [in] param  handler parameter
 */
typedef void (*bt_app_cb_t)(uint16_t event, void *param);

/* message to be sent */
typedef struct
{
    uint16_t sig;   /*!< signal to bt_app_task */
    uint16_t event; /*!< message event id */
    bt_app_cb_t cb; /*!< context switch callback */
    void *param;    /*!< parameter area needs to be last */
} bt_app_msg_t;

/**
 * @brief  parameter deep-copy function to be customized
 *
 * @param [out] p_dest  pointer to destination data
 * @param [in]  p_src   pointer to source data
 * @param [in]  len     data length in byte
 */
typedef void (*bt_app_copy_cb_t)(void *p_dest, void *p_src, int len);

/* event for init */
enum
{
    BT_APP_EVT_REGISTER_CB_FUNCTION = 0,
};

/********************************
 * EXTERNAL FUNCTION DECLARATIONS
 *******************************/

void bt_app_init(void);
void bt_app_task_start_up(void);
void bt_app_task_shut_down(void);

bool bt_app_work_dispatch(bt_app_cb_t p_cback, uint16_t event, void *p_params, int param_len, bt_app_copy_cb_t p_copy_cback);

void bt_app_register_callback_function(uint16_t event, void *p_param);