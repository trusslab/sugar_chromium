/* Copyright (C) 2016-2018 University of California, Irvine
 *
 * Authors:
 * Zhihao Yao <z.yao@uci.edu>
 * Ardalan Amiri Sani <arrdalan@gmail.com>
 * 
 * All rights reserved. Use of this source code is governed
 * by a BSD-style license that can be found in the LICENSE file.
 */

#ifndef _PRINTS_H_
#define _PRINTS_H_

#define FPRINTF0(des, fmt, args...)            /*fprintf(des, fmt, ##args)*/
#define FPRINTF1(fmt, args...)                 /*fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args)*/
#define FPRINTF2(fmt, args...)                 /*fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args)*/
#define FPRINTF3(des, fmt, args...)            /*fprintf(des, fmt, ##args)*/
#define FPRINTF_COND0(cond, fmt, args...)      /*{if (cond) fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args);}*/
#define FPRINTF_COND1(cond, fmt, args...)      /*{if (cond) fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args);}*/
#define FPRINTF_COND2(cond, des, fmt, args...) /*{if (cond) fprintf(des, fmt, ##args);}*/

#define FPRINTF4(des, fmt, args...)            /*fprintf(des, fmt, ##args)*/
#define FPRINTF5(fmt, args...)                 /*fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args)*/

#define FPRINTF6(fmt, args...)                 /*fprintf(stderr, "%s: " fmt, __PRETTY_FUNCTION__, ##args)*/

#define STACK_TRACE0()                         /*base::debug::StackTrace().Print()*/

#define INTEL_GPU_DEV_FILE_NAME		"/dev/dri/card0"

#endif /* _PRINTS_H_ */
