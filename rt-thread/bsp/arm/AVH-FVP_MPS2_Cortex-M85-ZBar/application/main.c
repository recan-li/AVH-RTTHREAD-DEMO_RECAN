/*---------------------------------------------------------------------------
 * Copyright (c) 2021 Arm Limited (or its affiliates). All rights reserved.
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the License); you may
 * not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an AS IS BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *---------------------------------------------------------------------------*/

#include <stdio.h>
#include <rtthread.h>

extern int zbar_main(void);
extern int rpc_fs_main(int argc, const char *argv[]);

void main_thread_entry22(void *arg)
{
    //rpc_fs_main(0, NULL);
    zbar_main();

    while (1) {
        //printf("22222 %s:%d\n", __func__, __LINE__);
        rt_thread_delay(1000);
    }
}

int main (void) 
{   
    int cnt = 1;

    rt_thread_t tid;

    tid = rt_thread_create("zbar", main_thread_entry22, RT_NULL, 4096, RT_MAIN_THREAD_PRIORITY / 2, 10);
    rt_thread_startup(tid);

    extern int avh_rtt_debug_server_main(int argc, const char *argv[]);
    const char *argv[] = 
    {
        "tcp_server",
        "12345",
        "12346",
    };
    avh_rtt_debug_server_main(3, argv);

    while (1)
    {
        //printf("===== %d ...\n", cnt++);
        //rt_kprintf("=====--- %d ...\n", cnt++);
        //printf(" root\n");
        rt_thread_delay(10000);
    }

    return 0;
}
