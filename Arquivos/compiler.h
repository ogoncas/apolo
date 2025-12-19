#ifndef APOLO_COMPILER_H
#define APOLO_COMPILER_H

#include "object.h"
#include "vm.h"

bool compile(const char* source, Chunk* chunk);

#endif