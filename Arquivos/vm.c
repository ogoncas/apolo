#include <stdio.h>
#include <stdarg.h>
#include <string.h>
#include <stdlib.h>

#include "common.h"
#include "compiler.h"
#include "object.h"
#include "vm.h"

VM vm;

static void resetStack(VM* vmptr) {
    vmptr->stackTop = vmptr->stack;
}

static void runtimeError(VM* vmptr, const char* format, ...) {
    va_list args;
    va_start(args, format);
    vfprintf(stderr, format, args);
    va_end(args);
    fputs("\n", stderr);

    size_t instruction = vmptr->ip - vmptr->chunk->code - 1;
    int line = vmptr->chunk->lines[instruction];
    fprintf(stderr, "[Linha %d] no script\n", line);
    resetStack(vmptr);
}

void initVM(VM* vmptr) {
    resetStack(vmptr);
    vmptr->objects = NULL;
    initTable(&vmptr->globals);
    initTable(&vmptr->strings);
}

void freeVM(VM* vmptr) {
    freeTable(&vmptr->globals);
    freeTable(&vmptr->strings);
    
    Obj* object = vmptr->objects;
    while (object != NULL) {
        Obj* next = object->next;
        if (object->type == OBJ_STRING) {
            ObjString* string = (ObjString*)object;
            free(string->chars);
        }
        free(object);
        object = next;
    }
}

void push(VM* vmptr, Value value) {
    *vmptr->stackTop = value;
    vmptr->stackTop++;
}

Value pop(VM* vmptr) {
    vmptr->stackTop--;
    return *vmptr->stackTop;
}

static Value peek(VM* vmptr, int distance) {
    return vmptr->stackTop[-1 - distance];
}

static bool isFalsey(Value value) {
    return IS_NIL(value) || (IS_BOOL(value) && !AS_BOOL(value));
}

static void concatenate(VM* vmptr) {
    ObjString* b = AS_STRING(pop(vmptr));
    ObjString* a = AS_STRING(pop(vmptr));

    int length = a->length + b->length;
    char* chars = (char*)malloc(length + 1);
    memcpy(chars, a->chars, a->length);
    memcpy(chars + a->length, b->chars, b->length);
    chars[length] = '\0';

    ObjString* result = takeString(chars, length);
    push(vmptr, OBJ_VAL(result));
}

static InterpretResult run(VM* vmptr) {
    #define READ_BYTE() (*vmptr->ip++)
    #define READ_SHORT() (vmptr->ip += 2, (uint16_t)((vmptr->ip[-2] << 8) | vmptr->ip[-1]))
    #define READ_CONSTANT() (vmptr->chunk->constants.values[READ_BYTE()])
    #define READ_STRING() AS_STRING(READ_CONSTANT())
    #define BINARY_OP(valueType, op) \
        do { \
            if (!IS_NUMBER(peek(vmptr, 0)) || !IS_NUMBER(peek(vmptr, 1))) { \
                runtimeError(vmptr, "Operandos devem ser números."); \
                return INTERPRET_RUNTIME_ERROR; \
            } \
            double b = AS_NUMBER(pop(vmptr)); \
            double a = AS_NUMBER(pop(vmptr)); \
            push(vmptr, valueType(a op b)); \
        } while (false)

    for (;;) {
        Byte instruction;
        switch (instruction = READ_BYTE()) {
            case OP_CONSTANT: push(vmptr, READ_CONSTANT()); break;
            case OP_NIL:   push(vmptr, NIL_VAL); break;
            case OP_TRUE:  push(vmptr, BOOL_VAL(true)); break;
            case OP_FALSE: push(vmptr, BOOL_VAL(false)); break;
            case OP_POP: pop(vmptr); break;
            
            case OP_GET_LOCAL: {
                uint8_t slot = READ_BYTE();
                push(vmptr, vmptr->stack[slot]);
                break;
            }
            case OP_SET_LOCAL: {
                uint8_t slot = READ_BYTE();
                vmptr->stack[slot] = peek(vmptr, 0);
                break;
            }
            case OP_GET_GLOBAL: {
                ObjString* name = READ_STRING();
                Value value;
                if (!tableGet(&vmptr->globals, name, &value)) {
                    runtimeError(vmptr, "Variável indefinida '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                push(vmptr, value);
                break;
            }
            case OP_DEFINE_GLOBAL: {
                ObjString* name = READ_STRING();
                tableSet(&vmptr->globals, name, peek(vmptr, 0));
                pop(vmptr);
                break;
            }
            case OP_SET_GLOBAL: {
                ObjString* name = READ_STRING();
                if (tableSet(&vmptr->globals, name, peek(vmptr, 0))) {
                    tableDelete(&vmptr->globals, name);
                    runtimeError(vmptr, "Variável indefinida '%s'.", name->chars);
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }

            case OP_EQUAL: {
                Value b = pop(vmptr);
                Value a = pop(vmptr);
                push(vmptr, BOOL_VAL(valuesEqual(a, b)));
                break;
            }
            case OP_GREATER:  BINARY_OP(BOOL_VAL, >); break;
            case OP_LESS:     BINARY_OP(BOOL_VAL, <); break;
            case OP_ADD: {
                if (IS_STRING(peek(vmptr, 0)) && IS_STRING(peek(vmptr, 1))) {
                    concatenate(vmptr);
                } else if (IS_NUMBER(peek(vmptr, 0)) && IS_NUMBER(peek(vmptr, 1))) {
                    double b = AS_NUMBER(pop(vmptr));
                    double a = AS_NUMBER(pop(vmptr));
                    push(vmptr, NUMBER_VAL(a + b));
                } else {
                    runtimeError(vmptr, "Operandos devem ser dois números ou duas strings.");
                    return INTERPRET_RUNTIME_ERROR;
                }
                break;
            }
            case OP_SUB:      BINARY_OP(NUMBER_VAL, -); break;
            case OP_MUL:      BINARY_OP(NUMBER_VAL, *); break;
            case OP_DIV:      BINARY_OP(NUMBER_VAL, /); break;
            case OP_NOT:      push(vmptr, BOOL_VAL(isFalsey(pop(vmptr)))); break;
            case OP_NEGATE:   
                if (!IS_NUMBER(peek(vmptr, 0))) {
                     runtimeError(vmptr, "Operando deve ser número.");
                     return INTERPRET_RUNTIME_ERROR;
                }
                push(vmptr, NUMBER_VAL(-AS_NUMBER(pop(vmptr)))); 
                break;
            
            case OP_PRINT: {
                printValue(pop(vmptr));
                printf("\n");
                break;
            }
            case OP_INPUT: {
                char buffer[1024];
                if (fgets(buffer, sizeof(buffer), stdin)) {
                    buffer[strcspn(buffer, "\n")] = 0;
                    push(vmptr, OBJ_VAL(copyString(buffer, strlen(buffer))));
                } else {
                    push(vmptr, NIL_VAL);
                }
                break;
            }
            case OP_JUMP: {
                uint16_t offset = READ_SHORT();
                vmptr->ip += offset;
                break;
            }
            case OP_JUMP_IF_FALSE: {
                uint16_t offset = READ_SHORT();
                if (isFalsey(peek(vmptr, 0))) vmptr->ip += offset;
                break;
            }
            case OP_LOOP: {
                uint16_t offset = READ_SHORT();
                vmptr->ip -= offset;
                break;
            }
            case OP_RETURN: return INTERPRET_OK;
        }
    }
}

InterpretResult interpret(VM* vmptr, const char* source) {
    Chunk chunk;
    initChunk(&chunk);

    if (!compile(source, &chunk)) {
        freeChunk(&chunk);
        return INTERPRET_COMPILE_ERROR;
    }

    vmptr->chunk = &chunk;
    vmptr->ip = vmptr->chunk->code;

    InterpretResult result = run(vmptr);

    freeChunk(&chunk);
    return result;
}