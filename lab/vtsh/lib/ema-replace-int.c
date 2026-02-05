#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <fcntl.h>

#define INT_SIZE sizeof(int)

int main(int argc, char *argv[]) {
    if (argc < 4) {
        printf("Использование: %s <размер_файла_в_байтах> <искомое_значение> <значение_для_замены>\n", argv[0]);
        printf("Пример: %s 1048576 0 100\n", argv[0]);
        return 1;
    }

    size_t file_size = atol(argv[1]);
    int search_value = atoi(argv[2]);
    int replace_value = atoi(argv[3]);

    const char *filename = "search_file.bin";

    printf("=== Поиск и замена в файле ===\n");
    printf("Файл: %s\n", filename);
    printf("Размер: %zu байт\n", file_size);
    printf("Ищем значение: %d\n", search_value);
    printf("Заменяем на: %d\n", replace_value);
    printf("==============================\n\n");

    printf("1. Создание тестового файла...\n");

    int fd = open(filename, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) {
        perror("Ошибка создания файла");
        return 1;
    }

    size_t int_count = file_size / INT_SIZE;
    printf("   Заполняем %zu целых чисел...\n", int_count);

    for (size_t i = 0; i < int_count; i++) {
        int value = i % 100;
        write(fd, &value, INT_SIZE);
    }

    close(fd);
    printf("   Файл создан успешно\n\n");

    printf("2. Поиск и замена...\n");

    clock_t start = clock();

    fd = open(filename, O_RDWR);
    if (fd < 0) {
        perror("Ошибка открытия файла");
        return 1;
    }

    int *buffer = malloc(file_size);
    if (!buffer) {
        perror("Ошибка выделения памяти");
        close(fd);
        return 1;
    }

    ssize_t bytes_read = read(fd, buffer, file_size);
    if (bytes_read != file_size) {
        printf("Ошибка чтения файла\n");
        free(buffer);
        close(fd);
        return 1;
    }

    size_t found_count = 0;
    for (size_t i = 0; i < int_count; i++) {
        if (buffer[i] == search_value) {
            buffer[i] = replace_value;
            found_count++;
        }
    }

    lseek(fd, 0, SEEK_SET);
    ssize_t bytes_written = write(fd, buffer, file_size);
    if (bytes_written != file_size) {
        printf("Ошибка записи в файл\n");
    }

    free(buffer);
    close(fd);

    clock_t end = clock();
    double time_spent = (double)(end - start) / CLOCKS_PER_SEC;

    printf("\n3. Результаты:\n");
    printf("   Найдено и заменено: %zu значений\n", found_count);
    printf("   Время выполнения: %.3f секунд\n", time_spent);

    if (time_spent > 0) {
        double mb_processed = file_size / (1024.0 * 1024.0);
        printf("   Скорость обработки: %.2f МБ/сек\n", mb_processed / time_spent);
    }

    printf("\nГотово! Файл '%s' обновлен.\n", filename);
    return 0;
}