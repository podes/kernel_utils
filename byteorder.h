#pragma once

#ifndef _BSD_SOURCE
#define _BSD_SOURCE
#endif

#include <endian.h>

#define be16_to_cpu	be16toh
#define le16_to_cpu	le16toh
#define le32_to_cpu	le32toh
#define le64_to_cpu	le64toh

#define cpu_to_be16	htobe16
#define cpu_to_le16	htole16
#define cpu_to_le32	htole32
#define cpu_to_le64	htole64

