#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "chunk.h"
#include "vm.h"

static void repl() {
    char line[1024];
    // Inicia VM uma vez para manter estado global (variáveis)
    initVM(&vm);
    printf("Apolo Lang v2.0\nDigite 'sair' para fechar.\n");
    
    for (;;) {
        printf("apolo > ");
        if (!fgets(line, sizeof(line), stdin)) { printf("\n"); break; }
        
        line[strcspn(line, "\n")] = 0;
        if (strcmp(line, "sair") == 0) break;
        
        interpret(&vm, line);
    }
    freeVM(&vm);
}

static char* readFile(const char* path) {
    FILE* file = fopen(path, "rb");
    if (file == NULL) {
        fprintf(stderr, "Nao foi possivel abrir o arquivo \"%s\".\n", path);
        exit(74);
    }
    fseek(file, 0L, SEEK_END);
    size_t fileSize = ftell(file);
    rewind(file);
    char* buffer = (char*)malloc(fileSize + 1);
    if (buffer == NULL) {
        fprintf(stderr, "Memoria insuficiente para ler \"%s\".\n", path);
        exit(74);
    }
    size_t bytesRead = fread(buffer, sizeof(char), fileSize, file);
    buffer[bytesRead] = '\0';
    fclose(file);
    return buffer;
}

static void runFile(const char* path) {
    char* source = readFile(path);
    initVM(&vm);
    InterpretResult result = interpret(&vm, source);
    freeVM(&vm);
    free(source);
    if (result == INTERPRET_COMPILE_ERROR) exit(65);
    if (result == INTERPRET_RUNTIME_ERROR) exit(70);
}

int main(int argc, char* argv[]) {
    if (argc == 1) {
        repl();
    } else if (argc == 2) {
        runFile(argv[1]);
    } else {
        fprintf(stderr, "Uso: apolo [caminho do arquivo]\n");
        exit(64);
    }
    return 0;
}