//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#ifndef _COMM_H_
#define _COMM_H_

/* Simple communication. Used for communication between two processes. */

/* if create_new == 1, create the channel; otherwise just open it */
int CommInit(int id, int create_new);

/* Send a message through the message queue. If the queue is full, wait until it is not full.*/
void CommSend(int channel_id, void *message, int size);

/* Send a message through the message queue. If the queue is full, return -1, else return 0 */
int CommSendNoBlock(int channel_id, void *message, int size);

/* Receive a message through the queue. If no message, wait until there is one. */
void CommReceive(int channel_id, void *message, int size);

/* Receive a message through the queue. If no message, return -1, else return 0 */
int CommReceiveNoBlock(int channel_id, void *message, int size);

/* Destory the message queue. */
void CommDestroy(int channel_id);

#endif
