/** Declarations for IP flow metric space creation, queries, indexing and more.
 */

/*
 * Copyright (C) 2017 CESNET
 *
 * LICENSE TERMS
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 * 3. Neither the name of the Company nor the names of its contributors
 *    may be used to endorse or promote products derived from this
 *    software without specific prior written permission.
 *
 * ALTERNATIVELY, provided that this notice is retained in full, this
 * product may be distributed under the terms of the GNU General Public
 * License (GPL) version 2 or later, in which case the provisions
 * of the GPL apply INSTEAD OF those given above.
 *
 * This software is provided ``as is'', and any express or implied
 * warranties, including, but not limited to, the implied warranties of
 * merchantability and fitness for a particular purpose are disclaimed.
 * In no event shall the company or contributors be liable for any
 * direct, indirect, incidental, special, exemplary, or consequential
 * damages (including, but not limited to, procurement of substitute
 * goods or services; loss of use, data, or profits; or business
 * interruption) however caused and on any theory of liability, whether
 * in contract, strict liability, or tort (including negligence or
 * otherwise) arising in any way out of the use of this software, even
 * if advised of the possibility of such damage.
 *
 */

#pragma once


#include "common.h"
#include "vector.h"

#include <stddef.h> //size_t


#define CLUSTER_UNVISITED -1 //point wasn't visited by the algorithm yet
#define CLUSTER_NOISE      0 //point doesn't belong to any cluster yet
#define CLUSTER_FIRST      1 //first no-noise cluster


/* TODO: cleanup the data types (int -> ssize_t, exact width, ...) */
/* Point's features. */
struct point_features {
        uint16_t srcport;
        uint16_t dstport;
        uint8_t tcp_flags;
        lnf_ip_t srcaddr;
        lnf_ip_t dstaddr;
        uint8_t proto;
} __attribute__((packed)); //TODO: this is because of sprint_rec

struct point_clustering {
        int cluster_id;    //ID of cluster the point belongs to
        bool core;         //core point flag
        size_t covered_by; //ID of the representative covering this point
        double quality;
};

struct point {
        size_t id; //point identification
        size_t cov_cnt; //number of points represented by this point
        struct point_features f; //feature vector

        union {
                struct point_clustering clustering;
        };

};

struct metric_space {
        struct point *point_storage;
        const size_t size;
};

struct distance; //forward declaration


void point_print(const struct point *p);
size_t point_eps_neigh(struct point *point, const struct point *point_storage,
                size_t point_storage_size, struct vector *eps_vec,
                struct distance *distance, const double eps);


struct distance * distance_init(size_t size, struct point *point_data);
void distance_free(struct distance *d);
double distance_function(const struct point *point1, const struct point *point2);
void distance_fill(struct distance *d);

const double * distance_get_vector(const struct distance *d,
                const struct point *point);

void distance_print(const struct distance *d);
void distance_pmf(const struct distance *d);
bool distance_validate(const struct distance *d);

void distance_ones_perspective(const struct point *the_one,
                struct point *points[], size_t points_size);


struct metric_space * metric_space_init(void);
void metric_space_free(struct metric_space *ms);
error_code_t metric_space_load(struct metric_space *ms, char **paths,
                size_t paths_cnt, const lnf_filter_t *lnf_filter);

