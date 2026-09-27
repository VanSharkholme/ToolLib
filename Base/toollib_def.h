//
// Created by VanSharkholme on 2026/9/25.
//

#ifndef TOOLLIB_TOOLLIB_DEF_H
#define TOOLLIB_TOOLLIB_DEF_H

#if defined(__GNUC__)
#ifndef __weak
#define __weak __attribute__((weak))
#endif
#endif

#if !defined(UNUSED)
#define UNUSED(x) ((void)(x))
#endif


#endif //TOOLLIB_TOOLLIB_DEF_H
