#include "args.h"
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* ------------------ */
/* Utility functions. */
/* ------------------ */

// Prints a message to stderr and exits with a non-zero error code.
static void err(const char *msg) {
  fprintf(stderr, "Error: %s.\n", msg);
  exit(1);
}

// Prints to an automatically-allocated string. Returns NULL if an encoding
// error occurs or if sufficient memory cannot be allocated.
static char *str(const char *fmtstr, ...) {
  va_list args;

  va_start(args, fmtstr);
  int len = vsnprintf(NULL, 0, fmtstr, args);
  if (len < 0) {
    return NULL;
  }
  va_end(args);

  char *string = malloc(len + 1);
  if (string == NULL) {
    return NULL;
  }

  va_start(args, fmtstr);
  vsnprintf(string, len + 1, fmtstr, args);
  va_end(args);

  return string;
}

// Duplicates a string, automatically allocating memory for the copy.
static char *str_dup(const char *string) {
  size_t len = strlen(string) + 1;
  char *copy = malloc(len);
  return copy ? memcpy(copy, string, len) : NULL;
}

// Hashes a string using the FNV-1a algorithm.
static uint32_t str_hash(const char *string) {
  uint32_t hash = 2166136261u;
  size_t length = strlen(string);
  for (size_t i = 0; i < length; i++) {
    hash ^= (uint8_t)string[i];
    hash *= 16777619;
  }
  return hash;
}

// Attempts to parse a string as an integer value, exiting on failure.
static int try_str_to_int(const char *string) {
  char *endptr;
  errno = 0;
  long result = strtol(string, &endptr, 0);
  if (errno == ERANGE || result > INT_MAX || result < INT_MIN) {
    err(str("'%s' is out of range", string));
  }
  if (*endptr != '\0') {
    err(str("cannot parse '%s' as an integer", string));
  }
  return (int)result;
}

// Attempts to parse a string as a double value, exiting on failure.
static double try_str_to_double(const char *string) {
  char *endptr;
  errno = 0;
  double result = strtod(string, &endptr);
  if (errno == ERANGE) {
    err(str("'%s' is out of range", string));
  }
  if (*endptr != '\0') {
    err(str("cannot parse '%s' as a floating-point value", string));
  }
  return result;
}

/* --------------------------------- */
/* Vec: a dynamic array of pointers. */
/* --------------------------------- */

typedef struct {
  int count;
  int capacity;
  void **entries;
} Vec;

static Vec *vec_new() {
  Vec *vec = malloc(sizeof(Vec));
  vec->count = 0;
  vec->capacity = 0;
  vec->entries = NULL;
  return vec;
}

static void vec_free(Vec *vec) {
  free(vec->entries);
  free(vec);
}

static void vec_add(Vec *vec, void *entry) {
  if (vec->count + 1 > vec->capacity) {
    vec->capacity = vec->capacity < 8 ? 8 : vec->capacity * 2;
    vec->entries = realloc(vec->entries, sizeof(void *) * vec->capacity);
  }
  vec->entries[vec->count] = entry;
  vec->count++;
}

/* ------------------------------------------------------------------- */
/* Map: a linear-probing hash map with string-keys and pointer-values. */
/* ------------------------------------------------------------------- */

// The map automatically grows to keep count/capacity < MAP_MAX_LOAD.
// This data structure performs optimally when it's less than half-full.
#define MAP_MAX_LOAD 0.5

typedef struct {
  char *key;
  void *value;
  uint32_t key_hash;
} MapEntry;

typedef struct {
  int count;
  int capacity;
  int max_load_threshold;
  MapEntry *entries;
} Map;

static Map *map_new() {
  Map *map = malloc(sizeof(Map));
  map->count = 0;
  map->capacity = 0;
  map->max_load_threshold = 0;
  map->entries = NULL;
  return map;
}

static void map_free(Map *map) {
  for (int i = 0; i < map->capacity; i++) {
    MapEntry *entry = &map->entries[i];
    if (entry->key != NULL) {
      free(entry->key);
    }
  }
  free(map->entries);
  free(map);
}

static MapEntry *map_find(Map *map, const char *key, uint32_t key_hash) {
  // Capacity is always a power of 2 so we can use bitwise-AND as a fast
  // modulo operator, i.e. this is equivalent to: index = key_hash % capacity.
  size_t index = key_hash & (map->capacity - 1);

  for (;;) {
    MapEntry *entry = &map->entries[index];
    if (entry->key == NULL) {
      return entry;
    } else if (key_hash == entry->key_hash && strcmp(key, entry->key) == 0) {
      return entry;
    }
    index = (index + 1) & (map->capacity - 1);
  }
}

static void map_grow(Map *map) {
  MapEntry *old_entries = map->entries;
  int old_capacity = map->capacity;
  int new_capacity = old_capacity < 8 ? 8 : old_capacity * 2;

  MapEntry *new_entries = malloc(sizeof(MapEntry) * new_capacity);
  for (int i = 0; i < new_capacity; i++) {
    new_entries[i].key = NULL;
  }

  map->count = 0;
  map->capacity = new_capacity;
  map->max_load_threshold = new_capacity * MAP_MAX_LOAD;
  map->entries = new_entries;

  for (int i = 0; i < old_capacity; i++) {
    MapEntry *src = &old_entries[i];
    if (src->key == NULL)
      continue;

    MapEntry *dst = map_find(map, src->key, src->key_hash);
    dst->key = src->key;
    dst->value = src->value;
    dst->key_hash = src->key_hash;
    map->count++;
  }

  free(old_entries);
}

// Returns true if the key was found.
static bool map_get(Map *map, const char *key, void **value) {
  if (map->count == 0)
    return false;

  uint32_t key_hash = str_hash(key);
  MapEntry *entry = map_find(map, key, key_hash);
  if (entry->key == NULL)
    return false;

  *value = entry->value;
  return true;
}

// Adds a new entry to the map or updates the value of an existing entry.
// (Note that the map stores its own internal copy of the key string.)
static void map_set(Map *map, const char *key, void *value) {
  if (map->count == map->max_load_threshold) {
    map_grow(map);
  }

  uint32_t key_hash = str_hash(key);
  MapEntry *entry = map_find(map, key, key_hash);
  if (entry->key == NULL) {
    map->count++;
    entry->key = str_dup(key);
    entry->value = value;
    entry->key_hash = key_hash;
  } else {
    entry->value = value;
  }
}

// Convenience wrapper for map_set(). This splits the keystring into space-
// separated words and adds a separate entry to the map for each word.
static void map_set_splitkey(Map *map, const char *keys, void *value) {
  char *key;
  char *saveptr;
  char *keys_copy = str_dup(keys);

  key = strtok_r(keys_copy, " ", &saveptr);
  while (key != NULL) {
    map_set(map, key, value);
    key = strtok_r(NULL, " ", &saveptr);
  }

  free(keys_copy);
}

/* -------- */
/* Options. */
/* -------- */

typedef enum {
  OPT_FLAG,
  OPT_STR,
  OPT_INT,
  OPT_DBL,
} OptionType;

typedef union {
  const char *str_val;
  int int_val;
  double dbl_val;
} OptionValue;

typedef struct {
  OptionType type;
  int count;
  int capacity;
  OptionValue *values;
  OptionValue fallback;
} Option;

static void option_free(Option *opt) {
  free(opt->values);
  free(opt);
}

static void option_append_value(Option *opt, OptionValue value) {
  if (opt->count + 1 > opt->capacity) {
    opt->capacity = opt->capacity < 4 ? 4 : opt->capacity * 2;
    opt->values = realloc(opt->values, sizeof(OptionValue) * opt->capacity);
  }
  opt->values[opt->count] = value;
  opt->count++;
}

static void option_try_set(Option *opt, const char *arg) {
  if (opt->type == OPT_STR) {
    option_append_value(opt, (OptionValue){.str_val = arg});
  } else if (opt->type == OPT_INT) {
    int value = try_str_to_int(arg);
    option_append_value(opt, (OptionValue){.int_val = value});
  } else if (opt->type == OPT_DBL) {
    double value = try_str_to_double(arg);
    option_append_value(opt, (OptionValue){.dbl_val = value});
  }
}

static Option *option_new() {
  Option *option = malloc(sizeof(Option));
  option->count = 0;
  option->capacity = 0;
  option->values = NULL;
  return option;
}

static Option *option_new_flag() {
  Option *opt = option_new();
  opt->type = OPT_FLAG;
  return opt;
}

static Option *option_new_str(const char *fallback) {
  Option *opt = option_new();
  opt->type = OPT_STR;
  opt->fallback = (OptionValue){.str_val = fallback};
  return opt;
}

static Option *option_new_int(int fallback) {
  Option *opt = option_new();
  opt->type = OPT_INT;
  opt->fallback = (OptionValue){.int_val = fallback};
  return opt;
}

static Option *option_new_double(double fallback) {
  Option *opt = option_new();
  opt->type = OPT_DBL;
  opt->fallback = (OptionValue){.dbl_val = fallback};
  return opt;
}

static const char *option_get_str(Option *opt) {
  if (opt->count > 0) {
    return opt->values[opt->count - 1].str_val;
  }
  return opt->fallback.str_val;
}

static int option_get_int(Option *opt) {
  if (opt->count > 0) {
    return opt->values[opt->count - 1].int_val;
  }
  return opt->fallback.int_val;
}

static double option_get_double(Option *opt) {
  if (opt->count > 0) {
    return opt->values[opt->count - 1].dbl_val;
  }
  return opt->fallback.dbl_val;
}

// Returns the option's values as a freshly-allocated array of string pointers.
static const char **option_get_str_list(Option *opt) {
  if (opt->count == 0) {
    return NULL;
  }
  const char **list = malloc(sizeof(char *) * opt->count);
  for (int i = 0; i < opt->count; i++) {
    list[i] = opt->values[i].str_val;
  }
  return list;
}

// Returns the option's values as a freshly-allocated array of integers.
static int *option_get_int_list(Option *opt) {
  if (opt->count == 0) {
    return NULL;
  }
  int *list = malloc(sizeof(int) * opt->count);
  for (int i = 0; i < opt->count; i++) {
    list[i] = opt->values[i].int_val;
  }
  return list;
}

// Returns the option's values as a freshly-allocated array of doubles.
static double *option_get_double_list(Option *opt) {
  if (opt->count == 0) {
    return NULL;
  }
  double *list = malloc(sizeof(double) * opt->count);
  for (int i = 0; i < opt->count; i++) {
    list[i] = opt->values[i].dbl_val;
  }
  return list;
}

// Returns a freshly-allocated state-string for debugging.
static char *option_to_str(Option *opt) {
  if (opt->type == OPT_FLAG) {
    return str("%i", opt->count);
  }

  char *fallback = NULL;
  if (opt->type == OPT_STR) {
    fallback = str_dup(opt->fallback.str_val);
  } else if (opt->type == OPT_INT) {
    fallback = str("%i", opt->fallback.int_val);
  } else if (opt->type == OPT_DBL) {
    fallback = str("%f", opt->fallback.dbl_val);
  }

  char *values = str_dup("");
  for (int i = 0; i < opt->count; i++) {
    char *value = NULL;
    if (opt->type == OPT_STR) {
      value = str_dup(opt->values[i].str_val);
    } else if (opt->type == OPT_INT) {
      value = str("%i", opt->values[i].int_val);
    } else if (opt->type == OPT_DBL) {
      value = str("%f", opt->values[i].dbl_val);
    }
    char *old_values = values;
    if (i == 0) {
      values = str_dup(value);
    } else {
      values = str("%s, %s", old_values, value);
    }
    free(old_values);
    free(value);
  }

  char *output = str("(%s) [%s]", fallback, values);
  free(fallback);
  free(values);
  return output;
}

/* ----------------------------------------------------- */
/* ArgStream: a wrapper for an array of string pointers. */
/* ----------------------------------------------------- */

typedef struct ArgStream {
  int count;
  int index;
  char **args;
} ArgStream;

static void argstream_free(ArgStream *stream) { free(stream); }

static ArgStream *argstream_new(int count, char **args) {
  ArgStream *stream = malloc(sizeof(ArgStream));
  stream->count = count;
  stream->index = 0;
  stream->args = args;
  return stream;
}

static char *argstream_next(ArgStream *stream) {
  return stream->args[stream->index++];
}

static bool argstream_has_next(ArgStream *stream) {
  return stream->index < stream->count;
}

/* ----------------- */
/* ArgParser: setup. */
/* ----------------- */

// An ArgParser instance stores registered flags, options and commands.
struct ArgParser {
  const char *helptext;
  const char *version;
  Vec *option_vec;
  Map *option_map;
  Vec *command_vec;
  Map *command_map;
  Vec *positional_args;
  void (*callback)(char *cmd_name, struct ArgParser *cmd_parser);
  char *cmd_name;
  struct ArgParser *cmd_parser;
  bool cmd_help;
};

// Initialize a new ArgParser instance.
ArgParser *ap_new() {
  ArgParser *parser = malloc(sizeof(ArgParser));
  parser->helptext = NULL;
  parser->version = NULL;
  parser->callback = NULL;
  parser->cmd_name = NULL;
  parser->cmd_parser = NULL;
  parser->cmd_help = false;
  parser->option_vec = vec_new();
  parser->option_map = map_new();
  parser->command_vec = vec_new();
  parser->command_map = map_new();
  parser->positional_args = vec_new();
  return parser;
}

// Free the memory associated with an ArgParser instance.
void ap_free(ArgParser *parser) {
  for (int i = 0; i < parser->option_vec->count; i++) {
    option_free(parser->option_vec->entries[i]);
  }
  vec_free(parser->option_vec);
  map_free(parser->option_map);
  for (int i = 0; i < parser->command_vec->count; i++) {
    ap_free(parser->command_vec->entries[i]);
  }
  vec_free(parser->command_vec);
  map_free(parser->command_map);
  vec_free(parser->positional_args);
  free(parser);
}

// Sets the parser's helptext string.
void ap_helptext(ArgParser *parser, const char *helptext) {
  parser->helptext = helptext;
}

// Sets the parser's version string.
void ap_version(ArgParser *parser, const char *version) {
  parser->version = version;
}

/* -------------------------------------- */
/* ArgParser: register flags and options. */
/* -------------------------------------- */

// Register a new flag.
void ap_flag(ArgParser *parser, const char *name) {
  Option *opt = option_new_flag();
  vec_add(parser->option_vec, opt);
  map_set_splitkey(parser->option_map, name, opt);
}

// Register a new string-valued option.
void ap_str_opt(ArgParser *parser, const char *name, const char *fallback) {
  Option *opt = option_new_str(fallback);
  vec_add(parser->option_vec, opt);
  map_set_splitkey(parser->option_map, name, opt);
}

// Register a new integer-valued option.
void ap_int_opt(ArgParser *parser, const char *name, int fallback) {
  Option *opt = option_new_int(fallback);
  vec_add(parser->option_vec, opt);
  map_set_splitkey(parser->option_map, name, opt);
}

// Register a new double-valued option.
void ap_dbl_opt(ArgParser *parser, const char *name, double fallback) {
  Option *opt = option_new_double(fallback);
  vec_add(parser->option_vec, opt);
  map_set_splitkey(parser->option_map, name, opt);
}

/* ---------------------------------- */
/* ArgParser: flag and option values. */
/* ---------------------------------- */

// Retrieve an Option instance by name.
static Option *ap_get_opt(ArgParser *parser, const char *name) {
  void *option;
  if (!map_get(parser->option_map, name, &option)) {
    err(str("'%s' is not a registered flag or option name", name));
  }
  return (Option *)option;
}

// Returns the number of times the specified flag or option was found.
int ap_count(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return opt->count;
}

// Returns true if the specified flag or option was found.
bool ap_found(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return opt->count > 0;
}

// Returns the value of the specified string option.
char *ap_str_value(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return (char *)option_get_str(opt);
}

// Returns the value of the specified integer option.
int ap_int_value(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return option_get_int(opt);
}

// Returns the value of the specified floating-point option.
double ap_dbl_value(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return option_get_double(opt);
}

// Returns an option's values as a freshly-allocated array of string pointers.
// The array's memory is not affected by calls to ap_free().
char **ap_str_values(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return (char **)option_get_str_list(opt);
}

// Returns an option's values as a freshly-allocated array of integers. The
// array's memory is not affected by calls to ap_free().
int *ap_int_values(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return option_get_int_list(opt);
}

// Returns an option's values as a freshly-allocated array of doubles. The
// array's memory is not affected by calls to ap_free().
double *ap_dbl_values(ArgParser *parser, const char *name) {
  Option *opt = ap_get_opt(parser, name);
  return option_get_double_list(opt);
}

/* -------------------------------- */
/* ArgParser: positional arguments. */
/* -------------------------------- */

// Returns true if the parser has found one or more positional arguments.
bool ap_has_args(ArgParser *parser) {
  return parser->positional_args->count > 0;
}

// Returns the number of positional arguments.
int ap_count_args(ArgParser *parser) { return parser->positional_args->count; }

// Returns the positional argument at the specified index.
char *ap_arg(ArgParser *parser, int index) {
  return (char *)parser->positional_args->entries[index];
}

// Returns the positional arguments as a freshly-allocated array of string
// pointers. The memory occupied by the returned array is not affected by
// calls to ap_free().
char **ap_args(ArgParser *parser) {
  int count = ap_count_args(parser);
  char **args = malloc(sizeof(char *) * count);
  memcpy(args, parser->positional_args->entries, sizeof(char *) * count);
  return args;
}

// Attempts to parse and return the positional arguments as a freshly
// allocated array of integers. Exits with an error message on failure. The
// memory occupied by the returned array is not affected by calls to
// ap_free().
int *ap_args_as_ints(ArgParser *parser) {
  int count = ap_count_args(parser);
  int *args = malloc(sizeof(int) * count);
  for (int i = 0; i < count; i++) {
    *(args + i) = try_str_to_int(parser->positional_args->entries[i]);
  }
  return args;
}

// Attempts to parse and return the positional arguments as a freshly
// allocated array of doubles. Exits with an error message on failure. The
// memory occupied by the returned array is not affected by calls to
// ap_free().
double *ap_args_as_doubles(ArgParser *parser) {
  int count = ap_count_args(parser);
  double *args = malloc(sizeof(double) * count);
  for (int i = 0; i < count; i++) {
    *(args + i) = try_str_to_double(parser->positional_args->entries[i]);
  }
  return args;
}

/* -------------------- */
/* ArgParser: commands. */
/* -------------------- */

// Register a new command.
ArgParser *ap_cmd(ArgParser *parser, const char *name) {
  parser->cmd_help = true;
  ArgParser *cmd_parser = ap_new();
  vec_add(parser->command_vec, cmd_parser);
  map_set_splitkey(parser->command_map, name, cmd_parser);
  return cmd_parser;
}

// Register a callback function for a command.
void ap_callback(ArgParser *parser, void (*callback)(char *, ArgParser *)) {
  parser->callback = callback;
}

// Returns true if the parser has found a command.
bool ap_has_cmd(ArgParser *parser) { return parser->cmd_name != NULL; }

// Returns the command name, if the parser has found a command.
char *ap_cmd_name(ArgParser *parser) { return parser->cmd_name; }

// Returns the command's parser instance, if the parser has found a command.
ArgParser *ap_cmd_parser(ArgParser *parser) { return parser->cmd_parser; }

// Toggles support for the automatic 'help' command.
void ap_cmd_help(ArgParser *parser, bool enable) { parser->cmd_help = enable; }

/* --------------------------- */
/* ArgParser: parse arguments. */
/* --------------------------- */

// Parse an option of the form --name=value or -n=value.
static void ap_handle_equals_opt(ArgParser *parser, const char *prefix,
                                 const char *arg) {
  char *name = str_dup(arg);
  *strchr(name, '=') = '\0';
  char *value = strchr(arg, '=') + 1;

  Option *option;
  bool found = map_get(parser->option_map, name, (void **)&option);

  if (!found || option->type == OPT_FLAG) {
    err(str("%s%s is not a recognised option name", prefix, name));
  }

  if (strlen(value) == 0) {
    err(str("missing value for the %s%s option", prefix, name));
  }

  option_try_set(option, value);
  free(name);
}

// Parse a long-form option, i.e. an option beginning with a double dash.
static void ap_handle_long_opt(ArgParser *parser, const char *arg,
                               ArgStream *stream) {
  Option *option;
  if (map_get(parser->option_map, arg, (void **)&option)) {
    if (option->type == OPT_FLAG) {
      option->count++;
    } else if (argstream_has_next(stream)) {
      option_try_set(option, argstream_next(stream));
    } else {
      err(str("missing argument for the --%s option", arg));
    }
  } else if (strcmp(arg, "help") == 0 && parser->helptext != NULL) {
    puts(parser->helptext);
    exit(0);
  } else if (strcmp(arg, "version") == 0 && parser->version != NULL) {
    puts(parser->version);
    exit(0);
  } else {
    err(str("--%s is not a recognised flag or option name", arg));
  }
}

// Parse a short-form option, i.e. an option beginning with a single dash.
static void ap_handle_short_opt(ArgParser *parser, const char *arg,
                                ArgStream *stream) {
  for (size_t i = 0; i < strlen(arg); i++) {
    char keystr[] = {arg[i], 0};
    Option *option;
    bool found = map_get(parser->option_map, keystr, (void **)&option);
    if (!found) {
      if (arg[i] == 'h' && parser->helptext != NULL) {
        puts(parser->helptext);
        exit(0);
      } else if (arg[i] == 'v' && parser->version != NULL) {
        puts(parser->version);
        exit(0);
      } else if (strlen(arg) > 1) {
        err(str("'%c' in -%s is not a recognised flag or option name", arg[i],
                arg));
      } else {
        err(str("-%s is not a recognised flag or option name", arg));
      }
    } else if (option->type == OPT_FLAG) {
      option->count++;
    } else if (argstream_has_next(stream)) {
      option_try_set(option, argstream_next(stream));
    } else if (strlen(arg) > 1) {
      err(str("missing argument for the '%c' option in -%s", arg[i], arg));
    } else {
      err(str("missing argument for the -%s option", arg));
    }
  }
}

// Parse a stream of string arguments.
static void ap_parse_stream(ArgParser *parser, ArgStream *stream) {
  ArgParser *cmd_parser;
  bool is_first_arg = true;

  while (argstream_has_next(stream)) {
    char *arg = argstream_next(stream);

    // If we encounter a '--' argument, turn off option-parsing.
    if (strcmp(arg, "--") == 0) {
      while (argstream_has_next(stream)) {
        vec_add(parser->positional_args, argstream_next(stream));
      }
    }

    // Is the argument a long-form option or flag?
    else if (strncmp(arg, "--", 2) == 0) {
      if (strstr(arg, "=") != NULL) {
        ap_handle_equals_opt(parser, "--", arg + 2);
      } else {
        ap_handle_long_opt(parser, arg + 2, stream);
      }
    }

    // Is the argument a short-form option or flag?
    else if (arg[0] == '-') {
      if (strlen(arg) == 1 || isdigit(arg[1])) {
        vec_add(parser->positional_args, arg);
      } else if (strstr(arg, "=") != NULL) {
        ap_handle_equals_opt(parser, "-", arg + 1);
      } else {
        ap_handle_short_opt(parser, arg + 1, stream);
      }
    }

    // Is the argument a registered command?
    else if (is_first_arg &&
             map_get(parser->command_map, arg, (void **)&cmd_parser)) {
      parser->cmd_name = arg;
      parser->cmd_parser = cmd_parser;
      ap_parse_stream(cmd_parser, stream);
      if (cmd_parser->callback != NULL) {
        cmd_parser->callback(arg, cmd_parser);
      }
    }

    // Is the argument the automatic 'help' command?
    else if (is_first_arg && parser->cmd_help && strcmp(arg, "help") == 0) {
      if (argstream_has_next(stream)) {
        char *name = argstream_next(stream);
        if (map_get(parser->command_map, name, (void **)&cmd_parser)) {
          if (cmd_parser->helptext != NULL) {
            puts(cmd_parser->helptext);
          }
          exit(0);
        } else {
          err(str("'%s' is not a recognised command", name));
        }
      } else {
        err(str("the help command requires an argument"));
      }
    }

    // Otherwise add the argument to our list of positionals.
    else {
      vec_add(parser->positional_args, arg);
    }
    is_first_arg = false;
  }
}

// Parse an array of string arguments.
void ap_parse_array(ArgParser *parser, int count, char *args[]) {
  ArgStream *stream = argstream_new(count, args);
  ap_parse_stream(parser, stream);
  argstream_free(stream);
}

// Parse the application's command line arguments.
void ap_parse(ArgParser *parser, int argc, char *argv[]) {
  ap_parse_array(parser, argc - 1, argv + 1);
}

/* --------------------- */
/* ArgParser: utilities. */
/* --------------------- */

// Print a parser instance to stdout.
void ap_print(ArgParser *parser) {
  puts("Flags/Options:");
  if (parser->option_map->count > 0) {
    for (int i = 0; i < parser->option_map->capacity; i++) {
      MapEntry *entry = &parser->option_map->entries[i];
      if (entry->key != NULL) {
        Option *opt = entry->value;
        char *opt_str = option_to_str(opt);
        printf("  %s: %s\n", entry->key, opt_str);
        free(opt_str);
      }
    }
  } else {
    puts("  [none]");
  }

  puts("\nArguments:");
  if (parser->positional_args->count > 0) {
    for (int i = 0; i < parser->positional_args->count; i++) {
      printf("  %s\n", ap_arg(parser, i));
    }
  } else {
    puts("  [none]");
  }

  puts("\nCommand:");
  if (ap_has_cmd(parser)) {
    printf("  %s\n", ap_cmd_name(parser));
  } else {
    puts("  [none]");
  }
}