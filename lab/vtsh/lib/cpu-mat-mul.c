#include <errno.h>
#include <getopt.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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
} cpu_options;

struct task {
  cpu_options* option;
  int64_t*** first;
  int64_t*** second;
  int64_t*** result;
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

  for (int i = 1; i < argc; ++i) {
    const char* arg = argv[i];

    if (strncmp(arg, "--", 2) != 0) {
      (void)fprintf(
          stderr,
          "Error: Invalid argument format. Options must start with '--'.\n"
      );
      return false;
    }

    const char* opt_name = arg + 2;
    char* value = NULL;

    char* eq_ptr = strchr(opt_name, '=');
    if (eq_ptr != NULL) {
      *eq_ptr = '\0';
      value = eq_ptr + 1;
    } else {
      if (i + 1 >= argc) {
        (void)fprintf(stderr, "Error: No value provided for option.\n");
        return false;
      }
      value = argv[i + 1];
      i++;
    }

    if (!parse_one_option(opt_name, opts, value)) {
      return false;
    }
  }

  if (opts->width1 == 0 || opts->height1 == 0 || opts->width2 == 0 ||
      opts->height2 == 0) {
    (void)fprintf(
        stderr, "Error: Not all required matrix dimensions were specified.\n"
    );
    return false;
  }

  return true;
}

static bool check_args(const cpu_options* options) {
  if (options->width1 != options->height2) {
    (void)fprintf(
        stderr, "Error: Incompatible matrix dimensions for multiplication.\n"
    );
    return false;
  }
  if (options->height1 == 0 || options->height2 == 0 || options->width1 == 0 ||
      options->width2 == 0) {
    return false;
  }
  return true;
}

static bool read_matrix_from_file(
    const char* filename, uint64_t height, int64_t** matrix, uint64_t width
) {
  FILE* file = fopen(filename, "r");
  if (!file) {
    perror("Error opening file for reading");
    return false;
  }

  char buffer[BUFF_SIZE];

  for (uint64_t i = 0; i < height; ++i) {
    for (uint64_t j = 0; j < width; ++j) {
      if (fscanf(file, "%63s", buffer) != 1) {
        (void)fprintf(stderr, "size is not equal to input\n");
        (void)fclose(file);
        return false;
      }

      char* endptr = NULL;
      errno = 0;
      long long val = strtoll(buffer, &endptr, BASE_10);

      if (endptr == buffer) {
        (void)fprintf(stderr, "no numbers\n");
        (void)fclose(file);
        return false;
      }
      if (*endptr != '\0') {
        (void)fprintf(stderr, "invalid file content\n");
        (void)fclose(file);
        return false;
      }

      matrix[i][j] = (int64_t)val;
    }
  }

  (void)fclose(file);
  return true;
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

static bool run_computation(struct task* clac_task) {
  uint64_t height1 = clac_task->option->height1;
  uint64_t width1 = clac_task->option->width1;
  uint64_t width2 = clac_task->option->width2;

  int64_t** matrix_A = *(clac_task->first);
  int64_t** matrix_B = *(clac_task->second);
  int64_t** matrix_C = *(clac_task->result);

  for (uint64_t i = 0; i < height1; i++) {
    for (uint64_t j = 0; j < width2; j++) {
      int64_t sum = 0;
      for (uint64_t k = 0; k < width1; k++) {
        sum += matrix_A[i][k] * matrix_B[k][j];
      }
      matrix_C[i][j] = sum;
    }
  }
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

  if (!parse_args(argc, argv, &options) || !check_args(&options)) {
    return EXIT_FAILURE;
  }

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

  struct task exec_task = {&options, &first, &second, &result};
  if (!run_computation(&exec_task)) {
    exit_code = EXIT_FAILURE;
    goto cleanup;
  }

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