#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <math.h>

/*
 * Tamanho da memória real em bytes 
 * 512 MB de RAM, 131.072 págs na MR e 262.144 págs na MV
 */
#define RAM_SIZE 536870912
/* 
 * Tamanho da página em bytes
 * Páginas de 4 KB
 */
#define PAGE_SIZE 4096
#define MR_LENGTH (RAM_SIZE / PAGE_SIZE)
#define MV_LENGTH (2 * MR_LENGTH)
#define MS_LENGTH (MV_LENGTH - MR_LENGTH)
/* Número de vezes para repetir cada teste */
#define TEST_NR 5
/*
 * Número de instantes (interrupções de tempo) em cada teste
 * Assumindo um clock de 10 ms, 500 instantes equivalem a 5 segundos
 */
#define TEST_TICKS 500
/*
 * Número de acessos à memória em cada instante
 * Assumindo uma memória de 100 MHz, o que equivale a 200 MT/s
 * 200.000.000 operações por segundo, dividido por 100 interrupções por segundo,
 * equivale a 2.000.000 operações por interrupção
 */
#define TEST_ACCESSES 2000000

/* Implementação de fila */

struct q_node {
    struct page_t* value;
    struct q_node* next;
};

struct queue {
    struct q_node* head;
    struct q_node* tail;
};

struct q_node* create_node(struct page_t* page) {
    struct q_node* node;
    node = (struct q_node*) malloc(sizeof(struct q_node));
    node->value = page;
    node->next = NULL;
    return node;
}

struct queue* create_queue() {
    struct queue* queue;
    queue = (struct queue*) malloc(sizeof(struct queue));
    queue->head = NULL;
    queue->tail = NULL;
    return queue;
}

void enqueue(struct queue* queue, struct q_node* node) {
    if (queue->tail) {
        queue->tail->next = node;
    }
    if (!queue->head) {
        queue->head = node;
    }
    queue->tail = node;
}
/*
int queue_length(struct queue* queue) {
    int result = 0;
    struct q_node* current = queue->head;
    while (current) {
        result++;
        current = current->next;
    }
    return result;
}
*/
struct q_node* dequeue(struct queue* queue) {
    if (!queue->head) {
        return NULL;
    }
    struct q_node* head = queue->head;
    queue->head = head->next;
    if (queue->head == NULL) {
        queue->tail = NULL;
    }
    head->next = NULL;
    return head;
}

/* Implementação de página */

struct page_t {
    unsigned char referenced:1;  // Se a página foi acessada nesse tick
    unsigned char modified:1;  // Se a página foi modificada
    unsigned char present:1;  // Se a página está carregada na MR
    unsigned char pad:5;
    int mr_page;  // Posição da MR que a página se encontra
    unsigned char tick_accessed;  // Tick da última vez que a página foi acessada
};

struct queue* nru_queues[4];
struct page_t MV[MV_LENGTH];  // Memória virtual (tabela de páginas)

int MR[MR_LENGTH];  // Memória real (o que está carregado)
int pages_in_mr;    // Número de páginas na memória real

struct page_t create_page() {
    struct page_t page;
    page.referenced = 0;
    page.modified = 0;
    page.present = 0;
    page.mr_page = -1;
    page.tick_accessed = -1;
    return page;
}

void initialize_page_table(struct page_t* table, int length) {
    int i;
    for (i = 0; i < length; i++) {
        table[i] = create_page();
    }
}

void initialize_vector(int* v, int length) {
    int i;
    for(i = 0; i < length; i++) {
        v[i] = -1;
    }
}

/*
Prioridades:
0: R=0 e M=0
1: R=0 e M=1
2: R=1 e M=0
3: R=1 e M=1
*/

void add_page(int mv_index, int mr_index) {
    MV[mv_index].present = 1;
    MV[mv_index].referenced = 1;
    MV[mv_index].mr_page = mr_index;
    MR[mr_index] = mv_index;
    int queue_index = 2;
    if (MV[mv_index].modified == 1) {
        queue_index++;
    }
    enqueue(nru_queues[queue_index], create_node(&MV[mv_index]));
    pages_in_mr++;
}

void remove_page(int mv_index, int queue_index) {
    MV[mv_index].present = 0;
    struct q_node* node = dequeue(nru_queues[queue_index]);
    free(node);
    pages_in_mr--;
}

int get_page(int mv_index) {
    if (MV[mv_index].present) {
        // Presente
        MV[mv_index].referenced = 1;
        return 0;
    }
    // Page miss
    int mr_index;
    if (pages_in_mr < MR_LENGTH) {
        for (mr_index = 0; mr_index < MR_LENGTH; mr_index++) {
            // Checa se tem posição vazia na memória
            if (MR[mr_index] == -1) {
                add_page(mv_index, mr_index);
                return 1;
            }
        }
    }
    
    int queue_index;
    for (queue_index = 0; queue_index < 4; queue_index++) {
        struct q_node* current = nru_queues[queue_index]->head;
        if (current) {
            mr_index = current->value->mr_page;
            remove_page(MR[mr_index], queue_index);
            add_page(mv_index, mr_index);
            break;
        }
    }
    return 1;
}

double random_normal(double mu, double sigma) {
    static int alternate = 0;
    static double stored_val;
    double u1, u2, z0;

    u1 = (rand() + 1.0) / (RAND_MAX + 2.0);
    u2 = (rand() + 1.0) / (RAND_MAX + 2.0);

    if (alternate == 0) {
        z0 = sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
    } else {
        z0 = sqrt(-2.0 * log(stored_val)) * sin(2.0 * M_PI * stored_val);
    }

    alternate = 1 - alternate;
    stored_val = u1;

    return mu + sigma * z0;
}

int test(float std_dev, int seed) {
    srand(seed);
    initialize_page_table(MV, MV_LENGTH);
    initialize_vector(MR, MR_LENGTH);
    pages_in_mr = 0;
    
    int i;
    for (i=0; i<4; i++) {
        nru_queues[i] = create_queue();
    }
    
    float avg = MV_LENGTH / 2;
    
    int page_misses = 0;
    int t, a;
    for (t = 0; t < TEST_TICKS; t++) {
        for (i=2; i<4; i++) {
            struct q_node* node;
            while((node = dequeue(nru_queues[i]))) {
                node->value->referenced = 0;
                enqueue(nru_queues[i-2], node);
            }
        }
        for (a = 0; a < TEST_ACCESSES; a++) {
            int chosen_page = round(random_normal(avg, MV_LENGTH * std_dev));
            chosen_page = abs(chosen_page) % MV_LENGTH;
            page_misses += get_page(chosen_page);
        }
    }
    //printf("Tamanho das filas:\n0: %d\n1: %d\n2: %d\n3: %d\n", queue_length(nru_queues[0]), queue_length(nru_queues[1]), queue_length(nru_queues[2]), queue_length(nru_queues[3]));

    for (i=0; i<4; i++) {
        free(nru_queues[i]);
    }

    return page_misses;
}


int main(void) {
    int t, std_dev, seed = 0;
    for (std_dev = 10; std_dev <= 50; std_dev += 10) {
        int total = 0;
        clock_t start, end;
        double time_elapsed;
        start = clock();
        for (t = 0; t < TEST_NR; t++, seed++) {
            int page_misses = test((float) std_dev/100, seed);
            total += page_misses;
            printf("Desvio padrão %d%% - Teste %d: %d page misses\n", std_dev, t, page_misses);
        }
        end = clock();
        time_elapsed = ((double) (end - start)) / CLOCKS_PER_SEC;
        printf("Total: %d, Média: %.2f, Tempo médio: %f\n\n", total, (float) total / TEST_NR, time_elapsed / TEST_NR);
    }
    return 0;
}