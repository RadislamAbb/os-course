#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <time.h>
#include <sys/time.h>
#include <unistd.h>
#include <errno.h>
#include <fcntl.h>
#include <sys/stat.h>

// Попытка включить vtpc.h, если доступен
#ifdef USE_VTPC
#include "../../vtpc/lib/vtpc.h"
#else
// Заглушки для vtpc функций, если библиотека недоступна
// Используем стандартные системные вызовы
static int vtpc_open_wrapper(const char* path, int mode, int access) {
    (void)access;  // access не используется в стандартном open
    return open(path, mode, 0644);
}
#define vtpc_open vtpc_open_wrapper
#define vtpc_close close
#define vtpc_read read
#define vtpc_write write
#define vtpc_lseek lseek
#endif

#ifndef INT_SIZE
#define INT_SIZE sizeof(int)
#endif

typedef struct config_t {
    int repetitions;
    size_t block_count;
    char access_type[32];
    int search_value;      // Значение для поиска
    int replace_value;     // Значение для замены
} config_t;

int run_ema_replace_int(const config_t *config, int fd, off_t file_size) {
    struct timeval start_time, end_time;
    gettimeofday(&start_time, NULL);

    srand(time(NULL));

    size_t total_ops_processed = 0;
    size_t total_bytes_processed = 0;
    size_t int_count = file_size / INT_SIZE;
    int found_count = 0;

    printf("Запуск EMA-Replace-Int.\n");
    printf("  Элементов в файле: %zu\n", int_count);
    printf("  Ищем значение: %d\n", config->search_value);
    printf("  Заменяем на: %d\n", config->replace_value);
    printf("  Тип доступа: %s\n", config->access_type);

    if (strcmp(config->access_type, "sequential") == 0) {
        size_t elements_to_process = (config->block_count > 0) ? config->block_count : int_count;
        if (elements_to_process > int_count) {
            elements_to_process = int_count;
        }

        int *buffer = malloc(file_size);
        if (!buffer) {
            perror("Ошибка выделения памяти");
            return -1;
        }

        for (int rep = 0; rep < config->repetitions; rep++) {
            if (config->repetitions > 1) {
                printf("Повторение %d/%d\n", rep + 1, config->repetitions);
            }

            if (vtpc_lseek(fd, 0, SEEK_SET) == (off_t)-1) {
                perror("Ошибка vtpc_lseek");
                free(buffer);
                return -1;
            }

            ssize_t read_result = vtpc_read(fd, buffer, file_size);
            if (read_result != (ssize_t)file_size) {
                fprintf(stderr, "Ошибка чтения файла: %zd из %lld\n", read_result, (long long)file_size);
                free(buffer);
                return -1;
            }

            total_bytes_processed += file_size;

            for (size_t i = 0; i < elements_to_process; i++) {
                if (buffer[i] == config->search_value) {
                    buffer[i] = config->replace_value;
                    found_count++;
                    total_ops_processed++;
                }
            }

            if (vtpc_lseek(fd, 0, SEEK_SET) == (off_t)-1) {
                perror("Ошибка vtpc_lseek перед записью");
                free(buffer);
                return -1;
            }

            ssize_t write_result = vtpc_write(fd, buffer, file_size);
            if (write_result != (ssize_t)file_size) {
                fprintf(stderr, "Ошибка записи файла: %zd из %lld\n", write_result, (long long)file_size);
                free(buffer);
                return -1;
            }

            total_bytes_processed += file_size;
        }

        free(buffer);
    } else {
        for (int rep = 0; rep < config->repetitions; rep++) {
            printf("Повторение %d/%d\n", rep + 1, config->repetitions);

            for (size_t op = 0; op < config->block_count; op++) {
                off_t offset;
                int value;
                ssize_t result;

                size_t rand_index = rand() % int_count;
                offset = rand_index * INT_SIZE;

                if (offset + (off_t)INT_SIZE > file_size) {
                    fprintf(stderr, "Ошибка: выход за границы файла\n");
                    continue;
                }

                if (vtpc_lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                    perror("Ошибка vtpc_lseek перед Read");
                    continue;
                }

                result = vtpc_read(fd, &value, INT_SIZE);
                if (result != (ssize_t)INT_SIZE) {
                    printf("Ошибка vtpc_read: ожидалось %zu, получено %zd\n",
                           INT_SIZE, result);
                    continue;
                }

                total_bytes_processed += INT_SIZE;

                if (value == config->search_value) {
                    found_count++;

                    if (vtpc_lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                        perror("Ошибка vtpc_lseek перед Write");
                        continue;
                    }

                    result = vtpc_write(fd, &config->replace_value, INT_SIZE);
                    if (result != (ssize_t)INT_SIZE) {
                        printf("Ошибка vtpc_write: ожидалось %zu, получено %zd\n",
                               INT_SIZE, result);
                        continue;
                    }

                    total_bytes_processed += INT_SIZE;
                    total_ops_processed++;
                }
            }
        }
    }

    gettimeofday(&end_time, NULL);
    double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                     (end_time.tv_usec - start_time.tv_usec) / 1000000.0;

    printf("\nРезультаты EMA-Replace-Int:\n");
    printf("  Всего операций замены: %zu\n", total_ops_processed);
    printf("  Найдено соответствий: %d\n", found_count);
    printf("  Всего обработано байт: %zu (%.2f MB)\n",
           total_bytes_processed, total_bytes_processed / (1024.0 * 1024.0));
    printf("  Время выполнения: %.3f сек\n", elapsed);

    if (elapsed > 0) {
        printf("  Пропускная способность: %.2f MB/сек\n",
               total_bytes_processed / (1024.0 * 1024.0 * elapsed));
        printf("  IOPS: %.2f операций/сек\n", total_ops_processed / elapsed);
    }

    return 0;
}

int main(int argc, char *argv[]) {
    const char *filename = "test_file.bin";
    int repetitions = 1;
    size_t block_count = 100;
    char access_type[32] = "random";
    off_t file_size = 1024 * 1024;
    int search_value = 0;
    int replace_value = 1000;
    bool init_random = false;

    if (argc > 1) filename = argv[1];
    if (argc > 2) repetitions = atoi(argv[2]);
    if (argc > 3) block_count = (size_t)atoi(argv[3]);
    if (argc > 4) {
        strncpy(access_type, argv[4], sizeof(access_type) - 1);
        access_type[sizeof(access_type) - 1] = '\0';
    }
    if (argc > 5) file_size = (off_t)atoll(argv[5]);
    if (argc > 6) search_value = atoi(argv[6]);
    if (argc > 7) replace_value = atoi(argv[7]);
    if (argc > 8) init_random = (strcmp(argv[8], "random") == 0 || strcmp(argv[8], "1") == 0);

    config_t config;
    config.repetitions = repetitions;
    config.block_count = block_count;
    strncpy(config.access_type, access_type, sizeof(config.access_type) - 1);
    config.access_type[sizeof(config.access_type) - 1] = '\0';
    config.search_value = search_value;
    config.replace_value = replace_value;

    printf("EMA-Replace-Int Loader\n");
    printf("======================\n");
    printf("Файл: %s\n", filename);
    printf("Повторений: %d\n", config.repetitions);
    printf("Блоков на повторение: %zu\n", config.block_count);
    printf("Тип доступа: %s\n", config.access_type);
    printf("Размер файла: %lld байт\n", (long long)file_size);
    printf("Ищем значение: %d\n", config.search_value);
    printf("Заменяем на: %d\n", config.replace_value);
    printf("Инициализация случайными значениями: %s\n", init_random ? "да" : "нет");
    printf("\n");

    // Открываем файл
    int fd = vtpc_open(filename, O_RDWR | O_CREAT, 0644);
    if (fd < 0) {
        perror("Ошибка открытия файла");
        fprintf(stderr, "Использование: %s [файл] [повторений] [блоков] [random|sequential] [размер_файла] [search_value] [replace_value] [init_random]\n", 
                argv[0]);
        return 1;
    }

    struct stat st;
    if (fstat(fd, &st) == 0 && st.st_size < file_size) {
        printf("Инициализация файла до размера %lld байт...\n", (long long)file_size);
        if (ftruncate(fd, file_size) != 0) {
            perror("Ошибка установки размера файла");
            vtpc_close(fd);
            return 1;
        }
        
        if (init_random) {
            printf("Заполнение файла случайными значениями...\n");
            unsigned int seed = (unsigned int)time(NULL);
            int *buffer = malloc(INT_SIZE);
            if (buffer == NULL) {
                fprintf(stderr, "Ошибка выделения памяти для буфера\n");
                vtpc_close(fd);
                return 1;
            }
            
            size_t int_count = file_size / INT_SIZE;
            for (size_t i = 0; i < int_count; i++) {
                *buffer = rand_r(&seed) % 2000;
                off_t offset = i * INT_SIZE;
                if (vtpc_lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                    perror("Ошибка vtpc_lseek при инициализации");
                    free(buffer);
                    vtpc_close(fd);
                    return 1;
                }
                ssize_t written = vtpc_write(fd, buffer, INT_SIZE);
                if (written != (ssize_t)INT_SIZE) {
                    fprintf(stderr, "Ошибка записи при инициализации\n");
                    free(buffer);
                    vtpc_close(fd);
                    return 1;
                }
            }
            free(buffer);
            printf("Файл инициализирован случайными значениями.\n");
        } else {
            printf("Заполнение файла нулями...\n");
            int zero = 0;
            size_t int_count = file_size / INT_SIZE;
            for (size_t i = 0; i < int_count; i++) {
                off_t offset = i * INT_SIZE;
                if (vtpc_lseek(fd, offset, SEEK_SET) == (off_t)-1) {
                    perror("Ошибка vtpc_lseek при инициализации");
                    vtpc_close(fd);
                    return 1;
                }
                ssize_t written = vtpc_write(fd, &zero, INT_SIZE);
                if (written != (ssize_t)INT_SIZE) {
                    fprintf(stderr, "Ошибка записи при инициализации\n");
                    vtpc_close(fd);
                    return 1;
                }
            }
        }
    }

    int result = run_ema_replace_int(&config, fd, file_size);

    vtpc_close(fd);

    if (result != 0) {
        fprintf(stderr, "Ошибка выполнения теста\n");
        return 1;
    }

    printf("\nТест завершен успешно.\n");
    return 0;
}