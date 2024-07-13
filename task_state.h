#pragma once

#define TASK_RUNNING			0x0000
#define TASK_INTERRUPTIBLE		0x0001
#define TASK_UNINTERRUPTIBLE		0x0002
#define TASK_NOLOAD			0x0400
#define TASK_NEW			0x0800
#define TASK_IDLE			(TASK_UNINTERRUPTIBLE | TASK_NOLOAD)


