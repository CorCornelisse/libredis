#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "redis.h"

struct _Redis
{
};

static Redis redis_instance;

Redis *Redis_get_instance()
{
	return &redis_instance;
}

void *_Redis_alloc(Redis *redis, size_t size)
{
	return malloc(size);
}



