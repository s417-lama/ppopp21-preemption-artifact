#pragma once
#ifndef MLOG_UTIL_H_
#define MLOG_UTIL_H_

#define MLOG_CONCAT(a, b) a##b

#define MLOG_NARGS(...) MLOG_NARGS_(__VA_ARGS__, 64, 63, 62, 61, 60, 59, 58, 57, 56, 55, 54, 53, 52, 51, 50, 49, 48, 47, 46, 45, 44, 43, 42, 41, 40, 39, 38, 37, 36, 35, 34, 33, 32, 31, 30, 29, 28, 27, 26, 25, 24, 23, 22, 21, 20, 19, 18, 17, 16, 15, 14, 13, 12, 11, 10, 9, 8, 7, 6, 5, 4, 3, 2, 1,)
#define MLOG_NARGS_(_1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, _16, _17, _18, _19, _20, _21, _22, _23, _24, _25, _26, _27, _28, _29, _30, _31, _32, _33, _34, _35, _36, _37, _38, _39, _40, _41, _42, _43, _44, _45, _46, _47, _48, _49, _50, _51, _52, _53, _54, _55, _56, _57, _58, _59, _60, _61, _62, _63, _64, N, ...) N

/*
 * foreach
 */

#define MLOG_FOREACH_1( op, x, ...) op(x)
#define MLOG_FOREACH_2( op, x, ...) op(x) MLOG_FOREACH_1( op, __VA_ARGS__)
#define MLOG_FOREACH_3( op, x, ...) op(x) MLOG_FOREACH_2( op, __VA_ARGS__)
#define MLOG_FOREACH_4( op, x, ...) op(x) MLOG_FOREACH_3( op, __VA_ARGS__)
#define MLOG_FOREACH_5( op, x, ...) op(x) MLOG_FOREACH_4( op, __VA_ARGS__)
#define MLOG_FOREACH_6( op, x, ...) op(x) MLOG_FOREACH_5( op, __VA_ARGS__)
#define MLOG_FOREACH_7( op, x, ...) op(x) MLOG_FOREACH_6( op, __VA_ARGS__)
#define MLOG_FOREACH_8( op, x, ...) op(x) MLOG_FOREACH_7( op, __VA_ARGS__)
#define MLOG_FOREACH_9( op, x, ...) op(x) MLOG_FOREACH_8( op, __VA_ARGS__)
#define MLOG_FOREACH_10(op, x, ...) op(x) MLOG_FOREACH_9( op, __VA_ARGS__)
#define MLOG_FOREACH_11(op, x, ...) op(x) MLOG_FOREACH_10(op, __VA_ARGS__)
#define MLOG_FOREACH_12(op, x, ...) op(x) MLOG_FOREACH_11(op, __VA_ARGS__)
#define MLOG_FOREACH_13(op, x, ...) op(x) MLOG_FOREACH_12(op, __VA_ARGS__)
#define MLOG_FOREACH_14(op, x, ...) op(x) MLOG_FOREACH_13(op, __VA_ARGS__)
#define MLOG_FOREACH_15(op, x, ...) op(x) MLOG_FOREACH_14(op, __VA_ARGS__)
#define MLOG_FOREACH_16(op, x, ...) op(x) MLOG_FOREACH_15(op, __VA_ARGS__)
#define MLOG_FOREACH_17(op, x, ...) op(x) MLOG_FOREACH_16(op, __VA_ARGS__)
#define MLOG_FOREACH_18(op, x, ...) op(x) MLOG_FOREACH_17(op, __VA_ARGS__)
#define MLOG_FOREACH_19(op, x, ...) op(x) MLOG_FOREACH_18(op, __VA_ARGS__)
#define MLOG_FOREACH_20(op, x, ...) op(x) MLOG_FOREACH_19(op, __VA_ARGS__)

#define MLOG_FOREACH_(N, op, ...) MLOG_CONCAT(MLOG_FOREACH_, N)(op, __VA_ARGS__)
#define MLOG_FOREACH(op, ...) MLOG_FOREACH_(MLOG_NARGS(__VA_ARGS__), op, __VA_ARGS__)

/*
 * typeof, sizeof
 */

// MLOG_TYPEOF macro forces a type to decay (e.g., array, function pointer)
#ifdef __cplusplus
#include <type_traits>
#define MLOG_TYPEOF(x) __typeof__(typename std::decay<__typeof__(x)>::type)
#else
// This trick using a comma operator does not work in C++
#define MLOG_TYPEOF(x) __typeof__(((void)0, (x)))
#endif

#define MLOG_SIZEOF(x) sizeof(MLOG_TYPEOF(x))

#define MLOG_PLUS_SIZEOF(x) + MLOG_SIZEOF(x)

#define MLOG_SUM_SIZEOF(...) (0 MLOG_FOREACH(MLOG_PLUS_SIZEOF, __VA_ARGS__))

/*
 * MLOG_READ_ARG
 */

#define MLOG_READ_ARG(buf, type) \
  (*(buf) = (type*)*(buf)+1, *(((type*)*(buf))-1))

#endif /* MLOG_UTIL_H_ */

/* vim: set ts=2 sw=2 tw=0: */
