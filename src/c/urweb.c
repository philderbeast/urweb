#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdarg.h>

#include "types.h"

uw_unit uw_unit_v = {};

#define ERROR_BUF_LEN 1024

struct uw_context {
  char *page, *page_front, *page_back;
  char *heap, *heap_front, *heap_back;
  char **inputs;

  void *db;

  jmp_buf jmp_buf;

  char error_message[ERROR_BUF_LEN];
};

extern int uw_inputs_len;

uw_context uw_init(size_t page_len, size_t heap_len) {
  uw_context ctx = malloc(sizeof(struct uw_context));

  ctx->page_front = ctx->page = malloc(page_len);
  ctx->page_back = ctx->page_front + page_len;

  ctx->heap_front = ctx->heap = malloc(heap_len);
  ctx->heap_back = ctx->heap_front + heap_len;

  ctx->inputs = calloc(uw_inputs_len, sizeof(char *));

  ctx->db = NULL;

  ctx->error_message[0] = 0;

  return ctx;
}

void uw_set_db(uw_context ctx, void *db) {
  ctx->db = db;
}

void *uw_get_db(uw_context ctx) {
  return ctx->db;
}

void uw_free(uw_context ctx) {
  free(ctx->page);
  free(ctx->heap);
  free(ctx->inputs);
  free(ctx);
}

void uw_reset_keep_request(uw_context ctx) {
  ctx->page_front = ctx->page;
  ctx->heap_front = ctx->heap;

  ctx->error_message[0] = 0;
}

void uw_reset_keep_error_message(uw_context ctx) {
  ctx->page_front = ctx->page;
  ctx->heap_front = ctx->heap;
}

void uw_reset(uw_context ctx) {
  uw_reset_keep_request(ctx);
  memset(ctx->inputs, 0, uw_inputs_len * sizeof(char *));
}

void uw_db_init(uw_context);
void uw_handle(uw_context, char *);

failure_kind uw_begin_init(uw_context ctx) {
  int r = setjmp(ctx->jmp_buf);

  if (r == 0)
    uw_db_init(ctx);

  return r;
}

failure_kind uw_begin(uw_context ctx, char *path) {
  int r = setjmp(ctx->jmp_buf);

  if (r == 0)
    uw_handle(ctx, path);

  return r;
}

__attribute__((noreturn)) void uw_error(uw_context ctx, failure_kind fk, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);

  vsnprintf(ctx->error_message, ERROR_BUF_LEN, fmt, ap);

  longjmp(ctx->jmp_buf, fk);
}

char *uw_error_message(uw_context ctx) {
  return ctx->error_message;
}

int uw_input_num(char*);

void uw_set_input(uw_context ctx, char *name, char *value) {
  int n = uw_input_num(name);

  if (n < 0)
    uw_error(ctx, FATAL, "Bad input name %s", name);

  if (n >= uw_inputs_len)
    uw_error(ctx, FATAL, "For input name %s, index %d is out of range", name, n);

  ctx->inputs[n] = value;

  //printf("[%d] %s = %s\n", n, name, value);
}

char *uw_get_input(uw_context ctx, int n) {
  if (n < 0)
    uw_error(ctx, FATAL, "Negative input index %d", n);
  if (n >= uw_inputs_len)
    uw_error(ctx, FATAL, "Out-of-bounds input index %d", n);
  //printf("[%d] = %s\n", n, ctx->inputs[n]);
  return ctx->inputs[n];
}

char *uw_get_optional_input(uw_context ctx, int n) {
  if (n < 0)
    uw_error(ctx, FATAL, "Negative input index %d", n);
  if (n >= uw_inputs_len)
    uw_error(ctx, FATAL, "Out-of-bounds input index %d", n);
  printf("[%d] = %s\n", n, ctx->inputs[n]);
  return (ctx->inputs[n] == NULL ? "" : ctx->inputs[n]);
}

static void uw_check_heap(uw_context ctx, size_t extra) {
  if (ctx->heap_back - ctx->heap_front < extra) {
    size_t desired = ctx->heap_back - ctx->heap_front + extra, next;
    char *new_heap;

    for (next = ctx->heap_back - ctx->heap_front; next < desired; next *= 2);

    new_heap = realloc(ctx->heap, next);
    ctx->heap_front = new_heap;
    ctx->heap_back = new_heap + next;

    if (new_heap != ctx->heap) {
      ctx->heap = new_heap;
      uw_error(ctx, UNLIMITED_RETRY, "Couldn't allocate new heap chunk contiguously");
    }

    ctx->heap = new_heap;
  }
}

void *uw_malloc(uw_context ctx, size_t len) {
  void *result;

  uw_check_heap(ctx, len);

  result = ctx->heap_front;
  ctx->heap_front += len;
  return result;
}

int uw_really_send(int sock, const void *buf, ssize_t len) {
  while (len > 0) {
    ssize_t n = send(sock, buf, len, 0);

    if (n < 0)
      return n;

    buf += n;
    len -= n;
  }

  return 0;
}

int uw_send(uw_context ctx, int sock) {
  return uw_really_send(sock, ctx->page, ctx->page_front - ctx->page);
}

static void uw_check(uw_context ctx, size_t extra) {
  size_t desired = ctx->page_back - ctx->page_front + extra, next;
  char *new_page;

  for (next = ctx->page_back - ctx->page_front; next < desired; next *= 2);

  new_page = realloc(ctx->page, next);
  ctx->page_front = new_page + (ctx->page_front - ctx->page);
  ctx->page_back = new_page + (ctx->page_back - ctx->page);
  ctx->page = new_page;
}

static void uw_writec_unsafe(uw_context ctx, char c) {
  *(ctx->page_front)++ = c;
}

void uw_writec(uw_context ctx, char c) {
  uw_check(ctx, 1);
  uw_writec_unsafe(ctx, c);
}

static void uw_write_unsafe(uw_context ctx, const char* s) {
  int len = strlen(s);
  memcpy(ctx->page_front, s, len);
  ctx->page_front += len;
}

void uw_write(uw_context ctx, const char* s) {
  uw_check(ctx, strlen(s) + 1);
  uw_write_unsafe(ctx, s);
  *ctx->page_front = 0;
}


char *uw_Basis_attrifyInt(uw_context ctx, uw_Basis_int n) {
  char *result;
  int len;
  uw_check_heap(ctx, INTS_MAX);
  result = ctx->heap_front;
  sprintf(result, "%lld%n", n, &len);
  ctx->heap_front += len+1;
  return result;
}

char *uw_Basis_attrifyFloat(uw_context ctx, uw_Basis_float n) {
  char *result;
  int len;
  uw_check_heap(ctx, FLOATS_MAX);
  result = ctx->heap_front;
  sprintf(result, "%g%n", n, &len);
  ctx->heap_front += len+1;
  return result;
}

char *uw_Basis_attrifyString(uw_context ctx, uw_Basis_string s) {
  int len = strlen(s);
  char *result, *p;
  uw_check_heap(ctx, len * 6 + 1);

  result = p = ctx->heap_front;

  for (; *s; s++) {
    char c = *s;

    if (c == '"') {
      strcpy(p, "&quot;");
      p += 6;
    } else if (c == '&') {
      strcpy(p, "&amp;");
      p += 5;
    }
    else if (isprint(c))
      *p++ = c;
    else {
      int len2;
      sprintf(p, "&#%d;%n", c, &len2);
      p += len2;
    }
  }

  *p++ = 0;
  ctx->heap_front = p;
  return result;
}

static void uw_Basis_attrifyInt_w_unsafe(uw_context ctx, uw_Basis_int n) {
  int len;

  sprintf(ctx->page_front, "%lld%n", n, &len);
  ctx->page_front += len;
}

void uw_Basis_attrifyInt_w(uw_context ctx, uw_Basis_int n) {
  uw_check(ctx, INTS_MAX);
  uw_Basis_attrifyInt_w_unsafe(ctx, n);
}

void uw_Basis_attrifyFloat_w(uw_context ctx, uw_Basis_float n) {
  int len;

  uw_check(ctx, FLOATS_MAX);
  sprintf(ctx->page_front, "%g%n", n, &len);
  ctx->page_front += len;
}

void uw_Basis_attrifyString_w(uw_context ctx, uw_Basis_string s) {
  uw_check(ctx, strlen(s) * 6);

  for (; *s; s++) {
    char c = *s;

    if (c == '"')
      uw_write_unsafe(ctx, "&quot;");
    else if (c == '&')
      uw_write_unsafe(ctx, "&amp;");
    else if (isprint(c))
      uw_writec_unsafe(ctx, c);
    else {
      uw_write_unsafe(ctx, "&#");
      uw_Basis_attrifyInt_w_unsafe(ctx, c);
      uw_writec_unsafe(ctx, ';');
    }
  }
}


char *uw_Basis_urlifyInt(uw_context ctx, uw_Basis_int n) {
  int len;
  char *r;

  uw_check_heap(ctx, INTS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%lld%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

char *uw_Basis_urlifyFloat(uw_context ctx, uw_Basis_float n) {
  int len;
  char *r;

  uw_check_heap(ctx, FLOATS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%g%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

char *uw_Basis_urlifyString(uw_context ctx, uw_Basis_string s) {
  char *r, *p;

  uw_check_heap(ctx, strlen(s) * 3 + 1);

  for (r = p = ctx->heap_front; *s; s++) {
    char c = *s;

    if (c == ' ')
      *p++ = '+';
    else if (isalnum(c))
      *p++ = c;
    else {
      sprintf(p, "%%%02X", c);
      p += 3;
    }
  }

  *p++ = 0;
  ctx->heap_front = p;
  return r;
}

char *uw_Basis_urlifyBool(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False)
    return "0";
  else
    return "1";
}

static void uw_Basis_urlifyInt_w_unsafe(uw_context ctx, uw_Basis_int n) {
  int len;

  sprintf(ctx->page_front, "%lld%n", n, &len);
  ctx->page_front += len;
}

void uw_Basis_urlifyInt_w(uw_context ctx, uw_Basis_int n) {
  uw_check(ctx, INTS_MAX);
  uw_Basis_urlifyInt_w_unsafe(ctx, n);
}

void uw_Basis_urlifyFloat_w(uw_context ctx, uw_Basis_float n) {
  int len;

  uw_check(ctx, FLOATS_MAX);
  sprintf(ctx->page_front, "%g%n", n, &len);
  ctx->page_front += len;
}

void uw_Basis_urlifyString_w(uw_context ctx, uw_Basis_string s) {
  uw_check(ctx, strlen(s) * 3);

  for (; *s; s++) {
    char c = *s;

    if (c == ' ')
      uw_writec_unsafe(ctx, '+');
    else if (isalnum(c))
      uw_writec_unsafe(ctx, c);
    else {
      sprintf(ctx->page_front, "%%%02X", c);
      ctx->page_front += 3;
    }
  }
}

void uw_Basis_urlifyBool_w(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False)
    uw_writec(ctx, '0');
  else
    uw_writec(ctx, '1');
}


static char *uw_unurlify_advance(char *s) {
  char *new_s = strchr(s, '/');

  if (new_s)
    *new_s++ = 0;
  else
    new_s = strchr(s, 0);

  return new_s;
}

uw_Basis_int uw_Basis_unurlifyInt(uw_context ctx, char **s) {
  char *new_s = uw_unurlify_advance(*s);
  uw_Basis_int r;

  r = atoll(*s);
  *s = new_s;
  return r;
}

uw_Basis_float uw_Basis_unurlifyFloat(uw_context ctx, char **s) {
  char *new_s = uw_unurlify_advance(*s);
  uw_Basis_float r;

  r = atof(*s);
  *s = new_s;
  return r;
}

static uw_Basis_string uw_unurlifyString_to(uw_context ctx, char *r, char *s) {
  char *s1, *s2;
  int n;

  for (s1 = r, s2 = s; *s2; ++s1, ++s2) {
    char c = *s2;

    switch (c) {
    case '+':
      *s1 = ' ';
      break;
    case '%':
      if (s2[1] == 0)
        uw_error(ctx, FATAL, "Missing first character of escaped URL byte");
      if (s2[2] == 0)
        uw_error(ctx, FATAL, "Missing second character of escaped URL byte");
      if (sscanf(s2+1, "%02X", &n) != 1)
        uw_error(ctx, FATAL, "Invalid escaped URL byte starting at: %s", s2);
      *s1 = n;
      s2 += 2;
      break;
    default:
      *s1 = c;
    }
  }
  *s1++ = 0;
  return s1;
}

uw_Basis_bool uw_Basis_unurlifyBool(uw_context ctx, char **s) {
  char *new_s = uw_unurlify_advance(*s);
  uw_Basis_bool r;
  
  if (*s[0] == 0 || !strcmp(*s, "0") || !strcmp(*s, "off"))
    r = uw_Basis_False;
  else
    r = uw_Basis_True;

  *s = new_s;
  return r;
}

uw_Basis_string uw_Basis_unurlifyString(uw_context ctx, char **s) {
  char *new_s = uw_unurlify_advance(*s);
  char *r, *s1, *s2;
  int len, n;

  len = strlen(*s);
  uw_check_heap(ctx, len + 1);

  r = ctx->heap_front;
  ctx->heap_front = uw_unurlifyString_to(ctx, ctx->heap_front, *s);
  *s = new_s;
  return r;
}


char *uw_Basis_htmlifyInt(uw_context ctx, uw_Basis_int n) {
  int len;
  char *r;

  uw_check_heap(ctx, INTS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%lld%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

void uw_Basis_htmlifyInt_w(uw_context ctx, uw_Basis_int n) {
  int len;

  uw_check(ctx, INTS_MAX);
  sprintf(ctx->page_front, "%lld%n", n, &len);
  ctx->page_front += len;
}

char *uw_Basis_htmlifyFloat(uw_context ctx, uw_Basis_float n) {
  int len;
  char *r;

  uw_check_heap(ctx, FLOATS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%g%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

void uw_Basis_htmlifyFloat_w(uw_context ctx, uw_Basis_float n) {
  int len;

  uw_check(ctx, FLOATS_MAX);
  sprintf(ctx->page_front, "%g%n", n, &len);
  ctx->page_front += len;
}

char *uw_Basis_htmlifyString(uw_context ctx, uw_Basis_string s) {
  char *r, *s2;

  uw_check_heap(ctx, strlen(s) * 5 + 1);

  for (r = s2 = ctx->heap_front; *s; s++) {
    char c = *s;

    switch (c) {
    case '<':
      strcpy(s2, "&lt;");
      s2 += 4;
      break;
    case '&':
      strcpy(s2, "&amp;");
      s2 += 5;
      break;
    default:
      if (isprint(c))
        *s2++ = c;
      else {
        int len2;
        sprintf(s2, "&#%d;%n", c, &len2);
        s2 += len2;
      }
    }
  }

  *s2++ = 0;
  ctx->heap_front = s2;
  return r;
}

void uw_Basis_htmlifyString_w(uw_context ctx, uw_Basis_string s) {
  uw_check(ctx, strlen(s) * 5);

  for (; *s; s++) {
    char c = *s;

    switch (c) {
    case '<':
      uw_write_unsafe(ctx, "&lt;");
      break;
    case '&':
      uw_write_unsafe(ctx, "&amp;");
      break;
    default:
      if (isprint(c))
        uw_writec_unsafe(ctx, c);
      else {
        uw_write_unsafe(ctx, "&#");
        uw_Basis_attrifyInt_w_unsafe(ctx, c);
        uw_writec_unsafe(ctx, ';');
      }
    }
  }
}

uw_Basis_string uw_Basis_htmlifyBool(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False)
    return "False";
  else
    return "True";
}

void uw_Basis_htmlifyBool_w(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False) {
    uw_check(ctx, 6);
    strcpy(ctx->page_front, "False");
    ctx->page_front += 5;
  } else {
    uw_check(ctx, 5);
    strcpy(ctx->page_front, "True");
    ctx->page_front += 4;
  }
}

uw_Basis_string uw_Basis_strcat(uw_context ctx, uw_Basis_string s1, uw_Basis_string s2) {
  int len = strlen(s1) + strlen(s2) + 1;
  char *s;

  uw_check_heap(ctx, len);

  s = ctx->heap_front;

  strcpy(s, s1);
  strcat(s, s2);
  ctx->heap_front += len;

  return s;
}

uw_Basis_string uw_Basis_strdup(uw_context ctx, uw_Basis_string s1) {
  int len = strlen(s1) + 1;
  char *s;

  uw_check_heap(ctx, len);

  s = ctx->heap_front;

  strcpy(s, s1);
  ctx->heap_front += len;

  return s;
}


char *uw_Basis_sqlifyInt(uw_context ctx, uw_Basis_int n) {
  int len;
  char *r;

  uw_check_heap(ctx, INTS_MAX + 6);
  r = ctx->heap_front;
  sprintf(r, "%lld::int8%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

char *uw_Basis_sqlifyFloat(uw_context ctx, uw_Basis_float n) {
  int len;
  char *r;

  uw_check_heap(ctx, FLOATS_MAX + 8);
  r = ctx->heap_front;
  sprintf(r, "%g::float8%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}


uw_Basis_string uw_Basis_sqlifyString(uw_context ctx, uw_Basis_string s) {
  char *r, *s2;

  uw_check_heap(ctx, strlen(s) * 2 + 10);

  r = s2 = ctx->heap_front;
  *s2++ = 'E';
  *s2++ = '\'';

  for (; *s; s++) {
    char c = *s;

    switch (c) {
    case '\'':
      strcpy(s2, "\\'");
      s2 += 2;
      break;
    case '\\':
      strcpy(s2, "\\\\");
      s2 += 2;
      break;
    default:
      if (isprint(c))
        *s2++ = c;
      else {
        sprintf(s2, "\\%3o", c);
        s2 += 4;
      }
    }
  }

  strcpy(s2, "'::text");
  ctx->heap_front = s2 + 8;
  return r;
}

char *uw_Basis_sqlifyBool(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False)
    return "FALSE";
  else
    return "TRUE";
}

char *uw_Basis_ensqlBool(uw_Basis_bool b) {
  static uw_Basis_int true = 1;
  static uw_Basis_int false = 0;

  if (b == uw_Basis_False)
    return (char *)&false;
  else
    return (char *)&true;
}

uw_Basis_string uw_Basis_intToString(uw_context ctx, uw_Basis_int n) {
  int len;
  char *r;

  uw_check_heap(ctx, INTS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%lld%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

uw_Basis_string uw_Basis_floatToString(uw_context ctx, uw_Basis_float n) {
  int len;
  char *r;

  uw_check_heap(ctx, FLOATS_MAX);
  r = ctx->heap_front;
  sprintf(r, "%g%n", n, &len);
  ctx->heap_front += len+1;
  return r;
}

uw_Basis_string uw_Basis_boolToString(uw_context ctx, uw_Basis_bool b) {
  if (b == uw_Basis_False)
    return "False";
  else
    return "True";
}


uw_Basis_int *uw_Basis_stringToInt(uw_context ctx, uw_Basis_string s) {
  char *endptr;
  uw_Basis_int n = strtoll(s, &endptr, 10);

  if (*s != '\0' && *endptr == '\0') {
    uw_Basis_int *r = uw_malloc(ctx, sizeof(uw_Basis_int));
    *r = n;
    return r;
  } else
    return NULL;
}

uw_Basis_float *uw_Basis_stringToFloat(uw_context ctx, uw_Basis_string s) {
  char *endptr;
  uw_Basis_float n = strtod(s, &endptr);

  if (*s != '\0' && *endptr == '\0') {
    uw_Basis_float *r = uw_malloc(ctx, sizeof(uw_Basis_float));
    *r = n;
    return r;
  } else
    return NULL;
}

uw_Basis_bool *uw_Basis_stringToBool(uw_context ctx, uw_Basis_string s) {
  static uw_Basis_bool true = uw_Basis_True;
  static uw_Basis_bool false = uw_Basis_False;

  if (!strcasecmp (s, "True"))
    return &true;
  else if (!strcasecmp (s, "False"))
    return &false;
  else
    return NULL;
}

uw_Basis_int uw_Basis_stringToInt_error(uw_context ctx, uw_Basis_string s) {
  char *endptr;
  uw_Basis_int n = strtoll(s, &endptr, 10);

  if (*s != '\0' && *endptr == '\0')
    return n;
  else
    uw_error(ctx, FATAL, "Can't parse int: %s", s);
}

uw_Basis_float uw_Basis_stringToFloat_error(uw_context ctx, uw_Basis_string s) {
  char *endptr;
  uw_Basis_float n = strtod(s, &endptr);

  if (*s != '\0' && *endptr == '\0')
    return n;
  else
    uw_error(ctx, FATAL, "Can't parse float: %s", s);
}

uw_Basis_bool uw_Basis_stringToBool_error(uw_context ctx, uw_Basis_string s) {
  if (!strcasecmp(s, "T") || !strcasecmp (s, "True"))
    return uw_Basis_True;
  else if (!strcasecmp(s, "F") || !strcasecmp (s, "False"))
    return uw_Basis_False;
  else
    uw_error(ctx, FATAL, "Can't parse bool: %s", s);
}