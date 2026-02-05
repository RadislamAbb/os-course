#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <getopt.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#define BASE_10 10
#define HANDLERS_ARRAY_SIZE 7
#define BYTE_SIZE 255

typedef struct {
  off_t start;
  off_t end;
} range;

typedef bool flag;

typedef struct {
  flag rw;
  size_t block_size;
  size_t block_count;
  char* file;
  range range;
  flag direct;
  flag type;
  size_t alignment;
} config;

typedef bool (*option_handler_t)(const char* arg, config* main_conf);

typedef struct {
  const char* name;
  option_handler_t handler;
  bool is_init;
} option_handler_map;

bool parse_flags(
    const char* arg, bool* flag, const char* first, const char* second
) {
  if (strcasecmp(arg, first) == 0) {
    *flag = true;
    return true;
  }
  if (strcasecmp(arg, second) == 0) {
    *flag = false;
    return true;
  }
  (void)fprintf(stderr, "flag parsing error for\n");
  return false;
}

bool parse_size(const char* arg, size_t* value) {
  char* endptr = NULL;
  errno = 0;
  unsigned long long num = strtoull(arg, &endptr, BASE_10);
  if (errno != 0 || endptr == arg) {
    (void)fprintf(stderr, "incorrect number input\n");
    return false;
  }
  if (*endptr != '\0') {
    (void)fprintf(stderr, "trash after number\n");
    return false;
  }
  *value = (size_t)num;
  return true;
}

bool parse_range_val(const char* arg, range* range) {
  char* endptr = NULL;
  char* separator = strchr(arg, '-');
  if (separator == NULL) {
    (void)fprintf(stderr, "invalid range format\n");
    return false;
  }
  errno = 0;
  range->start = strtoll(arg, &endptr, BASE_10);
  if (errno != 0 || endptr != separator) {
    (void)fprintf(stderr, "incorrect start of range\n");
    return false;
  }
  const char* end_str = separator + 1;
  range->end = strtoll(end_str, &endptr, BASE_10);
  if (errno != 0 || *endptr != '\0') {
    (void)fprintf(stderr, "incorrect end of range\n");
    return false;
  }
  if (range->start > range->end) {
    (void)fprintf(stderr, "start of range cannot be greater than end\n");
    return false;
  }
  return true;
}

bool handle_rw(const char* arg, config* main_conf) {
  return parse_flags(arg, &main_conf->rw, "write", "read");
}

bool handle_block_size(const char* arg, config* main_conf) {
  return parse_size(arg, &main_conf->block_size);
}

bool handle_block_count(const char* arg, config* main_conf) {
  return parse_size(arg, &main_conf->block_count);
}

bool handle_file(const char* arg, config* main_conf) {
  main_conf->file = (char*)arg;
  return true;
}

bool handle_range(const char* arg, config* main_conf) {
  return parse_range_val(arg, &main_conf->range);
}

bool handle_direct(const char* arg, config* main_conf) {
  return parse_flags(arg, &main_conf->direct, "on", "off");
}

bool handle_type(const char* arg, config* main_conf) {
  return parse_flags(arg, &main_conf->type, "random", "sequence");
}

bool parse_arguments(int argc, char** argv, config* main_conf) {
  static option_handler_map handlers[] = {
      {         "rw",          handle_rw, false},
      { "block_size",  handle_block_size, false},
      {"block_count", handle_block_count, false},
      {       "file",        handle_file, false},
      {      "range",       handle_range, false},
      {     "direct",      handle_direct, false},
      {       "type",        handle_type, false},
  };
  const size_t num_handlers = sizeof(handlers) / sizeof(handlers[0]);

  const static struct option long_options[] = {
      {         "rw", required_argument, 0, 0},
      { "block_size", required_argument, 0, 0},
      {"block_count", required_argument, 0, 0},
      {       "file", required_argument, 0, 0},
      {      "range", optional_argument, 0, 0},
      {     "direct", required_argument, 0, 0},
      {       "type", required_argument, 0, 0},
      {            0,                 0, 0, 0}
  };

  while (true) {
    int option_index = 0;
    int parse_res = getopt_long(argc, argv, "", long_options, &option_index);
    if (parse_res == -1) {
      break;
    }

    if (parse_res == 0) {
      const char* option_name = long_options[option_index].name;
      bool handled = false;

      for (size_t i = 0; i < num_handlers; ++i) {
        if (strcmp(option_name, handlers[i].name) == 0) {
          if (!handlers[i].handler(optarg, main_conf)) {
            return false;
          }
          handled = true;
          handlers[i].is_init = true;
          break;
        }
      }
      if (!handled) {
        (void)fprintf(stderr, "Unhandled option: %s\n", option_name);
        return false;
      }
    } else if (parse_res == '?') {
      (void)fprintf(stderr, "Unknown or invalid argument.\n");
      return false;
    }
  }

  for (int i = 0; i < HANDLERS_ARRAY_SIZE; i++) {
    if (!handlers[i].is_init && i != 4) {
      (void)fprintf(stderr, "required argument was not passed to programm.\n");
      return false;
    }
  }
  return true;
}

int open_or_create_file(config* main_conf) {
  int file_desc = -1;
  if (main_conf->rw) {
    int flags = O_WRONLY | O_CREAT;
    mode_t mode = S_IRUSR | S_IWUSR;

    if (main_conf->direct) {
      flags |= O_DIRECT | O_SYNC;
    }
    file_desc = open(main_conf->file, flags, mode);

  } else {
    int flags = O_RDONLY;

    if (main_conf->direct) {
      flags |= O_DIRECT;
    }
    file_desc = open(main_conf->file, flags);
  }

  if (file_desc == -1) {
    (void)fprintf(stderr, "fail to open file\n");
    return -1;
  }
  return file_desc;
}

bool alloc_buffer(config* main_conf, void** buffer) {
  if (main_conf->direct) {
    int result =
        posix_memalign(buffer, main_conf->alignment, main_conf->block_size);
    if (result != 0) {
      (void)fprintf(stderr, "Direct buffer creation error.\n");
      return false;
    }
  } else {
    *buffer = malloc(main_conf->block_size);
    if (*buffer == NULL) {
      (void)fprintf(stderr, "malloc error.\n");
      return false;
    }
  }
  return true;
}

bool validate_and_finalize_config(int file_desc, config* main_conf) {
  struct stat file_stat;
  if (fstat(file_desc, &file_stat) == -1) {
    (void)fprintf(stderr, "fstat error.\n");
    return false;
  }

  main_conf->alignment = file_stat.st_blksize;

  if (main_conf->range.end == 0 && main_conf->range.start == 0) {
    main_conf->range.end = file_stat.st_size;
  }

  if (main_conf->direct) {
    off_t alignment = (off_t)main_conf->alignment;
    if (main_conf->block_size % alignment != 0) {
      (void)fprintf(stderr, "allignment error.\n");
      return false;
    }
    if (main_conf->range.start % alignment != 0) {
      (void)fprintf(stderr, "range allignment error.\n");
      return false;
    }

    off_t original_end = main_conf->range.end;
    main_conf->range.end = (original_end / alignment) * alignment;
  }

  off_t range_size = main_conf->range.end - main_conf->range.start;
  size_t requested_io_size = main_conf->block_size * main_conf->block_count;

  if (range_size < (off_t)requested_io_size) {
    (void)fprintf(stderr, "range smaller than block_size * block_count\n");
    return false;
  }

  return true;
}

uint8_t gen_random_non_zero_byte(unsigned int* seed_ptr) {
  return (uint8_t)((rand_r(seed_ptr) % BYTE_SIZE) + 1);
}

off_t calculate_next_offset(
    config* main_conf, size_t loop_index, unsigned int* seed_ptr
) {
  if (main_conf->type) {
    off_t range_size = main_conf->range.end - main_conf->range.start;
    if (main_conf->block_size == 0) {
      return -1;
    }

    off_t num_possible_blocks = range_size / (off_t)main_conf->block_size;

    off_t random_block_index = rand_r(seed_ptr) % num_possible_blocks;
    return main_conf->range.start +
           (random_block_index * (off_t)main_conf->block_size);
  }
  return main_conf->range.start + (off_t)(loop_index * main_conf->block_size);
}

bool perform_write(
    int file_desc,
    off_t offset,
    void* buffer,
    config* conf,
    unsigned int* seed_ptr
) {
  if (lseek(file_desc, offset, SEEK_SET) == -1) {
    (void)fprintf(stderr, "lseek error\n");
    return false;
  }

  uint8_t* char_buf = (uint8_t*)buffer;
  for (size_t j = 0; j < conf->block_size; j++) {
    char_buf[j] = gen_random_non_zero_byte(seed_ptr);
  }

  ssize_t written = write(file_desc, buffer, conf->block_size);
  if (written != (ssize_t)conf->block_size) {
    (void)fprintf(stderr, "write error\n");
    return false;
  }
  return true;
}

int perform_read(int file_desc, off_t offset, void* buffer, config* conf) {
  if (lseek(file_desc, offset, SEEK_SET) == -1) {
    (void)fprintf(stderr, "lseek error\n");
    return -1;
  }

  ssize_t bytes_read = read(file_desc, buffer, conf->block_size);
  if (bytes_read < 0) {
    (void)fprintf(stderr, "read error\n");
    return -1;
  }
  if (bytes_read == 0) {
    return 0;
  }
  return 1;
}

bool common_loader(config* main_conf) {
  int file_desc = open_or_create_file(main_conf);
  if (file_desc == -1) {
    return false;
  }

  if (!validate_and_finalize_config(file_desc, main_conf)) {
    close(file_desc);
    return false;
  }

  void* buffer = NULL;
  if (!alloc_buffer(main_conf, &buffer)) {
    close(file_desc);
    return false;
  }

  unsigned int seed = (unsigned int)time(NULL) ^ (unsigned int)getpid();
  bool success = true;

  for (size_t i = 0; i < main_conf->block_count; i++) {
    off_t offset = calculate_next_offset(main_conf, i, &seed);
    if (offset < 0) {
      (void)fprintf(stderr, "calc offset error\n");
      success = false;
      break;
    }

    if (main_conf->rw) {
      if (!perform_write(file_desc, offset, buffer, main_conf, &seed)) {
        success = false;
        break;
      }
    } else {
      int read_result = perform_read(file_desc, offset, buffer, main_conf);
      if (read_result < 0) {
        success = false;
        break;
      }
      if (read_result == 0) {
        break;
      }
    }
  }

  free(buffer);
  close(file_desc);
  return success;
}

int main(int argc, char** argv) {
  config main_conf;

  main_conf.rw = 0;
  main_conf.block_size = 0;
  main_conf.block_count = 0;
  main_conf.file = NULL;
  main_conf.range.start = 0;
  main_conf.range.end = 0;
  main_conf.direct = 0;
  main_conf.type = 0;
  main_conf.alignment = 0;

  if (!parse_arguments(argc, argv, &main_conf)) {
    return EXIT_FAILURE;
  }

  if (!common_loader(&main_conf)) {
    return EXIT_FAILURE;
  }

  return EXIT_SUCCESS;
}