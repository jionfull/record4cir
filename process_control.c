/*
 * process_control.c
 *
 *  Created on: Mar 11, 2014
 *      Author: lifeng
 */

#include <stdio.h>
#include <string.h>

#include <unistd.h>
#include <pthread.h>

#include "serial/frame_manager.h"
#include "serial/frame.h"

#include "lib/hwclock.h"
#include "lib/bcd.h"

#include "lib/block_filter.h"
#include "lib/block_manager.h"

#include "storage/record_manager.h"


#include "led/led.h"
#include "sound/sound.h"

#include "config/config.h"
#include "config/cab_type.h"

#include "global.h"

#include "./state/state.h"
pthread_t thread_control;

const char version[16] = "V2.1.01-16/03/31";

static void * proc_control(void *args) {
	struct frame_manager *manager = get_frame_manager(CONTROL_MANAGER);
	struct record_manager * record_manager=get_record_manager();
	struct block_filter *filter = NULL;

	struct frame *pframe = NULL;
	struct block * pblock = NULL;
	char buffer[64];
	int length;
	char dest_addr = 0;
	char src_addr = 0;
	char operation = 0;
	char cmd = 0;
	char tmp;

	if (manager == NULL)
		return NULL;
	filter = get_frame_filter(manager);
	while (1) {
		pblock = get_block(filter, TIMEOUT_MAIN_UNIT, BLOCK_FULL);
		if (pblock != NULL) {
			set_sys_state(BIT5_MAIN_UNIT,STATE_MAINUNIT_OK);
			light_on(0);
			light_main_alarm(0);
			pframe = (struct frame*) pblock->data;
			pframe->length = pblock->data_length;
			dest_addr = destination_of_frame(pframe);
			src_addr = pframe->data[4];
			cmd = command_of_frame(pframe);
			operation = operation_of_frame(pframe);

			if (src_addr == MASTER_ADDRESS) {

				switch (dest_addr) {
				case RADIO_450M_ADDRESS:
					if ((operation == 0x03) && (cmd == 0x20)) { //向450M发送机车号
						char * cab_id = get_id();

						get_frame_real_data(pframe, buffer, &length);

						buffer[8] = 0;
						tmp = buffer[3];
						buffer[3] = 0;
						int cab_type = atoi(buffer);

						buffer[3] = tmp;

						sprintf(cab_id, "%s-%s", get_cab_code(cab_type),
								buffer + 3);

					}
					break;
				case ALL_MMI_ADDRESS:
					if ((operation == 0x03) && (cmd == 0x46)) { //关机命令

						flush_all_data(record_manager);
					}
					break;
				}

			}

			if (dest_addr == RECORD_ADDRESS) {

				switch (operation) {
				case 1: //维护信息
					// echo off
					tmp = 0;
					if (src_addr == 0x01) {
						tmp = 0x01;
					} else if ((src_addr == 0x03) || (src_addr == 0x04)) {
						tmp = 0x41;
					}
					send_frame(manager, RECORD_ADDRESS, src_addr, 1, tmp,
							pframe->data + 9, 2);

					switch (cmd) {
					case (char) 0x33: //播放控制
					{
						if (pframe->data[10] == (char) 0xff) {
							start_play(src_addr);
						} else if (pframe->data[10] == 0) {
							stop_play();
						}
					}
						break;
					case (char) 0xfa: //问询测试
						break;
					case (char) 0x34: //查询时钟
					{
						struct tm time;
						get_time(&time);

						buffer[0] = to_bcd(time.tm_year - 100); //year
						buffer[1] = to_bcd(time.tm_mon + 1);
						buffer[2] = to_bcd(time.tm_mday);
						buffer[3] = to_bcd(time.tm_hour);
						buffer[4] = to_bcd(time.tm_min);
						buffer[5] = to_bcd(time.tm_sec);
						send_frame(manager, RECORD_ADDRESS, src_addr, 1,
								(char) 0x91, buffer, 6);
					}
						break;
					case (char) 0x35: //设置时钟
						break;
					case (char) 0xa5: //查询软件版本
					{

						bcopy(version, buffer, 16);
						send_frame(manager, RECORD_ADDRESS, src_addr, 1,
								(char) 0xaa, buffer, 16);
					}
						break;

					}

					break;
				case 3: //不需要应答
					break;
				}
			}
			put_block(pblock, BLOCK_EMPTY);
		} else {
			set_sys_state(BIT5_MAIN_UNIT,STATE_MAINUNIT_FAIL);
			light_main_alarm(1);
		}
	}

	return NULL;
}

void start_control_process() {
	pthread_create(&thread_control, NULL, proc_control, NULL);
}

