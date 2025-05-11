
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <pthread.h>
#include <errno.h>
#include <unistd.h>

#ifdef _WIN32
#include <windows.h>
#else
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#endif


typedef struct {
    unsigned int seed;   // Начальное значение
    unsigned int a;      // Множитель
    unsigned int c;      // Приращение
    unsigned int m;      // Модуль
    unsigned int* sequence; // Указатель на генерируемую последовательность
    size_t size;         // Размер последовательности
} lcg_params_t;

typedef struct {
    pthread_barrier_t* barrier;   // Указатель на барьер синхронизации
    const unsigned char* text_part; // Часть текста для обработки
    const unsigned int* pad_part; // Часть одноразового блокнота
    unsigned char* result_part;   // Буфер для результата
    size_t part_size;             // Размер обрабатываемого блока
} worker_context_t;

unsigned int get_cpu_cores() {
#ifdef _WIN32
    SYSTEM_INFO sysinfo;
    GetSystemInfo(&sysinfo);
    return sysinfo.dwNumberOfProcessors;
#else
    long cores = sysconf(_SC_NPROCESSORS_ONLN);
    if (cores <= 0) {
        fprintf(stderr, "Warning: Using single core mode\n");
        return 1;
    }
    return (unsigned int)cores;
#endif
}


int load_file(const char* path, unsigned char** buffer, size_t* file_size) {
#ifdef _WIN32
    FILE* file = fopen(path, "rb");
    if (!file) {
        perror("Failed to open input file");
        return -1;
    }

    fseek(file, 0, SEEK_END);
    *file_size = ftell(file);
    fseek(file, 0, SEEK_SET);

    *buffer = (unsigned char*)malloc(*file_size);
    if (!*buffer) {
        perror("Failed to allocate memory for file");
        fclose(file);
        return -1;
    }

    if (fread(*buffer, 1, *file_size, file) != *file_size) {
        perror("Failed to read file");
        free(*buffer);
        fclose(file);
        return -1;
    }

    fclose(file);
    return 0;
#else
    int fd = open(path, O_RDONLY);
    if (fd == -1) {
        perror("Failed to open input file");
        return -1;
    }

    struct stat st;
    if (fstat(fd, &st) == -1) {
        perror("Failed to get file size");
        close(fd);
        return -1;
    }

    *file_size = st.st_size;
    *buffer = (unsigned char*)mmap(NULL, *file_size, PROT_READ, MAP_PRIVATE, fd, 0);
    close(fd);

    if (*buffer == MAP_FAILED) {
        perror("Failed to mmap input file");
        return -1;
    }
    return 0;
#endif
}

void free_file(unsigned char* buffer, size_t size) {
#ifdef _WIN32
    free(buffer);  
#else
    munmap(buffer, size);
#endif
}

void* lcg_thread(void* arg) {
    lcg_params_t* params = (lcg_params_t*)arg;
    unsigned int x = params->seed;
    
    for (size_t i = 0; i < params->size; i++) {
        params->sequence[i] = x;
        x = (params->a * x + params->c) % params->m;
    }
    
    return NULL;
}

// Функция рабочего потока для шифрования/дешифрования
void* worker_thread(void* arg) {
    worker_context_t* context = (worker_context_t*)arg;
    
    // Побитовое XOR
    for (size_t i = 0; i < context->part_size; i++) {
        context->result_part[i] = context->text_part[i] ^ (context->pad_part[i] & 0xFF);
    }
    
    // Ожидание на барьере синхронизации
    pthread_barrier_wait(context->barrier);
    
    return NULL;
}

int main(int argc, char* argv[]) {
    int opt;
    char* input_path = NULL;
    char* output_path = NULL;
    unsigned int x = 0, a = 0, c = 0, m = 0;
    
    while ((opt = getopt(argc, argv, "i:o:x:a:c:m:")) != -1) {
        switch (opt) {
            case 'i': input_path = optarg; break;
            case 'o': output_path = optarg; break;
            case 'x': x = atoi(optarg); break;
            case 'a': a = atoi(optarg); break;
            case 'c': c = atoi(optarg); break;
            case 'm': m = atoi(optarg); break;
            default:
                fprintf(stderr, "Usage: %s -i input -o output -x seed -a multiplier -c increment -m modulus\n", argv[0]);
                return EXIT_FAILURE;
        }
    }
    
    if (!input_path || !output_path || !m) {
        fprintf(stderr, "Missing required arguments\n");
        return EXIT_FAILURE;
    }
    
    unsigned char* text = NULL;
    size_t file_size;
    if (load_file(input_path, &text, &file_size) {
        return EXIT_FAILURE;
    }
    
    // Определение количества рабочих потоков
    unsigned int num_workers = get_cpu_cores();
    printf("Using %u worker threads\n", num_workers);
    
    // Выделение памяти
    unsigned int* pad = (unsigned int*)malloc(file_size * sizeof(unsigned int));
    if (!pad) {
        perror("Failed to allocate memory for pad");
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Настройка параметров ЛКГ
    lcg_params_t lcg_params = {
        .seed = x,
        .a = a,
        .c = c,
        .m = m,
        .sequence = pad,
        .size = file_size
    };
    
    // Создание потока для генерации
    pthread_t lcg_thread_id;
    if (pthread_create(&lcg_thread_id, NULL, lcg_thread, &lcg_params) != 0) {
        perror("Failed to create LCG thread");
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Ожидание завершения генерации
    if (pthread_join(lcg_thread_id, NULL) != 0) {
        perror("Failed to join LCG thread");
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Инициализация барьера синхронизации
    pthread_barrier_t barrier;
    if (pthread_barrier_init(&barrier, NULL, num_workers + 1) != 0) {
        perror("Failed to initialize barrier");
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Выделение памяти для результата
    unsigned char* result = (unsigned char*)malloc(file_size);
    if (!result) {
        perror("Failed to allocate memory for result");
        pthread_barrier_destroy(&barrier);
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Распределение работы между потоками
    size_t chunk_size = file_size / num_workers;
    size_t remainder = file_size % num_workers;
    
    pthread_t* worker_threads = (pthread_t*)malloc(num_workers * sizeof(pthread_t));
    worker_context_t* contexts = (worker_context_t*)malloc(num_workers * sizeof(worker_context_t));
    
    if (!worker_threads || !contexts) {
        perror("Failed to allocate memory for worker threads");
        if (worker_threads) free(worker_threads);
        if (contexts) free(contexts);
        pthread_barrier_destroy(&barrier);
        free(result);
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    // Создание и запуск рабочих потоков
    size_t offset = 0;
    for (unsigned int i = 0; i < num_workers; i++) {
        size_t current_chunk = chunk_size + (i < remainder ? 1 : 0);
        
        contexts[i] = (worker_context_t){
            .barrier = &barrier,
            .text_part = text + offset,
            .pad_part = pad + offset,
            .result_part = result + offset,
            .part_size = current_chunk
        };
        
        if (pthread_create(&worker_threads[i], NULL, worker_thread, &contexts[i]) != 0) {
            perror("Failed to create worker thread");
            // Очистка уже созданных потоков
            for (unsigned int j = 0; j < i; j++) {
                pthread_join(worker_threads[j], NULL);
            }
            free(worker_threads);
            free(contexts);
            pthread_barrier_destroy(&barrier);
            free(result);
            free(pad);
            free_file(text, file_size);
            return EXIT_FAILURE;
        }
        
        offset += current_chunk;
    }
    
    // Главный поток ожидает на барьере
    pthread_barrier_wait(&barrier);
    
    // Ожидание завершения всех рабочих потоков
    for (unsigned int i = 0; i < num_workers; i++) {
        pthread_join(worker_threads[i], NULL);
    }
    
    // Запись результата в выходной файл
    FILE* output_file = fopen(output_path, "wb");
    if (!output_file) {
        perror("Failed to open output file");
        free(worker_threads);
        free(contexts);
        pthread_barrier_destroy(&barrier);
        free(result);
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    if (fwrite(result, 1, file_size, output_file) != file_size) {
        perror("Failed to write output file");
        fclose(output_file);
        free(worker_threads);
        free(contexts);
        pthread_barrier_destroy(&barrier);
        free(result);
        free(pad);
        free_file(text, file_size);
        return EXIT_FAILURE;
    }
    
    fclose(output_file);
    
    // Освобождение ресурсов
    free(worker_threads);
    free(contexts);
    pthread_barrier_destroy(&barrier);
    free(result);
    free(pad);
    free_file(text, file_size);
    
    printf("Operation completed successfully\n");
    return EXIT_SUCCESS;
}
