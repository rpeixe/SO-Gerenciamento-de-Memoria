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

void min_heapify(int arr[], int length, int i);

/* Implementação de página */

struct page_t {
    unsigned char referenced:1;  // Se a página foi acessada nesse tick
    unsigned char modified:1;  // Se a página foi modificada
    unsigned char present:1;  // Se a página está carregada na MR
    unsigned char pad:5;
    int mr_page;  // Posição da MR que a página se encontra
    unsigned char tick_accessed;  // Tick da última vez que a página foi acessada
};

struct page_t MV[MV_LENGTH];  // Memória virtual (tabela de páginas)

int MR[MR_LENGTH];  // Memória real (o que está carregado)
int pages_in_mr;    // Número de páginas na memória real

struct page_t create_page() {
    struct page_t page;
    page.referenced = 0;
    page.modified = 0;
    page.present = 0;
    page.mr_page = -1;
    page.tick_accessed = 0;
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
    pages_in_mr++;
}

void remove_page(int mv_index) {
    MV[mv_index].present = 0;
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
    /*
    struct page_t* chosen_page = &MV[MR[0]];
    for (mr_index = 0; mr_index < MR_LENGTH; mr_index++) {
        if (MV[MR[mr_index]].tick_accessed < chosen_page->tick_accessed) {
            chosen_page = &MV[MR[mr_index]];
        }
    }
    mr_index = chosen_page->mr_page;
    */
    mr_index = 0;
    remove_page(MR[mr_index]);
    add_page(mv_index, mr_index);
    min_heapify(MR, MR_LENGTH, 0);
    return 1;
}

/* Implementação de Heap */

void swap(int* a, int* b) {
    int temp = *a;
    int temp_page = MV[*a].mr_page;
    MV[*a].mr_page = MV[*b].mr_page;
    *a = *b;
    MV[*b].mr_page = temp_page;
    *b = temp;
}

void min_heapify(int arr[], int length, int i) {
    int left = 2*i + 1;
    int right = 2*i + 2;
    int smallest;
    if (left < length && MV[arr[left]].tick_accessed < MV[arr[i]].tick_accessed) {
        smallest = left;
    }
    else {
        smallest = i;
    }
    if (right < length && MV[arr[right]].tick_accessed < MV[arr[i]].tick_accessed) {
        smallest = right;
    }
    if (smallest != i) {
        swap(&MR[i], &MR[smallest]);
        min_heapify(arr, length, smallest);
    }
}

void build_min_heap(int arr[], int length) {
    int i;
    for (i = length/2; i >=0; i--) {
        min_heapify(arr, length, i);
    }
}

void heap_update_key(int arr[], int i) {
    while (i > 0 && MV[arr[i/2]].tick_accessed > MV[arr[i]].tick_accessed) {
        swap(&arr[i], &arr[i/2]);
        i = i/2;
    }
}

/* Testes */

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
    initialize_page_table(MV, MV_LENGTH);
    initialize_vector(MR, MR_LENGTH);
    pages_in_mr = 0;
    srand(seed);
    
    float avg = MV_LENGTH / 2;
    
    int page_misses = 0;
    int t, a;
    for (t = 0; t < TEST_TICKS; t++) {
        int p;
        for (p = 0; p < MV_LENGTH; p++) {
            // Move os bits para a direita e adiciona o bit de referenciado à esquerda
            MV[p].tick_accessed = (MV[p].tick_accessed >> 1) + (MV[p].referenced << 7);
            if (MV[p].referenced == 1 && MV[p].present == 1) {
                heap_update_key(MR, MV[p].mr_page);
            }
            
            // Reinicia o bit de referenciado de cada item da tabela
            MV[p].referenced = 0;
        }
        for (a = 0; a < TEST_ACCESSES; a++) {
            int chosen_page = round(random_normal(avg, MV_LENGTH * std_dev));
            chosen_page = abs(chosen_page) % MV_LENGTH;
            page_misses += get_page(chosen_page);
        }
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