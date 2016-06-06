//
// Copyright (c) 2016-present, Facebook, Inc.
// All rights reserved.
//
// This source code is licensed under the BSD-style license found in the
// LICENSE file in the root directory of this source tree. An additional grant 
// of patent rights can be found in the PATENTS file in the same directory.
// 

#include "comm.h"
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <memory.h>
#include <sys/types.h>
#include <sys/ipc.h>
#include <sys/msg.h>
/*#include <errno.h>*/

#define MESSAGE_HEAD_SIZE sizeof(long)

/* Simple communication.*/
int CommInit(int id, int create_new) {
  key_t key;
  if ((key = ftok("/home/yuandong/.bashrc", id)) == -1) {
    printf("Error! generate key failed!\n");
    exit(1);
  }
  int channel_id = -1;
  int attr = 0644;
  if (create_new == 1) attr |= IPC_CREAT;
  if ((channel_id = msgget(key, attr)) == -1) {
    printf("Error! create message queue failed! \n");
    exit(1);
  }
  return channel_id;
}

/* Send a message through the message queue. size is the total size of the buffer in bytes.*/
void CommSend(int channel_id, void *message, int size) {
  if (msgsnd(channel_id, message, size - MESSAGE_HEAD_SIZE, 0) == -1) {
    printf("Error! Send message wrong!\n");
    exit(1);
  }
}

/* Send a message through the message queue. If the queue is full, return -1, else return 0 */
int CommSendNoBlock(int channel_id, void *message, int size) {
  if (msgsnd(channel_id, message, size - MESSAGE_HEAD_SIZE, IPC_NOWAIT) == -1) {
    return -1;
  }
  return 0;
}

/* Receive a message through the queue. If no message, wait until there is one. */
void CommReceive(int channel_id, void *message, int size) {
  long type = * (long *)message;
  if (msgrcv(channel_id, message, size - MESSAGE_HEAD_SIZE, type, 0) == -1) {
    printf("Error! Receive message wrong!\n");
    exit(1);
  }
}

/* Receive a message through the queue. If no message, wait until there is one. */
int CommReceiveNoBlock(int channel_id, void *message, int size) {
  long type = * (long *)message;
  if (msgrcv(channel_id, message, size - MESSAGE_HEAD_SIZE, type, IPC_NOWAIT) == -1) {
    return -1;
  }
  return 0;
}

/* Destory the message queue. */
void CommDestroy(int channel_id) {
  if (msgctl(channel_id, IPC_RMID, NULL) == -1) {
    printf("Error! Failed to destroy message queue! \n");
    exit(1);
  }
}
