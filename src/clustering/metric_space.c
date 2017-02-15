/**
 * \file metric_space.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2017
 */

/*
 * Copyright (C) 2016 CESNET
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


#include "metric_space.h"
#include "vp_tree.h"
#include "output.h"
#include "print.h"

#include <string.h>
#include <assert.h>
#include <inttypes.h>
#include <float.h>
#include <omp.h>


#define FEATURE_CNT 6.0


/**
 * \defgroup point_group Point related objects
 * @{
 */
static void point_metadata_print(const struct point_metadata *pm)
{
        printf("cluster_id = %d, core = %d, covered_by = %zu, "
                        "quality = %f, representative = %d, cov_cnt = %zu",
                        pm->cluster_id,
                        pm->core,
                        pm->covered_by,
                        pm->quality,
                        pm->representative,
                        pm->cov_cnt);
}

static void point_features_print(const struct point_features *pf)
{
        printf("%s", sprint_rec((const uint8_t *)pf));
}

void point_metadata_clear(struct point_metadata *pm)
{
        memset(pm, 0, sizeof (*pm));
        //pm->id = 0;
        pm->cluster_id = CLUSTER_UNVISITED;
        //pm->core = false;
        pm->covered_by = SIZE_MAX;
        //pm->quality = 0.0;

        //pm->representative = false;
        pm->cov_cnt = 1;
}

void point_print(const struct point *p)
{
        point_metadata_print(&p->m);
        putchar('\t');
        point_features_print(&p->f);
        putchar('\n');
}

/*
 * Eps-neighborhood is an open set (i.e. <, not <=).
 * This function fills eps_vec with all unvisited and noise points within a
 * point's eps-neighborhood (including the point itsef).
 */
//TODO: change to metric_space related function
size_t point_eps_neigh(struct point *point, const struct vector *point_vec,
                struct vector *eps_vec, struct distance *distance,
                const double eps)
{
        size_t cov_cnt_sum = 0;
        const double *distance_vector = distance_get_vector(distance, point);

        /* Clear the result vector. */
        vector_clear(eps_vec);

        /* Find and store eps-neighbors of a certain point. */
        for (size_t i = 0; i < point_vec->size; ++i) {
                const double dist = distance_vector[i];

                if (dist < eps) {
                        struct point *neigbor =
                                (struct point *)point_vec->data + i;

                        cov_cnt_sum += neigbor->m.cov_cnt;
                        if (neigbor->m.covered_by == SIZE_MAX) {
                                vector_add(eps_vec, &neigbor);
                        }
                }
        }

        return cov_cnt_sum;
}
/**
 * @}
 */ //point_group


/**
 * \defgroup distance_group Distance related objects
 * @{
 */
//TODO: generalize to metric_space or something like that
struct distance {
        size_t size;
        bool have_matrix;
        struct point *point_data;
        union {
                double **matrix;
                double *vector;
        } data;
};


static double distance_function_rand(void)
{
        return (double)rand() / RAND_MAX;
}


/*
 * Distance matrix with a following properties:
 * 1. the entries on the main diagonal are all zero                 x[i][i] == 0
 * 2. the matrix is a symmetric matrix                        x[i][j] == x[j][i]
 * 3. the triangle inequality             for all k x[i][j] <= x[i][k] + x[k][j]
 */
struct distance * distance_init(size_t size, struct point *point_data)
{
        struct distance *d = malloc_wr(1, sizeof (*d), true);

        d->size = size;
        d->point_data = point_data;

        /* Allocate space for one row of pointers plus row * row dist matrix. */
        d->data.matrix = malloc_wr(d->size * sizeof (double *) +
                        (d->size * d->size * sizeof (double)), 1, false);

        if (d->data.matrix == NULL) {
                PRINT_WARNING(E_MEM, 0, "not enough memory for distance matrix, falling back to on-demand calculation");

                /* Allocate only one row for distance vector. */
                d->data.vector = malloc_wr(d->size , sizeof (double), true);
                d->have_matrix = false;
        } else {
                for (size_t i = 0; i < d->size; ++i) {
                        d->data.matrix[i] = (double *)(d->data.matrix +
                                        d->size + (i * d->size));
                }
                d->have_matrix = true;
        }

        return d;
}

/* Free the distance structure. */
void distance_free(struct distance *d)
{
        assert(d != NULL);
        if (d->have_matrix) {
                free(d->data.matrix);
        } else {
                free(d->data.vector);
        }
}

/*
 * Calculate distance function f: P^2 -> [0, 1].
 * extern inline in the C99 will make the function inlined for the translation
 * unit where the definition is (it's just a hint though) and will emmit the
 * object at the same time.
 */
//extern inline double distance_function(const struct point *point1,
//                const struct point *point2)
//{
//        double distance = FEATURE_CNT;
//
//        /* After the logical negation, 0 means different, 1 means same. */
//        /* Ports: */
//        distance -= (point1->f.srcport == point2->f.srcport);
//        distance -= (point1->f.dstport == point2->f.dstport);
//
//        /* TCP flags: */
//        distance -= (point1->f.tcp_flags == point2->f.tcp_flags);
//
//        /* Addresses: */
//        distance -= !memcmp(&point1->f.srcaddr, &point2->f.srcaddr,
//                        sizeof (point1->f.srcaddr));
//        distance -= !memcmp(&point1->f.dstaddr, &point2->f.dstaddr,
//                        sizeof (point1->f.dstaddr));
//
//        /* Protocol: */
//        distance -= (point1->f.proto == point2->f.proto);
//
//        return distance / FEATURE_CNT;
//}
extern inline double distance_function(const struct point *point1,
                const struct point *point2)
{
        size_t hd = 0; //hamming distance
        double distance = 0.0;

        //printf("\n\nDistance: \n");
        //point_print(point1);
        //point_print(point2);

        /* After the logical negation, 0 means different, 1 means same. */
        /* Ports: */
        distance += (double)abs(point1->f.srcport - point2->f.srcport) / UINT16_MAX;
        distance += (double)abs(point1->f.dstport - point2->f.dstport) / UINT16_MAX;
        //printf("srcport: dist = %f\n", (double)abs(point1->f.srcport - point2->f.srcport) / UINT16_MAX);
        //printf("dstport: dist = %f\n", (double)abs(point1->f.dstport - point2->f.dstport) / UINT16_MAX);

        /* TCP flags: */
        hd = __builtin_popcount(point1->f.tcp_flags ^ point2->f.tcp_flags);
        distance += (double)hd / (sizeof (point1->f.tcp_flags) * 8);
        //printf("TCP flags: HD = %zu, dist = %f\n", hd, (double)hd / (sizeof (point1->f.tcp_flags) * 8));

        /* Addresses: */
        hd = 0;
        for (size_t i = 0; i < sizeof (point1->f.srcaddr) / sizeof (unsigned int); ++i) {
               hd += __builtin_popcount(((const unsigned int *)&point1->f.srcaddr)[i] ^
                        ((const unsigned int *)&point2->f.srcaddr)[i]);
        }
        distance += (double)hd / (sizeof (point1->f.srcaddr) * 8);
        //printf("srcaddr: HD = %zu, dist = %f\n", hd, (double)hd / (sizeof (point1->f.srcaddr) * 8));

        hd = 0;
        for (size_t i = 0; i < sizeof (point1->f.dstaddr) / sizeof (unsigned int); ++i) {
               hd += __builtin_popcount(((const unsigned int *)&point1->f.dstaddr)[i] ^
                        ((const unsigned int *)&point2->f.dstaddr)[i]);
        }
        distance += (double)hd / (sizeof (point1->f.dstaddr) * 8);
        //printf("dstaddr: HD = %zu, dist = %f\n", hd, (double)hd / (sizeof (point1->f.dstaddr) * 8));

        /* Protocol: */
        distance += !(point1->f.proto == point2->f.proto);
        //printf("proto: dist = %d\n", !(point1->f.proto == point2->f.proto));

        //printf("dist = %f (%f)", distance, distance / FEATURE_CNT);
        return distance;
}

/* Fill the distance matrix. */
void distance_fill(struct distance *d)
{
        assert(d != NULL);
        if (!d->have_matrix) { //no distance matrix, only distance vector
                return;
        }

        #pragma omp parallel for schedule(guided) firstprivate(d)
        for (size_t i = 0; i < d->size; ++i) {
                d->data.matrix[i][i] = 0.0; //property 1

                for (size_t j = 0; j < i; ++j) {
                        const double distance = distance_function(
                                        d->point_data + i, d->point_data + j);

                        d->data.matrix[i][j] = distance;
                        d->data.matrix[j][i] = distance; //property 2: symmetry
                }
        }

        /**********************************************************************/
        double test_eps = (double)2 / 6 + 0.00000000001;
        //size_t test_point = 10;
        double wtime;

        struct vp_node *vp_tree_root = vp_tree_init(d->point_data, d->size);
        //vp_tree_print(vp_tree_root);
        printf("height = %zu\n", vp_tree_height(vp_tree_root));
        return;

        wtime = -omp_get_wtime();
        for (size_t test_point = 0; test_point < d->size; ++test_point) {
                vp_tree_range_query(vp_tree_root, d->point_data + test_point, test_eps);
                putchar('\n');
        }
        wtime += omp_get_wtime();
        printf("vp_tree_range_query took %f\n", wtime);
        vp_tree_free(vp_tree_root);


        wtime = -omp_get_wtime();
        for (size_t test_point = 0; test_point < d->size; ++test_point) {
                for (size_t i = 0; i < d->size; ++i) {
                        if (d->data.matrix[test_point][i] < test_eps) {
                                printf("%zu ", i);
                        }
                }
                putchar('\n');
        }
        wtime += omp_get_wtime();
        printf("distance_matrix_range_query took %f\n", wtime);
        /**********************************************************************/
}

const double * distance_get_vector(const struct distance *d,
                const struct point *point)
{
        assert(d != NULL);

        if (d->have_matrix) {
                return d->data.matrix[point->id];
        } else {
                #pragma omp parallel for firstprivate(d, point)
                for (size_t i = 0; i < d->size; ++i) {
                        d->data.vector[i] = distance_function(point,
                                        d->point_data + i);
                }
                return d->data.vector;
        }
}

void distance_print(const struct distance *d)
{
        assert(d != NULL);
        if (!d->have_matrix) { //no distance matrix, only distance vector
                return;
        }

        for (size_t i = 0; i < d->size; ++i) {
                for (size_t j = 0; j < d->size; ++j) {
                        printf("%f\n", d->data.matrix[i][j]);
                }
                //putchar('\n');
        }
}

void distance_pmf(const struct distance *d)
{
#define buckets 100
        //const size_t buckets = 10;
        size_t pmf[buckets] = { 0 };

        double distance_max = 0.0;
        for (size_t i = 0; i < d->size; ++i) {
                for (size_t j = 0; j < d->size; ++j) {
                        MAX_ASSIGN(distance_max, d->data.matrix[i][j]);
                }
        }
        const double bucket_size = distance_max / buckets;
        distance_max += 10 * DBL_EPSILON;

        //const double normalizator = (double)buckets / distance_max;
        for (size_t i = 0; i < d->size; ++i) {
                for (size_t j = 0; j < d->size; ++j) {
                        double idx = d->data.matrix[i][j] / distance_max;
                        idx *= buckets;
                        //printf("idx = %f, normalizator = %f\n", idx, normalizator);
                        assert(idx >= 0.0 && idx < buckets);
                        pmf[(size_t)idx]++;
                }
        }

        for (size_t i = 0; i < buckets; ++i) {
                double low_end = i * bucket_size;
                double upp_end = low_end + bucket_size;
                double probability = (double)pmf[i] / (d->size * d->size);

                printf("%zu: [%.2f, %.2f) = %f (%zu)\n", i, low_end, upp_end,
                                probability, pmf[i]);
        }
}

/*
 * Check if the distance matrix properties are true:
 * 1. the entries on the main diagonal are all zero                 x[i][i] == 0
 * 2. the matrix is a symmetric matrix                        x[i][j] == x[j][i]
 * 3. the triangle inequality             for all k x[i][j] <= x[i][k] + x[k][j]
 */
bool distance_validate(const struct distance *d)
{
        bool ret = true;

        assert(d != NULL);
        if (!d->have_matrix) { //no distance matrix, only distance vector
                return true;
        }

        #pragma omp parallel for firstprivate(d)
        for (size_t i = 0; i < d->size; ++i) {
openmp_break:
                if (!ret) {
                        continue; //cannot break from OpenMP for
                }

                if (d->data.matrix[i][i] != 0.0) {
                        PRINT_INFO("distance matrix check: "
                                        "non-zero entry on the main diagonal");
                        ret = false;
                        goto openmp_break;
                }

                for (size_t j = 0; j < d->size; ++j) {
                        if (d->data.matrix[i][j] != d->data.matrix[i][j]) {
                                PRINT_INFO("distance matrix check: "
                                                "matrix is not symmetric");
                                ret = false;
                                goto openmp_break;
                        }

                        for (size_t k = 0; k < d->size; ++k) {
                                if (d->data.matrix[i][j] > d->data.matrix[i][k] +
                                                d->data.matrix[k][j]) {
                                        PRINT_INFO("distance matrix check: "
                                                        "triangle inequality property is not satisfied");
                                        ret = false;
                                        goto openmp_break;
                                }
                        }
                }
        }

        return ret;
}

void distance_ones_perspective(const struct point *the_one,
                struct point *points[], size_t points_size)
{
        assert(the_one != NULL && points != NULL && points_size != 0);

        struct point **points_begin = points;
        struct point **points_end = points + points_size;
        for (struct point **point = points_begin; point < points_end; ++point) {
                (*point)->m.quality = distance_function(the_one, *point);
        }
}
/**
 * @}
 */ //distance_group
