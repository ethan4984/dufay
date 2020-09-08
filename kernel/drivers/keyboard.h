#pragma once

#include <lib/memoryUtils.h>
#include <lib/asmUtils.h>

namespace kernel {

void keyboardHandlerMain(regs_t *regs);

void changeInputFunction(function<void, uint8_t> newFunc);

}