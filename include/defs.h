/*

  Copyright (C) 2021 Gonzalo José Carracedo Carballal

  This program is free software: you can redistribute it and/or modify
  it under the terms of the GNU Lesser General Public License as
  published by the Free Software Foundation, version 3.

  This program is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU Lesser General Public License for more details.

  You should have received a copy of the GNU Lesser General Public
  License along with this program.  If not, see
  <http://www.gnu.org/licenses/>

*/

#ifndef _DEFS_H
#define _DEFS_H

#include <sigutils/util/util.h>

#ifndef my_calloc
#  define my_calloc(len, size) calloc(len, size)
#endif /* my_calloc */

#ifndef my_free
#  define my_free(ptr) free(ptr)
#endif /* my_free */

#ifndef my_malloc
#  define my_malloc(size) malloc(size)
#endif /* my_malloc */

#define ERROR(fmt, arg...) \
  fprintf(stderr, "error: " fmt, ##arg)

#define TYPENAME(class)          JOIN(class, _t)
#define METHOD_NAME(class, name) JOIN(class, JOIN(_, name))
#define METHOD(class, ret, name, ...)           \
  ret METHOD_NAME(class, name) (                \
    TYPENAME(class) *self,                      \
    ##__VA_ARGS__)

#define METHOD_CONST(class, ret, name, ...)     \
  ret METHOD_NAME(class, name) (                \
    const TYPENAME(class) *self,                \
    ##__VA_ARGS__)

#define GETTER METHOD_CONST

#define CONSTRUCTOR_TYPED(ret, class, ...)      \
  ret                                           \
  METHOD_NAME(class, init) (                    \
    TYPENAME(class) *self, ##__VA_ARGS__)

#define CONSTRUCTOR(class, ...)                 \
  CONSTRUCTOR_TYPED(                            \
    bool,                                       \
    class,                                      \
    ##__VA_ARGS__)

#define DESTRUCTOR(class)                       \
  void                                          \
  METHOD_NAME(class, finalize) (                \
    TYPENAME(class) *self)

#define INSTANCER(class, ...)                   \
  TYPENAME(class) *                             \
  METHOD_NAME(class, new) (__VA_ARGS__)

#define COPY_INSTANCER(class, ...)              \
  METHOD_CONST(                                 \
    class,                                      \
    TYPENAME(class) *,                          \
    dup,                                        \
    ##__VA_ARGS__)

#define COLLECTOR(class)                        \
  void                                          \
  METHOD_NAME(class, destroy) (                 \
    TYPENAME(class) *self)

#define ALLOCATE_MANY_CATCH(dest, len, type, action)        \
  if ((dest = my_calloc(len, sizeof(type))) == NULL) {      \
    ERROR(                                                  \
      "failed to allocate %lu objects of type \"%s\"\n",    \
      (unsigned long) (len),                                \
      STRINGIFY(type));                                     \
    action;                                                 \
  }

#define ALLOCATE_CATCH(dest, type, action)                  \
  if ((dest = my_calloc(1, sizeof(type))) == NULL) {        \
    ERROR(                                                  \
      "failed to allocate one object of type \"%s\"\n",     \
      STRINGIFY(type));                                     \
    action;                                                 \
  }

#define MAKE_CATCH(dest, class, action, ...)                \
  if ((dest = JOIN(class, _new)(__VA_ARGS__)) == NULL) {    \
    ERROR(                                                  \
      "failed to create instance of class \"%s\"\n",        \
      STRINGIFY(class));                                    \
    action;                                                 \
  }

#define CONSTRUCT_CATCH(class, dest, action, ...)           \
  if (!JOIN(class, _init)(dest, ##__VA_ARGS__)) {           \
    ERROR(                                                  \
      "failed to call constructor of class \"%s\"\n",       \
      STRINGIFY(class));                                    \
    action;                                                 \
  }

#define DESTRUCT(class, dest) JOIN(class, _finalize) (dest)
#define DISPOSE(class, dest)  JOIN(class, _destroy) (dest)

#define TRYCATCH(expr, action)          \
  if (!(expr)) {                        \
    ERROR(                              \
      "exception in \"%s\" (%s:%d)\n",  \
      STRINGIFY(expr),                  \
      __FILENAME__,                     \
      __LINE__);                        \
      action;                           \
  }

/* Macros for "goto done" style error recovery */
#define TRY(expr)   TRYCATCH(expr, goto done)
#define TRYC(expr)  TRY((expr) != -1)
#define TRYZ(expr)  TRY((expr) == 0)

#define ALLOCATE_MANY(dest, len, type)                     \
  ALLOCATE_MANY_CATCH(dest, len, type, goto done)

#define ALLOCATE(dest, type)                               \
  ALLOCATE_CATCH(dest, type, goto done)

#define MAKE(dest, class, ...)                             \
  MAKE_CATCH(dest, class, goto done, __VA_ARGS__)

#define CONSTRUCT(class, dest, ...)                        \
  CONSTRUCT_CATCH(class, dest, goto done, __VA_ARGS__)

/* Macros for "goto fail" style error recovery */
#define TRY_FAIL(expr)   TRYCATCH(expr, goto fail)
#define TRYC_FAIL(expr)  TRY_FAIL((expr) != -1)
#define TRYZ_FAIL(expr)  TRY_FAIL((expr) == 0)

#define ALLOCATE_MANY_FAIL(dest, len, type)                \
  ALLOCATE_MANY_CATCH(dest, len, type, goto fail)

#define ALLOCATE_FAIL(dest, type)                          \
  ALLOCATE_CATCH(dest, type, goto fail)

#define MAKE_FAIL(dest, class, ...)                        \
  MAKE_CATCH(dest, class, goto fail, __VA_ARGS__)

#define CONSTRUCT_FAIL(class, dest, ...)                   \
  CONSTRUCT_CATCH(class, dest, goto fail, ##__VA_ARGS__)

#endif /* _DEFS_H */
