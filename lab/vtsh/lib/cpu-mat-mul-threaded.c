#include <errno.h>
#include <getopt.h>
#include <pthread.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#define DEFAULT_SEED 52
#define BASE_10 10
#define SHIFT_32 32
#define BUFF_SIZE 64

typedef struct {
  uint64_t width1;
  uint64_t height1;
  uint64_t width2;
  uint64_t height2;
  uint64_t seed;
  bool has_seed;
  char* in_file_1;
  bool has_in_file_1;
  char* in_file_2;
  bool has_in_file_2;
  char* out_file;
  bool has_out_file;
  int num_threads;
} cpu_options;

struct thread_data {
  int64_t** matrix_A;
  int64_t** matrix_B;
  int64_t** matrix_C;
  uint64_t height1;
  uint64_t width1;
  uint64_t width2;
  int thread_id;
  int num_threads;
  uint64_t start_row;
  uint64_t end_row;
};

static bool parse_uint64(const char* str, uint64_t* out) {
  char* endptr = NULL;
  errno = 0;
  unsigned long long val = strtoull(str, &endptr, BASE_10);

  if (errno != 0 || *endptr != '\0') {
    (void)fprintf(stderr, "Incorrect input for uint64\n");
    return false;
  }
  *out = (uint64_t)val;
  return true;
}

static bool parse_one_option(
    const char* opt_name, cpu_options* opts, const char* value
) {
  if (strcmp(opt_name, "height1") == 0) {
    return parse_uint64(value, &opts->height1);
  }
  if (strcmp(opt_name, "width1") == 0) {
    return parse_uint64(value, &opts->width1);
  }
  if (strcmp(opt_name, "height2") == 0) {
    return parse_uint64(value, &opts->height2);
  }
  if (strcmp(opt_name, "width2") == 0) {
    return parse_uint64(value, &opts->width2);
  }
  if (strcmp(opt_name, "seed") == 0) {
    opts->has_seed = true;
    return parse_uint64(value, &opts->seed);
  }
  if (strcmp(opt_name, "in_file_1") == 0) {
    opts->has_in_file_1 = true;
    opts->in_file_1 = (char*)value;
    return true;
  }
  if (strcmp(opt_name, "in_file_2") == 0) {
    opts->has_in_file_2 = true;
    opts->in_file_2 = (char*)value;
    return true;
  }
  if (strcmp(opt_name, "out_file") == 0) {
    opts->has_out_file = true;
    opts->out_file = (char*)value;
    return true;
  }
  if (strcmp(opt_name, "threads") == 0) {
    int threads = atoi(value);
    if (threads <= 0) {
      fprintf(stderr, "Error: Number of threads must be positive\n");
      return false;
    }
    opts->num_threads = threads;
    return true;
  }

  (void)fprintf(stderr, "Error: Unknown option.\n");
  return false;
}

static bool parse_args(int argc, char** argv, cpu_options* opts) {
  opts->width1 = 0;
  opts->height1 = 0;
  opts->width2 = 0;
  opts->height2 = 0;
  opts->seed = DEFAULT_SEED;
  opts->has_seed = false;
  opts->in_file_1 = NULL;
  opts->has_in_file_1 = false;
  opts->in_file_2 = NULL;
  opts->has_in_file_2 = false;
  opts->out_file = NULL;
  opts->has_out_file = false;
  opts->num_threads = 1;

  for (int i = 1; i < argc; i++) {
    if (strncmp(argv[i], "--", 2) != 0) {
      (void)fprintf(stderr, "Error: Unknown option.\n");
      return false;
    }

    char* option_name = argv[i] + 2;
    char* value = strchr(option_name, '=');

    if (value) {
      *value = '\0';
      value++;
    } else if (i + 1 < argc && strncmp(argv[i + 1], "--", 2) != 0) {
      value = argv[i + 1];
      i++;
    } else {
      (void)fprintf(stderr, "Error: No value provided for option.\n");
      return false;
    }

    if (!parse_one_option(option_name, opts, value)) {
      return false;
    }
  }

  return true;
}

static bool check_args(const cpu_options* opts) {
  if (opts->width1 == 0 || opts->height1 == 0 || opts->width2 == 0 ||
      opts->height2 == 0) {
    (void)fprintf(stderr, "Error: All matrix dimensions must be specified.\n");
    return false;
  }

  if (opts->width1 != opts->height2) {
    (void)fprintf(
        stderr,
        "Error: width1 (%llu) must equal height2 (%llu) for matrix "
        "multiplication.\n",
        (unsigned long long)opts->width1, (unsigned long long)opts->height2
    );
    return false;
  }

  return true;
}

static int64_t** read_matrix_from_file(
    const char* filename, uint64_t height, int64_t** matrix, uint64_t width
) {
  FILE* file = fopen(filename, "r");
  if (!file) {
    perror("Error opening file for reading");
    return NULL;
  }

  uint64_t file_height, file_width;
  if (fscanf(file, "%llu %llu", (unsigned long long*)&file_height,
             (unsigned long long*)&file_width) != 2) {
    (void)fprintf(stderr, "Error reading matrix dimensions from file.\n");
    (void)fclose(file);
    return NULL;
  }

  if (file_height != height || file_width != width) {
    (void)fprintf(
        stderr,
        "Error: Matrix dimensions in file (%llu x %llu) do not match "
        "specified dimensions (%llu x %llu).\n",
        (unsigned long long)file_height, (unsigned long long)file_width,
        (unsigned long long)height, (unsigned long long)width
    );
    (void)fclose(file);
    return NULL;
  }

  for (uint64_t i = 0; i < height; ++i) {
    for (uint64_t j = 0; j < width; ++j) {
      if (fscanf(file, "%lld", (long long*)&matrix[i][j]) != 1) {
        (void)fprintf(stderr, "Error reading matrix element at [%llu][%llu].\n",
                      (unsigned long long)i, (unsigned long long)j);
        (void)fclose(file);
        return NULL;
      }
    }
  }

  (void)fclose(file);
  return matrix;
}

static bool write_matrix_to_file(
    const char* filename, uint64_t height, int64_t** matrix, uint64_t width
) {
  FILE* file = fopen(filename, "w");
  if (!file) {
    perror("Error opening file for writing");
    return false;
  }

  (void)fprintf(
      file, "%llu %llu\n", (unsigned long long)height, (unsigned long long)width
  );

  for (uint64_t i = 0; i < height; ++i) {
    for (uint64_t j = 0; j < width; ++j) {
      (void)fprintf(file, "%lld ", (long long)matrix[i][j]);
    }
    (void)fprintf(file, "\n");
  }

  (void)fclose(file);
  return true;
}

static bool generate_matrix(
    uint64_t height, int64_t*** matrix, uint64_t width
) {
  *matrix = malloc(height * sizeof(int64_t*));
  if (!(*matrix)) {
    return false;
  }

  for (size_t i = 0; i < height; i++) {
    (*matrix)[i] = malloc(width * sizeof(int64_t));
    if (!(*matrix)[i]) {
      return false;
    }
  }
  return true;
}

static int64_t random_int64(unsigned int* seed) {
  uint64_t part1 = (uint64_t)rand_r(seed);
  uint64_t part2 = (uint64_t)rand_r(seed);
  uint64_t random_val = (part1 << SHIFT_32) | part2;
  return (int64_t)random_val;
}

static bool fill_matrix_with_random(
    uint64_t height, int64_t** matrix, uint64_t width, unsigned int* seed
) {
  if (!matrix) {
    (void)fprintf(stderr, "Error: Cannot fill a NULL matrix.\n");
    return false;
  }

  for (uint64_t i = 0; i < height; ++i) {
    if (!matrix[i]) {
      (void)fprintf(stderr, "Error: Cannot fill a NULL row in the matrix.\n");
      return false;
    }
    for (uint64_t j = 0; j < width; ++j) {
      matrix[i][j] = random_int64(seed);
    }
  }

  return true;
}

static void free_matrix(uint64_t height, int64_t** matrix) {
  if (!matrix) {
    return;
  }

  for (uint64_t i = 0; i < height; i++) {
    free(matrix[i]);
  }

  free(matrix);
}

void* compute_matrix_part(void* arg) {
  struct thread_data* data = (struct thread_data*)arg;
  
  for (uint64_t i = data->start_row; i < data->end_row; i++) {
    for (uint64_t j = 0; j < data->width2; j++) {
      int64_t sum = 0;
      for (uint64_t k = 0; k < data->width1; k++) {
        sum += data->matrix_A[i][k] * data->matrix_B[k][j];
      }
      data->matrix_C[i][j] = sum;
    }
  }
  
  return NULL;
}

static bool run_computation_threaded(cpu_options* opts, int64_t** matrix_A, 
                                     int64_t** matrix_B, int64_t** matrix_C) {
  int num_threads = opts->num_threads;
  if (num_threads > (int)opts->height1) {
    num_threads = (int)opts->height1;
  }
  
  pthread_t* threads = malloc(num_threads * sizeof(pthread_t));
  struct thread_data* thread_args = malloc(num_threads * sizeof(struct thread_data));
  
  if (!threads || !thread_args) {
    fprintf(stderr, "Error: Memory allocation failed\n");
    free(threads);
    free(thread_args);
    return false;
  }
  
  uint64_t rows_per_thread = opts->height1 / num_threads;
  uint64_t remainder = opts->height1 % num_threads;
  
  for (int i = 0; i < num_threads; i++) {
    thread_args[i].matrix_A = matrix_A;
    thread_args[i].matrix_B = matrix_B;
    thread_args[i].matrix_C = matrix_C;
    thread_args[i].height1 = opts->height1;
    thread_args[i].width1 = opts->width1;
    thread_args[i].width2 = opts->width2;
    thread_args[i].thread_id = i;
    thread_args[i].num_threads = num_threads;
    thread_args[i].start_row = i * rows_per_thread;
    thread_args[i].end_row = (i + 1) * rows_per_thread;
    if (i == num_threads - 1) {
      thread_args[i].end_row += remainder;
    }
    
    if (pthread_create(&threads[i], NULL, compute_matrix_part, &thread_args[i]) != 0) {
      fprintf(stderr, "Error: Failed to create thread %d\n", i);
      free(threads);
      free(thread_args);
      return false;
    }
  }
  
  for (int i = 0; i < num_threads; i++) {
    pthread_join(threads[i], NULL);
  }
  
  free(threads);
  free(thread_args);
  return true;
}

static int64_t** create_and_fill_matrix(
    uint64_t height,
    uint64_t width,
    bool has_file,
    const char* filename,
    unsigned int seed
) {
  int64_t** matrix = NULL;

  if (!generate_matrix(height, &matrix, width)) {
    return NULL;
  }

  if (has_file) {
    if (!read_matrix_from_file(filename, height, matrix, width)) {
      free_matrix(height, matrix);
      return NULL;
    }
  } else {
    unsigned int current_seed = seed;
    if (!fill_matrix_with_random(height, matrix, width, &current_seed)) {
      free_matrix(height, matrix);
      return NULL;
    }
  }

  return matrix;
}

int main(int argc, char** argv) {
  cpu_options options;
  int64_t** first = NULL;
  int64_t** second = NULL;
  int64_t** result = NULL;
  int exit_code = EXIT_SUCCESS;
  struct timeval start_time, end_time;

  if (!parse_args(argc, argv, &options) || !check_args(&options)) {
    return EXIT_FAILURE;
  }

  gettimeofday(&start_time, NULL);

  unsigned int seed1 =
      options.has_seed ? (unsigned int)options.seed : DEFAULT_SEED;
  unsigned int seed2 = seed1 + 1;

  first = create_and_fill_matrix(
      options.height1,
      options.width1,
      options.has_in_file_1,
      options.in_file_1,
      seed1
  );
  if (!first) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

  second = create_and_fill_matrix(
      options.height2,
      options.width2,
      options.has_in_file_2,
      options.in_file_2,
      seed2
  );
  if (!second) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

  if (!generate_matrix(options.height1, &result, options.width2)) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

  if (!run_computation_threaded(&options, first, second, result)) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

  gettimeofday(&end_time, NULL);
  double elapsed = (end_time.tv_sec - start_time.tv_sec) +
                   (end_time.tv_usec - start_time.tv_usec) / 1000000.0;
  printf("Время выполнения: %.3f сек\n", elapsed);

  const char* output_filename =
      options.has_out_file ? options.out_file : "mat-mul-output.txt";
  if (!write_matrix_to_file(
          output_filename, options.height1, result, options.width2
      )) {
    exit_code = EXIT_FAILURE;
  }

cleanup:
  free_matrix(options.height1, first);
  free_matrix(options.height2, second);
  free_matrix(options.height1, result);

  return exit_code;
}

