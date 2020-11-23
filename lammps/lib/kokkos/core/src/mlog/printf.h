#pragma once
#ifndef MLOG_PRINTF_H_
#define MLOG_PRINTF_H_

#include "mlog/util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#define MLOG_DO_PRINTF(stream, print_start_pos, cur_pos, buf, type) { \
  char _mlog_format[256]; \
  int _mlog_format_size = (int)(cur_pos - print_start_pos) + 1; \
  strncpy(_mlog_format, print_start_pos, _mlog_format_size); \
  _mlog_format[_mlog_format_size] = '\0'; \
  print_start_pos += _mlog_format_size; \
  fprintf(stream, _mlog_format, MLOG_READ_ARG(buf, type)); \
}

static inline void _mlog_decode_printf_error() {
  fprintf(stderr, "Error while parsing formats in MLOG_PRINTF");
  abort();
}

static inline void* mlog_decode_printf(FILE* stream, char* format, void* buf) {
  char* c0 = format;
  char* c  = format;
  int in_specifier = 0;
  int h_flag = 0;
  int l_flag = 0;
  int L_flag = 0;
  while (*c != '\0') {
    if (in_specifier) {
      switch(*c) {
        case 'h':
          h_flag = 1;
          break;
        case 'l':
          l_flag = 1;
          break;
        case 'L':
          L_flag = 1;
          break;
        case 'd':
        case 'i':
          // signed integer
          if (h_flag) {
            MLOG_DO_PRINTF(stream, c0, c, &buf, int16_t);
            h_flag = 0;
          } else if (l_flag) {
            MLOG_DO_PRINTF(stream, c0, c, &buf, int64_t);
            l_flag = 0;
          } else {
            MLOG_DO_PRINTF(stream, c0, c, &buf, int32_t);
          }
          in_specifier = 0;
          break;
        case 'u':
        case 'o':
        case 'x':
        case 'X':
          // unsigned integer
          if (h_flag) {
            MLOG_DO_PRINTF(stream, c0, c, &buf, uint16_t);
            h_flag = 0;
          } else if (l_flag) {
            MLOG_DO_PRINTF(stream, c0, c, &buf, uint64_t);
            l_flag = 0;
          } else {
            MLOG_DO_PRINTF(stream, c0, c, &buf, uint32_t);
          }
          in_specifier = 0;
          break;
        case 'f':
        case 'F':
        case 'e':
        case 'E':
        case 'a':
        case 'A':
        case 'g':
        case 'G':
          if (L_flag) {
            // long double
            MLOG_DO_PRINTF(stream, c0, c, &buf, long double);
            L_flag = 0;
          } else if (l_flag) {
            // double
            MLOG_DO_PRINTF(stream, c0, c, &buf, double);
            l_flag = 0;
          } else {
            // float
            MLOG_DO_PRINTF(stream, c0, c, &buf, float);
          }
          in_specifier = 0;
          break;
        case 'c':
          // char
          MLOG_DO_PRINTF(stream, c0, c, &buf, char);
          in_specifier = 0;
          break;
        case 's':
          // char*
          MLOG_DO_PRINTF(stream, c0, c, &buf, char*);
          in_specifier = 0;
          break;
        case 'p':
          // void*
          MLOG_DO_PRINTF(stream, c0, c, &buf, void*);
          in_specifier = 0;
          break;
        case '%':
        case 'n':
          _mlog_decode_printf_error();
          break;
        default:
          if (h_flag) h_flag = 0;
          if (l_flag) l_flag = 0;
          if (L_flag) L_flag = 0;
          break;
      }
    } else {
      if (*c == '%') {
        if (*(c + 1) == '%') { // "%%" should be treated as '%'
          c++;
        } else {
          in_specifier = 1;
        }
      }
    }
    c++;
  }
  if (in_specifier) {
    _mlog_decode_printf_error();
  }
  fputs(c0, stream);
  return buf;
}

#endif /* MLOG_PRINTF_H_ */

/* vim: set ts=2 sw=2 tw=0: */
