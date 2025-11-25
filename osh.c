#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>

#define MAX_LINE 80
#define MAX_ARGS ((MAX_LINE / 2) + 1)

/**
 * Remove espaços em branco, tabs e quebras de linha do início e fim de uma string.
 * Retorna um ponteiro para a string aparada.
 */
static char *trim(char *s) {

    if (!s)
        return s;

    while (*s == ' ' || *s == '\t' || *s == '\n' || *s == '\r')
        s++;

    char *end = s + strlen(s) - 1;

    while (end > s && (*end == ' ' || *end == '\t' || *end == '\n' || *end == '\r')) {
        *end = '\0';
        end--;
    }

    return s;
}

/**
 * Divide uma linha de comando em tokens individuais (palavras).
 * Retorna o número de tokens encontrados.
 */
int tokenizar(char *line, char *tokens[], int max_tokens) {

    int count = 0;
    char *p = line;

    while (p && *p != '\0') {
        while (*p == ' ' || *p == '\t')
            p++;

        if (*p == '\0')
            break;

        if (count >= max_tokens - 1)
            break;

        tokens[count++] = p;

        while (*p != '\0' && *p != ' ' && *p != '\t' && *p != '\n')
            p++;

        if (*p == '\0')
            break;

        *p = '\0';
        p++;
    }

    tokens[count] = NULL;
    return count;
}

/**
 * Constrói um array de argumentos (argv) a partir de uma lista de tokens.
 * Copia os tokens da posição start até end-1 para o array argv.
 */
void construir_argv(char *tokens[], int start, int end, char *argv[]) {

    int j = 0;
    
    for (int i = start; i < end; ++i)
        argv[j++] = tokens[i];

    argv[j] = NULL;
}

/**
 * Executa um comando usando execvp.
 * Se o comando falhar, exibe uma mensagem de erro e encerra o processo filho.
 */
void exec_command(char *argv[]) {

    if (argv == NULL || argv[0] == NULL) {
        fprintf(stderr, "exec_command: argv inválido\n");
        exit(EXIT_FAILURE);
    }

    execvp(argv[0], argv);

    fprintf(stderr, "osh: erro ao executar '%s': %s\n", argv[0], strerror(errno));
    exit(EXIT_FAILURE);
}

int main(void) {

    char line[MAX_LINE];
    char *tokens[MAX_ARGS];
    char *argv[MAX_ARGS];
    int should_run = 1;

    while (should_run) {
        printf("osh> ");
        fflush(stdout);

        if (!fgets(line, sizeof(line), stdin)) {
            printf("\n");
            break;
        }

        char *trimmed = trim(line);
        if (trimmed[0] == '\0')
            continue;

        if (strcmp(trimmed, "exit") == 0) {
            should_run = 0;
            continue;
        }

        char line_copy[MAX_LINE];
        strncpy(line_copy, trimmed, MAX_LINE);
        
        line_copy[MAX_LINE - 1] = '\0';

        int ntok = tokenizar(line_copy, tokens, MAX_ARGS);

        if (ntok == 0)
            continue;

        construir_argv(tokens, 0, ntok, argv);

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            continue;
        } else if (pid == 0) {
            exec_command(argv);
        } else {
            wait(NULL);
        }
    }

    return 0;
}
