#ifndef LONG_DEBUG_H
#define LONG_DEBUG_H

#include "vm.h"

void print_chunk(vm_t);
void print_globals(vm_t);
void print_locals(vm_t);
void print_stack(vm_t);
void print_vm(vm_t);

#endif
