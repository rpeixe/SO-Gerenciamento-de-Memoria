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

struct queue* sc_queue;
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

void add_page(int mv_index, int mr_index) {
    MV[mv_index].present = 1;
    MV[mv_index].referenced = 1;
    MV[mv_index].mr_page = mr_index;
    MR[mr_index] = mv_index;
    enqueue(sc_queue, create_node(&MV[mv_index]));
    pages_in_mr++;
}

void remove_page(int mv_index) {
    MV[mv_index].present = 0;
    struct q_node* node = dequeue(sc_queue);
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
    struct q_node* current = sc_queue->head;
    while(current->value->referenced == 1) {
        mr_index = current->value->mr_page;
        remove_page(MR[mr_index]);
        add_page(MR[mr_index], mr_index);
        current->value->referenced = 0;
        current = sc_queue->head;
    }
    mr_index = current->value->mr_page;
    remove_page(MR[mr_index]);
    add_page(mv_index, mr_index);
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
    sc_queue = create_queue();
    
    float avg = MV_LENGTH / 2;

    int page_misses = 0;
    int t, a;
    for (t = 0; t < TEST_TICKS; t++) {
        int p;
        for (p = 0; p < MV_LENGTH; p++) {
            // Reinicia o bit de referenciado de cada item da tabela
            MV[p].referenced = 0;
        }
        for (a = 0; a < TEST_ACCESSES; a++) {
            int chosen_page = round(random_normal(avg, MV_LENGTH * std_dev));
            chosen_page = abs(chosen_page) % MV_LENGTH;
            page_misses += get_page(chosen_page);
        }
    }
    
    free(sc_queue);

    return page_misses;
}


int main(void) {
    int t, std_dev, seed = 0;
    for (std_dev = 10; std_dev <= 30; std_dev += 5) {
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