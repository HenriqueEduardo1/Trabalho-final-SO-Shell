#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <errno.h>
#include <fcntl.h> // Necessário para open()

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
    char history[MAX_LINE] = {0};
    int has_history = 0;
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

        char *command_to_run = NULL;

        /* Comando para sair do shell */
        if (strcmp(trimmed, "exit") == 0) {
            should_run = 0;
            continue;
        }

        /* Comando de histórico: executa o último comando (!!) */
        if (strcmp(trimmed, "!!") == 0) {
            if (!has_history) {
                printf("Nenhum comando no histórico\n");
                fflush(stdout);
                continue;
            }

            command_to_run = history;
            /* Mostra comando recente do histórico */
            printf("%s\n", command_to_run);
            fflush(stdout);
        } else {
            /* Se não for !! ele executa o que foi digitado como comando */
            strncpy(history, trimmed, MAX_LINE);
            history[MAX_LINE - 1] = '\0';
            has_history = 1;
            command_to_run = trimmed;
        }

        char line_copy[MAX_LINE];
        strncpy(line_copy, command_to_run, MAX_LINE);
        
        line_copy[MAX_LINE - 1] = '\0';

        int ntok = tokenizar(line_copy, tokens, MAX_ARGS);

        if (ntok == 0)
            continue;


        /* Verificar execução em background (&) */
        int background = 0;
        if (ntok > 0 && strcmp(tokens[ntok - 1], "&") == 0) {
            background = 1;
            ntok--; /* Ignora o & na lista de argumentos */
        }

        /* Verificar Pipe (|) */
        int pipe_idx = -1;
        for (int i = 0; i < ntok; i++) {
            if (strcmp(tokens[i], "|") == 0) {
                pipe_idx = i;
                break;
            }
        }

        if (pipe_idx != -1) {
            /* PONTO 2: Comunicação via pipe */
            int fd[2];
            if (pipe(fd) == -1) {
                perror("pipe");
                continue;
            }

            char *argv1[MAX_ARGS];
            char *argv2[MAX_ARGS];

            /* Constrói argumentos para o primeiro e segundo comando */
            construir_argv(tokens, 0, pipe_idx, argv1);
            construir_argv(tokens, pipe_idx + 1, ntok, argv2);

            pid_t p1 = fork();
            if (p1 < 0) {
                perror("fork (p1)");
                close(fd[0]); close(fd[1]);
                continue;
            }

            if (p1 == 0) {
                /* Filho 1: Redireciona stdout para o pipe */
                close(fd[0]); /* Fecha leitura */
                if (dup2(fd[1], STDOUT_FILENO) == -1) {
                    perror("dup2 p1");
                    exit(EXIT_FAILURE);
                }
                close(fd[1]); /* Fecha escrita após dup */
                exec_command(argv1);
            }

            pid_t p2 = fork();
            if (p2 < 0) {
                perror("fork (p2)");
                /* O p1 já está rodando, precisamos limpar */
                close(fd[0]); close(fd[1]);
                wait(NULL); 
                continue;
            }

            if (p2 == 0) {
                /* Filho 2: Redireciona stdin do pipe */
                close(fd[1]); /* Fecha escrita */
                if (dup2(fd[0], STDIN_FILENO) == -1) {
                    perror("dup2 p2");
                    exit(EXIT_FAILURE);
                }
                close(fd[0]); /* Fecha leitura após dup */
                exec_command(argv2);
            }

            /* Pai: fecha pipes e aguarda filhos */
            close(fd[0]);
            close(fd[1]);

            if (!background) {
                wait(NULL);
                wait(NULL);
            }
            
            /* Pula o restante do loop, pois o pipe já foi tratado */
            continue;
        }

        /* Verificar Redirecionamento (> ou <) */
        int redirect_out = 0;
        int redirect_in = 0;
        char *io_file = NULL;
        int args_end = ntok; /* Onde termina os argumentos do comando */

        for (int i = 0; i < ntok; i++) {
            if (strcmp(tokens[i], ">") == 0) {
                redirect_out = 1;
                args_end = i;
                if (i + 1 < ntok) io_file = tokens[i+1];
                break;
            }
            if (strcmp(tokens[i], "<") == 0) {
                redirect_in = 1;
                args_end = i;
                if (i + 1 < ntok) io_file = tokens[i+1];
                break;
            }
        }

        /* Constrói argv considerando possível corte no redirecionamento */
        construir_argv(tokens, 0, args_end, argv);

        /* --- FIM DA IMPLEMENTAÇÃO DA LÓGICA DE PARSING --- */

        pid_t pid = fork();

        if (pid < 0) {
            perror("fork");
            continue;
        } else if (pid == 0) {
            /* PONTO 1: Redirecionamento no processo filho */
            if (redirect_out) {
                if (!io_file) {
                    fprintf(stderr, "osh: erro de sintaxe, arquivo de saida faltando\n");
                    exit(EXIT_FAILURE);
                }
                int fd = open(io_file, O_WRONLY | O_CREAT | O_TRUNC, 0644);
                if (fd < 0) {
                    perror("open output");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDOUT_FILENO);
                close(fd);
            } else if (redirect_in) {
                if (!io_file) {
                    fprintf(stderr, "osh: erro de sintaxe, arquivo de entrada faltando\n");
                    exit(EXIT_FAILURE);
                }
                int fd = open(io_file, O_RDONLY);
                if (fd < 0) {
                    perror("open input");
                    exit(EXIT_FAILURE);
                }
                dup2(fd, STDIN_FILENO);
                close(fd);
            }

            exec_command(argv);
        } else {
            /* Pai espera, a menos que seja background */
            if (!background) {
                wait(NULL);
            }
        }
    }

    return 0;
}
