/*
 * Copyright (c) 2006-2018, ZINGTO UAV
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2020-03-04     serni        first version
 */

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include <stdlib.h>

#include "guardian.h"

#define DBG_ENABLE
#define DBG_SECTION_NAME "PanTilt"
#define DBG_LEVEL DBG_INFO       // DBG_INFO
#define DBG_COLOR
#include <rtdbg.h>

#pragma pack(1)
typedef struct __PTZ_SendPacket
{
    rt_uint32_t     HEADER;             // 4
    rt_int16_t      roll;               // 6  #1
    rt_int16_t      pitch;              // 8  #2
    rt_uint16_t     __reserved1;        // 10 #3
    rt_int16_t      yaw;                // 12 #4
    rt_uint16_t     mode;               // 14 #5
    rt_uint16_t     __reserved2;        // 16 #6
    rt_uint16_t     __reserved3;        // 18 #7
    rt_uint16_t     homing;             // 20 #8
    rt_uint16_t     __reserved4;        // 22
    rt_uint8_t      __reserved5[68-22]; // 68
    rt_uint8_t      checksum;           // 69
}ptz_serialctrlpkt;
#pragma pack(4)

const float ZOOM2RATIO[30] = {  1.0f,   0.6f,   0.5f,   0.4f,   0.35f,   0.3f,
                                0.3f,   0.3f,   0.3f,   0.25f,  0.25f,  0.25f,
                                0.2f,   0.2f,   0.2f,   0.15f,  0.15f,  0.15f,
                                0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.1f,
                                0.1f,   0.1f,   0.1f,   0.1f,   0.1f,   0.1f
                             };
const float IR2RATIO = 0.2f;

//#define IRSENSOR_COLOR_PKT_SIZE (9)                    

//rt_uint8_t irs_serialctrlpkt[IRSENSOR_COLOR_PKT_SIZE] = {0xAA, 0x05, 0x01, 0x42, 0x02, 0x00, 0xF4, 0xEB, 0xAA};
                             
#define IRSENSOR_FRAME_SIZE (10)

#define IRSENSOR_COLOR_HEADER   (0x1168)
rt_uint8_t irs_control_frame[IRSENSOR_FRAME_SIZE];

                             
rt_uint8_t irs_color[4][IRSENSOR_FRAME_SIZE] = {
    {0x68, 0x11, 0x02, 0x01, 0x00, 0x00, 0x87, 0x00, 0x01, 0x0A},   // gray
    {0x68, 0x11, 0x02, 0x01, 0x00, 0x00, 0x88, 0x00, 0x01, 0x0B},   // reverse-gray
    {0x68, 0x11, 0x02, 0x01, 0x00, 0x00, 0x7D, 0x00, 0x01, 0x00},   // rainbow_#1
    {0x68, 0x11, 0x02, 0x01, 0x00, 0x00, 0x85, 0x00, 0x01, 0x08}    // hotmetal_#1
};

rt_uint8_t irs_zoom[10][IRSENSOR_FRAME_SIZE] = {
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x98, 0x00, 0x06, 0x01},   // 1X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x99, 0x00, 0x06, 0x02},   // 2X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9A, 0x00, 0x06, 0x03},   // 3X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9B, 0x00, 0x06, 0x04},   // 4X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9C, 0x00, 0x06, 0x05},   // 5X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9D, 0x00, 0x06, 0x06},   // 6X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9E, 0x00, 0x06, 0x07},   // 7X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0x9F, 0x00, 0x06, 0x08},   // 8X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0xA0, 0x00, 0x06, 0x09},   // 9X
    {0x68, 0x26, 0x02, 0x01, 0x00, 0x00, 0xA1, 0x00, 0x06, 0x0A}    // 10X
};


#define PANTILT_CALIB_PKT_SIZE (5)

rt_uint8_t calib_protcol[4][PANTILT_CALIB_PKT_SIZE] = {
    {0xE1, 0x1E, 0x08, 0xF1, 0x1F},
    {0xE1, 0x1E, 0x09, 0xF1, 0x1F},
    {0xE1, 0x1E, 0x08, 0xF1, 0x1F},
    {0xE1, 0x1E, 0x09, 0xF1, 0x1F},
};     

#define PANTILT_UARTPORT_NAME "uart2"
#define PANTILT_SEMAPHORE_NAME "shPTZ"
#define PANTILT_SEMAPHORE_RX_NAME "shPTZrx"

#define PANTILT_SEND_MP_NAME "mpPTZtx"
#define PANTILT_SEND_MB_NAME "mbPTZtx"

#define PANTILT_BUFFER_SIZE     (128)
#define PANTILT_RX_TIMEOUT      (10)

#define PANTILT_PKT_HEADER          (0x6D402D3E)
#define PANTILT_CALIB_PKT_HEADER    (0x1EE1)

#define PANTILT_VALUE_MAXIMUM   (500)
#define PANTILT_VALUE_MININUM   (-500)
#define PANTILT_VALUE_RATIO     (0.7142857f)

/* defined the LED pin: PA0 */
#define LED_PIN    GET_PIN(A, 0)

static rt_sem_t semaph = RT_NULL;
static rt_mailbox_t mailbox = RT_NULL;
static rt_mp_t      mempool = RT_NULL;

static rt_err_t pantilt_update_checksum(ptz_serialctrlpkt *pkt)
{
    rt_uint8_t *ptr = (rt_uint8_t*)pkt;
    rt_size_t pktsz = sizeof(ptz_serialctrlpkt);
    
    for (int i = 4; i < pktsz - 1; i++)
        pkt->checksum += *(ptr + i);
    
    return RT_EOK;
}

static rt_err_t uart_hook_callback(rt_device_t dev, rt_size_t sz)
{
    rt_sem_release(semaph);
    
    return RT_EOK;
}

static void pantilt_data_send_entry(void* parameter)
{
    struct guardian_environment *env = RT_NULL;
    rt_ubase_t mail;
    rt_uint8_t* pbuf;
    rt_device_t dev = RT_NULL;
    rt_uint32_t ubase32 = 0;
    rt_uint16_t ubase16 = 0;
    
    env = (struct guardian_environment*)parameter;
    RT_ASSERT(env != RT_NULL);
    
    dev = rt_device_find(PANTILT_UARTPORT_NAME);
    RT_ASSERT(dev != RT_NULL);
    
    LOG_I("send sub-thread, start!");
    
    while (1)
    {
        rt_mb_recv(mailbox, &mail, RT_WAITING_FOREVER);
        LOG_D("mb recv %0X", mail);
        if (mail == RT_NULL)
            continue;
        pbuf = (rt_uint8_t*)mail;
        
        ubase32 = *(rt_uint32_t*)pbuf;
        ubase16 = *(rt_uint16_t*)pbuf;
        
        if (ubase32 == PANTILT_PKT_HEADER)
        {
            LOG_D("send to pantilt");
            rt_device_write(dev, 0, pbuf, sizeof(ptz_serialctrlpkt));
        }
        else if(ubase16 == IRSENSOR_COLOR_HEADER)
        {
            LOG_D("send to irsensor color");
            rt_device_write(dev, 0, pbuf, IRSENSOR_FRAME_SIZE);
        }
        else if(ubase16 == PANTILT_CALIB_PKT_HEADER)
        {
            LOG_D("send to pantilt calib gyro");
            rt_device_write(dev, 0, pbuf, PANTILT_CALIB_PKT_SIZE);            
        }
        
        rt_mp_free(pbuf);
        
        rt_thread_delay(1);
    }
}

static void pantilt_data_recv_entry(void* parameter)
{
    struct guardian_environment *env = RT_NULL;
    rt_uint8_t* pbuf;
    rt_device_t dev = RT_NULL;
    
    env = (struct guardian_environment*)parameter;
    RT_ASSERT(env != RT_NULL);
    
    pbuf = rt_malloc(PANTILT_BUFFER_SIZE);
    RT_ASSERT(pbuf != RT_NULL);
    
    dev = rt_device_find(PANTILT_UARTPORT_NAME);
    RT_ASSERT(dev != RT_NULL);
    
    LOG_I("recv sub-thread, start!");
    
    while (1)
    {
        rt_thread_delay(RT_TICK_PER_SECOND); 
        
        // todo
    }
}

void pantilt_resolving_entry(void* parameter)
{
    rt_device_t dev = RT_NULL;
    struct guardian_environment *env = RT_NULL;
    ptz_serialctrlpkt ctrlpkt;
    rt_uint8_t *pbuf = RT_NULL;
    rt_size_t   pktsz;
    rt_err_t result = RT_EOK;
    rt_thread_t pthread = RT_NULL;
    
    env = (struct guardian_environment *)parameter;
    RT_ASSERT(env != RT_NULL);
    
    dev = rt_device_find(PANTILT_UARTPORT_NAME);
    RT_ASSERT(dev != RT_NULL);
    rt_device_open(dev, RT_DEVICE_OFLAG_RDWR | RT_DEVICE_FLAG_INT_RX);
    
    semaph = rt_sem_create(PANTILT_SEMAPHORE_RX_NAME, 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(semaph != RT_NULL);
    
    env->sh_ptz = rt_sem_create(PANTILT_SEMAPHORE_NAME, 0, RT_IPC_FLAG_FIFO);
    RT_ASSERT(env->sh_ptz != RT_NULL);
    mempool = rt_mp_create(PANTILT_SEND_MP_NAME, 8, PANTILT_BUFFER_SIZE);
    RT_ASSERT(mempool != RT_NULL);
    mailbox = rt_mb_create(PANTILT_SEND_MB_NAME, 8, RT_IPC_FLAG_FIFO);
    RT_ASSERT(mailbox != RT_NULL);
    
    // set uart in 115200, 8N1.
    struct serial_configure config = RT_SERIAL_CONFIG_DEFAULT;
    rt_device_control(dev, RT_DEVICE_CTRL_CONFIG, &config);
    
    rt_device_set_rx_indicate(dev, uart_hook_callback);
    
    pthread = rt_thread_create("tPTZtx", pantilt_data_send_entry, env, 2048, 10, 20);
    RT_ASSERT(pthread != RT_NULL);
    result = rt_thread_startup(pthread);
    RT_ASSERT(result == RT_EOK);
    
    pthread = rt_thread_create("tPTZrx", pantilt_data_recv_entry, env, 2048, 10, 20);
    RT_ASSERT(pthread != RT_NULL);
    result = rt_thread_startup(pthread);
    RT_ASSERT(result == RT_EOK);
    
    LOG_I("initialization finish, start!");

    while (1)
    {
        result = rt_sem_take(env->sh_ptz, RT_WAITING_FOREVER);

        
        if (env->ptz_action == PANTILT_ACTION_IRZOOM)
        {
            env->ptz_action = PANTILT_ACTION_NULL;
            LOG_D("PANTILT_ACTION_IRZOOM");
            
            pktsz = IRSENSOR_FRAME_SIZE;
            
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            
            if (env->irs_zoom > 9)
                env->irs_zoom = 9;
            
            rt_memcpy(pbuf, irs_zoom[env->irs_zoom], pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);                
        }
        else if (env->ptz_action == PANTILT_ACTION_IRCOLOR)
        {
            env->ptz_action = PANTILT_ACTION_NULL;
            LOG_D("PANTILT_ACTION_IRCOLOR");
            
            switch(env->irs_color)
            {
                case 0:
                    rt_memcpy(irs_control_frame, irs_color[0], IRSENSOR_FRAME_SIZE);
                    break;
                case 1:
                    rt_memcpy(irs_control_frame, irs_color[1], IRSENSOR_FRAME_SIZE);
                    break;
                case 2:
                    rt_memcpy(irs_control_frame, irs_color[2], IRSENSOR_FRAME_SIZE);
                    break;
                default:
                    rt_memcpy(irs_control_frame, irs_color[3], IRSENSOR_FRAME_SIZE);
                    break;
            }

            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, irs_control_frame, IRSENSOR_FRAME_SIZE);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);
      
        }
        else if (env->ptz_action == PANTILT_ACTION_CALIBRATE)
        {
            LOG_D("PANTILT_ACTION_CALIBRATE");
            env->ptz_action = PANTILT_ACTION_NULL;
            
            for (int i = 0; i < 4; i++)
            {
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, calib_protcol[i], PANTILT_CALIB_PKT_SIZE);
                rt_mb_send(mailbox, (rt_ubase_t)pbuf);
                rt_thread_delay(200);
            }
        } 
        else if (env->ptz_action == PANTILT_ACTION_HOMING)
        {
            LOG_D("PANTILT_ACTION_HOMING");
            
            pktsz = sizeof(ptz_serialctrlpkt);
            
            rt_memset(&ctrlpkt, 0x00, pktsz);
            ctrlpkt.HEADER = PANTILT_PKT_HEADER;
            
            ctrlpkt.homing = 0x9BFE;
            pantilt_update_checksum(&ctrlpkt);
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, &ctrlpkt, pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);
            
            rt_thread_delay(100);
            
            rt_memset(&ctrlpkt, 0x00, pktsz);
            ctrlpkt.HEADER = PANTILT_PKT_HEADER;               
            ctrlpkt.homing = 0x0000;
            pantilt_update_checksum(&ctrlpkt);
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, &ctrlpkt, pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);
        }

        if (env->trck_incharge)
        {
            pktsz = sizeof(ptz_serialctrlpkt);
            rt_memset(&ctrlpkt, 0x00, pktsz);

            ctrlpkt.HEADER = PANTILT_PKT_HEADER;
            
            switch(env->cam_pip_mode)
            {
                case 1:
                case 3:
                    ctrlpkt.pitch   = env->trck_err_y * ZOOM2RATIO[env->cam_zoom_pos];
                    ctrlpkt.yaw     = env->trck_err_x * ZOOM2RATIO[env->cam_zoom_pos];
                    break;
                case 2:
                case 4:
                    ctrlpkt.pitch   = env->trck_err_y * IR2RATIO;
                    ctrlpkt.yaw     = env->trck_err_x * IR2RATIO;
                    break;
            }

            pantilt_update_checksum(&ctrlpkt);
               
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, &ctrlpkt, pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);

            if (env->trck_lost == RT_TRUE)
            {
                env->trck_incharge = RT_FALSE;
                env->trck_lost = RT_FALSE;
                
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, &ctrlpkt, pktsz);
                rt_mb_send(mailbox, (rt_ubase_t)pbuf);
            }
            // todo.
        }
        else if (env->sbus_incharge)
        {
            env->sbus_incharge = RT_FALSE;

            rt_int16_t dval_pitch, dval_yaw, dval_roll = 0;
            LOG_D("PANTILT_ACTION_OTHER, %d", env->ptz_action);
            
            pktsz = sizeof(ptz_serialctrlpkt);
            rt_memset(&ctrlpkt, 0x00, pktsz);
                        
            ctrlpkt.HEADER = PANTILT_PKT_HEADER;

            dval_pitch = env->ch_value[1] - SBUS_VALUE_MEDIAN;    // pitch
            if (abs(dval_pitch) < SBUS_VALUE_IGNORE)
                dval_pitch = 0;
           
            dval_yaw = env->ch_value[3] - SBUS_VALUE_MEDIAN;    // yaw
            if (abs(dval_yaw) < SBUS_VALUE_IGNORE)
                dval_yaw = 0;
            
            switch(env->cam_pip_mode)
            {
                case 1:
                case 3:
                default:
                    ctrlpkt.pitch   = dval_pitch * PANTILT_VALUE_RATIO * ZOOM2RATIO[env->cam_zoom_pos];
                    ctrlpkt.yaw     = dval_yaw * PANTILT_VALUE_RATIO * ZOOM2RATIO[env->cam_zoom_pos];
                    break;
                case 2:
                case 4:
                    ctrlpkt.pitch   = dval_pitch * IR2RATIO;
                    ctrlpkt.yaw     = dval_yaw * IR2RATIO;
                    break;
            }
                
//            LOG_D("pitch: %d, yaw: %d", ctrlpkt.pitch, ctrlpkt.yaw);
//            
//            ctrlpkt.roll = dval_roll;
                
            if (env->ptz_action == PANTILT_ACTION_HEADFREE)
                ctrlpkt.mode = 0x0000;
            else if (env->ptz_action == PANTILT_ACTION_HEADLOCK)
                ctrlpkt.mode = 0x6400;               
            else if (env->ptz_action == PANTILT_ACTION_HEADDOWN)
                ctrlpkt.mode = 0x9BFE;               
            
            pantilt_update_checksum(&ctrlpkt);
               
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, &ctrlpkt, pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);
            
            if ((ctrlpkt.roll == 0) || (ctrlpkt.pitch == 0) || (ctrlpkt.yaw == 0)) // send again, ensure stop.
            {
                rt_thread_delay(10);
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, &ctrlpkt, pktsz);
                rt_mb_send(mailbox,  (rt_ubase_t)pbuf);
                rt_thread_delay(10);
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, &ctrlpkt, pktsz);
                rt_mb_send(mailbox,  (rt_ubase_t)pbuf);
            }
        }
        else if (env->user_incharge)
        {
            env->user_incharge = RT_FALSE;

            rt_int16_t dval_pitch, dval_yaw, dval_roll = 0;
            LOG_D("PANTILT_ACTION_OTHER, %d", env->ptz_action);
            
            pktsz = sizeof(ptz_serialctrlpkt);
            rt_memset(&ctrlpkt, 0x00, pktsz);
                        
            ctrlpkt.HEADER = PANTILT_PKT_HEADER;

//            dval_roll = env->ch_value[0] - SBUS_VALUE_MEDIAN;    // roll
//            if (abs(dval_roll) < SBUS_VALUE_IGNORE)
//                dval_roll = 0;
//            else
//            {
//                if (dval_roll < 0)
//                    dval_roll = PANTILT_VALUE_MININUM;
//                else
//                    dval_roll = PANTILT_VALUE_MAXIMUM;
//            }
            
            dval_pitch = env->ch_value[1] - SBUS_VALUE_MEDIAN;    // pitch
            if (abs(dval_pitch) < SBUS_VALUE_IGNORE)
                dval_pitch = 0;
           
            dval_yaw = env->ch_value[3] - SBUS_VALUE_MEDIAN;    // yaw
            if (abs(dval_yaw) < SBUS_VALUE_IGNORE)
                dval_yaw = 0;
            
            switch(env->cam_pip_mode)
            {
                case 1:
                case 3:
                default:
                    ctrlpkt.pitch   = dval_pitch * PANTILT_VALUE_RATIO * ZOOM2RATIO[env->cam_zoom_pos];
                    ctrlpkt.yaw     = dval_yaw * PANTILT_VALUE_RATIO * ZOOM2RATIO[env->cam_zoom_pos];
                    break;
                case 2:
                case 4:
                    ctrlpkt.pitch   = dval_pitch * IR2RATIO;
                    ctrlpkt.yaw     = dval_yaw * IR2RATIO;
                    break;
            }
            
//                ctrlpkt.roll = dval_roll;
            
            if (env->ptz_action == PANTILT_ACTION_HEADFREE)
                ctrlpkt.mode = 0x0000;
            else if (env->ptz_action == PANTILT_ACTION_HEADLOCK)
                ctrlpkt.mode = 0x6400;               
            else if (env->ptz_action == PANTILT_ACTION_HEADDOWN)
                ctrlpkt.mode = 0x9BFE;               
            
            pantilt_update_checksum(&ctrlpkt);
               
            pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
            rt_memcpy(pbuf, &ctrlpkt, pktsz);
            rt_mb_send(mailbox, (rt_ubase_t)pbuf);
            
            if ((ctrlpkt.roll == 0) && (ctrlpkt.pitch == 0) && (ctrlpkt.yaw && 0) && (env->ptz_action == PANTILT_ACTION_NULL)) // send again, ensure stop.
            {
                rt_thread_delay(10);
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, &ctrlpkt, pktsz);
                rt_mb_send(mailbox,  (rt_ubase_t)pbuf);
                rt_thread_delay(10);
                pbuf = rt_mp_alloc(mempool, RT_WAITING_FOREVER);
                rt_memcpy(pbuf, &ctrlpkt, pktsz);
                rt_mb_send(mailbox,  (rt_ubase_t)pbuf);                       
            }
        }
    }
    
    // never be here.
}
