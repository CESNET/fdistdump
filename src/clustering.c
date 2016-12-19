/**
 * \file clustering.c
 * \brief
 * \author Jan Wrona, <wrona@cesnet.cz>
 * \date 2016
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


#include "common.h"
#include "clustering.h"
#include "slave.h"
#include "path_array.h"
#include "vector.h"
#include "output.h"
#include "print.h"

#include <string.h> //strlen()
#include <assert.h>
#include <stdbool.h>
#include <time.h>
#include <errno.h>
#include <limits.h> //PATH_MAX
#include <unistd.h> //access
#include <time.h>
#include <arpa/inet.h>
#include <float.h>
#include <math.h>

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <mpi.h>
#include <libnf.h>


#define EPS 0.6
#define MIN_PTS 10

#define CLUSTER_UNVISITED 0 //point wasn't visited by the algorithm yet
#define CLUSTER_NOISE     1 //point doesn't belong to any cluster yet
#define CLUSTER_FIRST     2 //first no-noise cluster


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;
static struct {
        double wtime_sum_cur;
        double wtime_sum_prev;

        int num_threads;
        size_t measurement_cnt;
        bool finished;
} thread_num_ctx;


struct master_task_ctx {
        /* Master and slave shared task context. */
        struct shared_task_ctx shared;

        /* Master specific task context. */
        size_t slave_cnt;
        struct output_params output_params;
};

/* Thread shared. */
struct slave_task_ctx {
        /* Master and slave shared task context. Received from master. */
        struct shared_task_ctx shared; //master-slave shared

        /* Slave specific task context. */
        lnf_filter_t *filter; //LNF compiled filter expression

        struct processed_summ processed_summ; //processed data statistics
        struct metadata_summ metadata_summ; //metadata statistics
        char *path_str; //file/directory/profile(s) path string
        size_t proc_rec_cntr; //processed record counter
        bool rec_limit_reached; //true if rec_limit records read
        size_t slave_cnt; //slave count

        lnf_file_t *file; //LNF file
        lnf_rec_t *rec; //LNF record
};

/* Point's metadata. */
/* TODO: shrink into bit array. */
struct point_metadata {
        size_t id;             //point ID
        size_t cluster_id;     //ID of cluster the point belongs to
        bool core;             //core-point flag
        tri_state_t spec_core; //specific core-point
        double spec_eps;
} __attribute__((packed));

/* Point's feature vector. */
struct point_features {
        uint16_t srcport;
        uint16_t dstport;
        uint8_t proto;
        lnf_ip_t srcaddr;
        lnf_ip_t dstaddr;
} __attribute__((packed));

struct point {
        struct point_metadata m;
        struct point_features f;
} __attribute__((packed));


/**
 * \defgroup slave_fun Slave functions
 * @{
 */
static void thread_num_init(void)
{
        thread_num_ctx.wtime_sum_cur = 0.0;
        thread_num_ctx.wtime_sum_prev = DBL_MAX;

        thread_num_ctx.num_threads = omp_get_max_threads();
        thread_num_ctx.measurement_cnt = 0;
        thread_num_ctx.finished = false;
}

static void thread_num_start(void)
{
        if (thread_num_ctx.finished) {
                return;
        }

        /* Set number of threads and start measuring time. */
        omp_set_num_threads(thread_num_ctx.num_threads);
        thread_num_ctx.wtime_sum_cur = -omp_get_wtime();
}

static void thread_num_stop(void)
{
        if (thread_num_ctx.finished) {
                return;
        }

        /* Stop measuring time. */
        thread_num_ctx.wtime_sum_cur += omp_get_wtime();

        if (++thread_num_ctx.measurement_cnt == 10) {
                PRINT_DEBUG("wtime for %d threads and %zu runs was %f s",
                                thread_num_ctx.num_threads,
                                thread_num_ctx.measurement_cnt,
                                thread_num_ctx.wtime_sum_cur);

                /*
                 * If wtime rose, revert number threads to the previous value
                 * and stop furthe measurement.
                 * If wtime fell, decrese number of threads and prepare for
                 * next measurement.
                 */
                if (thread_num_ctx.wtime_sum_cur >
                                thread_num_ctx.wtime_sum_prev) {
                        thread_num_ctx.num_threads++; //revert to the prev. val.
                        thread_num_ctx.finished = true;

                        PRINT_DEBUG("found %d as a fastest threads count",
                                        thread_num_ctx.num_threads);
                } else {
                        thread_num_ctx.num_threads--; //try less threads
                        thread_num_ctx.measurement_cnt = 0;
                        thread_num_ctx.wtime_sum_prev = thread_num_ctx.wtime_sum_cur;
                        thread_num_ctx.wtime_sum_cur = 0.0;
                }

                if (thread_num_ctx.num_threads == 1) {
                        thread_num_ctx.finished = true;

                        PRINT_DEBUG("found %d as a fastest threads count",
                                        thread_num_ctx.num_threads);
                }
        }
}


double distance_function(const struct point *point1, const struct point *point2)
{
        double distance = 5.0;

        /* After the logical negation, 0 means different, 1 means same. */
        distance -= !memcmp(&point1->f.srcaddr, &point2->f.srcaddr,
                        sizeof (point1->f.srcaddr));
        distance -= !memcmp(&point1->f.dstaddr, &point2->f.dstaddr,
                        sizeof (point1->f.dstaddr));
        distance -= (point1->f.srcport == point2->f.srcport);
        distance -= (point1->f.dstport == point2->f.dstport);
        distance -= (point1->f.proto == point2->f.proto);

        //printf("\n");
        //print_rec((const uint8_t *)&point1->f);
        //print_rec((const uint8_t *)&point2->f);
        //printf("%f\n", distance / 5.0);

        return distance / 5.0;
}

double distance_function_rand(const struct point *point1, const struct point *point2)
{
        (void)point1;
        (void)point2;
        //srand(time(NULL));
        return (double)rand() / RAND_MAX;
}


/*
 * Distance matrix with a following properties:
 * 1. the entries on the main diagonal are all zero                 x[i][i] == 0
 * 2. the matrix is a symmetric matrix                        x[i][j] == x[j][i]
 * 3. the triangle inequality             for all k x[i][j] <= x[i][k] + x[k][j]
 */
static double ** distance_matrix_init(size_t size)
{
        /* Allocate space for one row of pointers plus row * row dist matrix. */
        double **dm = malloc_wr(size * sizeof (*dm) +
                        (size * size * sizeof (**dm)), 1, false);

        if (dm == NULL) {
                PRINT_WARNING(E_MEM, 0, "not enough memory for distance matrix");

                /* Allocate only one row for distance vector. */
                dm = malloc_wr(sizeof (*dm) + (size * sizeof (**dm)), 1, true);
                dm[0] = NULL;
        } else {
                for (size_t row = 0; row < size; ++row) {
                        dm[row] = (double *)(dm + size + (row * size));
                }
        }

        return dm;
}

/* Free the distance matrix. */
void distance_matrix_free(double **dm)
{
        free(dm);
}

/* Fill the distance matrix. */
static void distance_matrix_fill(double **dm, size_t size,
                const struct point *point_data)
{
        if (dm[0] == NULL) { //no distance matrix, only distance vector
                return;
        }

        #pragma omp parallel for firstprivate(dm, size, point_data)
        for (size_t row = 0; row < size; ++row) {
                dm[row][row] = 0.0; //property 1: zero on the main diagonal

                for (size_t col = 0; col < row; ++col) {
                        const double distance = distance_function(
                                        point_data + row, point_data + col);

                        dm[row][col] = distance;
                        dm[col][row] = distance; //property 2: symmetry
                }
        }

        /* Print. */
        //for (size_t row = 0; row < size; ++row) {
        //        for (size_t col = 0; col < size; ++col) {
        //                printf("%.1f ", dm[row][col]);
        //        }
        //        putchar('\n');
        //}
}

/*
 * Check if the distance matrix properties are true:
 * 1. the entries on the main diagonal are all zero                 x[i][i] == 0
 * 2. the matrix is a symmetric matrix                        x[i][j] == x[j][i]
 * 3. the triangle inequality             for all k x[i][j] <= x[i][k] + x[k][j]
 */
static bool distance_matrix_validate(double **dm, size_t size)
{
        bool ret = true;

        #pragma omp parallel for firstprivate(dm, size)
        for (size_t i = 0; i < size; ++i) {
openmp_break:
                if (!ret) {
                        continue; //cannot break from OpenMP for
                }

                if (dm[i][i] != 0.0) { //1. zero on the diagonal
                        PRINT_INFO("distance matrix check: "
                                        "non-zero entry on the main diagonal");
                        ret = false;
                        goto openmp_break;
                }

                for (size_t j = 0; j < size; ++j) {
                        if (dm[i][j] != dm[i][j]) { //2. symmety
                                PRINT_INFO("distance matrix check: "
                                                "matrix is not symmetric");
                                ret = false;
                                goto openmp_break;
                        }

                        for (size_t k = 0; k < size; ++k) {
                                if (dm[i][j] > dm[i][k] + dm[k][j]) { //3. TI
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


/*
 * Return all points within a point's eps-neighborhood (including the
 * point). Eps-neighborhood is an open set (i.e. <, not <=).
 */
static void eps_get_neigh(struct vector *eps_vec, const struct point *point,
                double **distance_matrix, const struct vector *point_vec,
                double *max_distance)
{
        const struct point *point_data = (const struct point *)point_vec->data;
        double *distance_vec;
        static bool thread_num_initialized = false;

        /* Clear the result vector. */
        vector_clear(eps_vec);
        *max_distance = 0;

        if (distance_matrix[0] == NULL) {
                if (!thread_num_initialized) {
                        thread_num_init();
                        thread_num_initialized = true;
                }

                distance_vec = (double *)distance_matrix + 1;

                thread_num_start();
                #pragma omp parallel for firstprivate(distance_vec, point_data)
                for (size_t i = 0; i < point_vec->size; ++i) {
                        distance_vec[i] = distance_function(
                                        point_data + point->m.id, point_data + i);
                }
                thread_num_stop();
        } else {
                distance_vec = distance_matrix[point->m.id];
        }

        /* Find and store eps-neighbors of a certain point. */
        for (size_t i = 0; i < point_vec->size; ++i) {
                const double distance = distance_vec[i];

                if (distance < EPS) {
                        struct point *neigbor = vector_get_ptr(point_vec, i);

                        vector_add(eps_vec, &neigbor); //store only the pointer
                        MAX_ASSIGN(*max_distance, distance);
                }
        }
}

/*
 * Set all points' specific core point attribute in the eps-neighborhood to
 * false.
 */
static void eps_set_spec_core_false(struct vector *eps_vec)
{
        for (size_t i = 0; i < eps_vec->size; ++i) {
                struct point *point = ((struct point **)eps_vec->data)[i];
                point->m.spec_core = TS_FALSE;
        }
}

/*
 * Comparing addresses is fine because struct vector uses continuous memory.
 * Destination vector consist 1 to N non-descending sequences.
 */
static bool eps_merge(struct vector *dest_vec, const struct vector *new_vec)
{
        /* Iterators. */
        const struct point **dest_it = (const struct point **)dest_vec->data;
        const struct point **dest_it_end = dest_it + dest_vec->size;

        //printf("\n\nNEW EPS MERGE\n");
        /* Loop through destination vector. */
        while (dest_it < dest_it_end) {
                const struct point **new_it = (const struct point **)new_vec->data;
                const struct point **new_it_end = new_it + new_vec->size;
                const struct point *dest_last = *dest_it;

                //printf("BEF_SEQ: dest = %d, new = %d, seq = %d\n",
                //                dest_it < dest_it_end,  //dest didn't read end
                //                new_it < new_it_end,    //new didn't reach end
                //                dest_last <= *dest_it); //seq didn't reach end

                while (dest_it < dest_it_end && //dest didn't reach end
                                new_it < new_it_end && //new didn't reach end
                                dest_last <= *dest_it) //seq didn't reach end
                {
                        dest_last = *dest_it;

                        /* TODO: possible optimization when whole new is NULL
                         * (dest = 1, new = 0, seq = 1)
                         */
                        if (*new_it == NULL) {
                                new_it++;
                                continue;
                        }

                        if (*dest_it == *new_it) {
                                *new_it = NULL; //new is in this seq, move both
                                dest_it++;
                                new_it++;
                        } else if (*dest_it < *new_it) {
                                dest_it++; //new isn't this seq yet, move dest
                        } else /* (*dest_it > *new_it) */ {
                                new_it++; //new isn't in this seq, move new
                        }
                        //printf("SEQ: dest_it = %lx, *dest_it = %lx, dest_it_end = %lx\n",
                        //                dest_it, *dest_it, dest_it_end);
                }

                //printf("FFD: dest = %d, new = %d, seq = %d\n",
                //                dest_it < dest_it_end,  //dest didn't reach end
                //                new_it < new_it_end,    //new didn't reach end
                //                dest_last <= *dest_it); //seq didn't reach end

                /* Fast forward dest_it to the beginning of the next sequence.*/
                while (dest_it < dest_it_end && //dest didn't reach end
                                dest_last <= *dest_it) //seq didn't reach end
                {
                        dest_last = *dest_it;
                        dest_it++;
                        //printf("FFD: dest_it = %lx, *dest_it = %lx, dest_it_end = %lx\n",
                        //                dest_it, *dest_it, dest_it_end);
                }
                //printf("ALL: dest_it = %lx, *dest_it = %lx, dest_it_end = %lx\n",
                //                dest_it, *dest_it, dest_it_end);
        }

        /* Add remaining elements from new to the dest. */
        for (size_t i = 0; i < new_vec->size; ++i) {
                struct point *point = ((struct point **)new_vec->data)[i];
                if (point != NULL) {
                        vector_add(dest_vec, &point);
                }
        }

        return true;
}


static size_t dbscan(const struct vector *point_vec, double **distance_matrix)
{
        size_t cluster_id = CLUSTER_FIRST;

        struct vector eps_vec; //vectors of pointers to the record storage
        struct vector new_eps_vec;

        vector_init(&eps_vec, sizeof (struct point *));
        vector_init(&new_eps_vec, sizeof (struct point *));


        /* Loop through all the points. */
        for (size_t i = 0; i < point_vec->size; ++i) {
                struct point *point = (struct point *)point_vec->data + i;
                double max_distance;

                if (point->m.cluster_id != CLUSTER_UNVISITED) {
                        /* The point is a noise or already in some cluster. */
                        continue;
                }

                eps_get_neigh(&eps_vec, point, distance_matrix, point_vec,
                                &max_distance);

                if (eps_vec.size < MIN_PTS) {
                        /* The point is a noise, this may be chaged later. */
                        point->m.cluster_id = CLUSTER_NOISE;
                        vector_clear(&eps_vec);
                        continue;
                }

                /* The point is a core point of a new cluster, expand it. */
                point->m.cluster_id = cluster_id;
                point->m.core = true;
                /* Determination of a local model. */
                eps_set_spec_core_false(&eps_vec);
                point->m.spec_core = TS_TRUE;
                point->m.spec_eps = EPS + max_distance;

                /* Loop through the whole (growing) eps-neighborhood. */
                for (size_t j = 0; j < eps_vec.size; ++j) {
                        struct point *new_point = ((struct point **)eps_vec.data)[j];

                        if (new_point->m.cluster_id == CLUSTER_UNVISITED) {
                                /*
                                 * The point is either a core point (then add
                                 * its eps-neighborhood to the current
                                 * eps-neighborhood) or a border point.
                                 */
                                new_point->m.cluster_id = cluster_id;
                                eps_get_neigh(&new_eps_vec, new_point,
                                                distance_matrix, point_vec,
                                                &max_distance);

                                if (new_eps_vec.size >= MIN_PTS) { //core point
                                        new_point->m.core = true;
                                        if (new_point->m.spec_core == TS_UNSET) {
                                                eps_set_spec_core_false(&new_eps_vec);
                                                new_point->m.spec_core = TS_TRUE;
                                                new_point->m.spec_eps = EPS + max_distance;
                                        }

                                        vector_concat(&eps_vec, &new_eps_vec);
                                        //eps_merge(&eps_vec, &new_eps_vec);
                                } //else the point is a border point

                        } else if (new_point->m.cluster_id == CLUSTER_NOISE) {
                                /* The point was a noise, but isn't anymore. */
                                new_point->m.cluster_id = cluster_id;
                        } //else the point was already part of some cluster
                }

                cluster_id++; //cluster finished, move to the next one
        }

        vector_free(&new_eps_vec);
        vector_free(&eps_vec);

        return cluster_id;
}

static void dbscan_local_model_print(const struct vector *point_vec,
                double **distance_matrix, size_t cluster_cnt)
{
        size_t cluster_size = 0;
        size_t core_size = 0;
        size_t spec_core_size = 0;

        /* Loop through all the clusters. */
        for (size_t cid = CLUSTER_FIRST; cid < cluster_cnt; ++cid) {
                printf("Cluster %zu:\n", cid);

                /* Loop through all the points. */
                for (size_t i = 0; i < point_vec->size; ++i) {
                        struct point *point = (struct point *)point_vec->data + i;

                        if (point->m.cluster_id == cid) {
                                cluster_size++;
                                core_size += point->m.core;
                                spec_core_size += (point->m.spec_core == TS_TRUE);

                                printf("\t%s\tid = %zu, core = %d, spec_core = %d, spec_eps = %f\n",
                                                sprint_rec((const uint8_t *)&point->f),
                                                point->m.id, point->m.core,
                                                point->m.spec_core, point->m.spec_eps);
                        }
                }

                printf("summary: cluster_size = %zu, core_size = %zu, spec_core_size = %zu\n",
                                cluster_size, core_size, spec_core_size);
                cluster_size = core_size = spec_core_size = 0;
        }
}

static bool dbscan_local_model_validate(const struct vector *point_vec,
                double **distance_matrix, size_t cluster_cnt)
{
        /* Loop through all the clusters. */
        for (size_t cid = CLUSTER_FIRST; cid < cluster_cnt; ++cid) {
                for (size_t i = 0; i < point_vec->size; ++i) {
                        struct point *point = (struct point *)point_vec->data + i;

                        if (point->m.cluster_id != cid) {
                                continue;
                        }

                        /* Property 1. */
                        if (point->m.spec_core == TS_TRUE && !point->m.core) {
                                printf("\t1. invalid local model\n");
                                return false;
                        }

                        if (point->m.core) {
                                const double *distance_vec = distance_matrix[point->m.id];
                                size_t spec_in_eps_cnt = 0;

                                for (size_t j = 0; j < point_vec->size; ++j) {
                                        const double distance = distance_vec[j];
                                        struct point *neigbor = vector_get_ptr(point_vec, j);

                                        if (distance < EPS &&
                                                        neigbor->m.cluster_id == cid &&
                                                        neigbor->m.spec_core == TS_TRUE) {
                                                spec_in_eps_cnt++;
                                        }
                                }

                                if (point->m.spec_core == TS_TRUE && spec_in_eps_cnt != 1) {
                                        printf("\t2. invalid local model\n");
                                        return false;
                                } else if (point->m.spec_core != TS_TRUE && spec_in_eps_cnt < 1) {
                                        printf("\t3. invalid local model\n");
                                        return false;
                                }
                        }
                }
        }

        return true;
}

static void dbscan_local_model_send(const struct vector *point_vec)
{
        for (size_t i = 0; i < point_vec->size; ++i) {
                struct point *point = (struct point *)point_vec->data + i;

                if (point->m.spec_core != TS_TRUE) {
                        continue;
                }

                MPI_Send(&point->f, sizeof (point->f), MPI_BYTE, ROOT_PROC,
                                TAG_DATA, MPI_COMM_WORLD);
        }

        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
}


static error_code_t clusterize(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        size_t flows_cnt_meta;

        struct point point; //data point
        struct vector point_vec; //set of data points

        double **distance_matrix = NULL;

        struct output_params op;
        struct field_info fi[LNF_FLD_TERM_];

        op.print_records = OUTPUT_ITEM_YES;
        op.format = OUTPUT_FORMAT_PRETTY;
        op.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        op.ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;

        memset(fi, 0, sizeof (fi));
        fi[LNF_FLD_SRCADDR].id = LNF_FLD_SRCADDR;
        fi[LNF_FLD_DSTADDR].id = LNF_FLD_DSTADDR;
        fi[LNF_FLD_SRCPORT].id = LNF_FLD_SRCPORT;
        fi[LNF_FLD_DSTPORT].id = LNF_FLD_DSTPORT;
        fi[LNF_FLD_TCP_FLAGS].id = LNF_FLD_TCP_FLAGS;

        output_setup(op, fi);


        vector_init(&point_vec, sizeof (struct point));
        assert(lnf_info(stc->file, LNF_INFO_FLOWS, &flows_cnt_meta,
                                sizeof (flows_cnt_meta)) == LNF_OK);
        vector_reserve(&point_vec, flows_cnt_meta);


        /**********************************************************************/
        /* Read all records from the file. */
        while ((secondary_errno = lnf_read(stc->file, stc->rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, stc->rec)) {
                        continue;
                }
                file_proc_rec_cntr++;

                point.m.id = file_rec_cntr - 1;
                point.m.cluster_id = CLUSTER_UNVISITED;
                point.m.core = false;
                point.m.spec_core = TS_UNSET;
                point.m.spec_eps = INFINITY;

                lnf_rec_fget(stc->rec, LNF_FLD_SRCADDR, &point.f.srcaddr);
                lnf_rec_fget(stc->rec, LNF_FLD_DSTADDR, &point.f.dstaddr);
                lnf_rec_fget(stc->rec, LNF_FLD_SRCPORT, &point.f.srcport);
                lnf_rec_fget(stc->rec, LNF_FLD_DSTPORT, &point.f.dstport);
                lnf_rec_fget(stc->rec, LNF_FLD_PROT, &point.f.proto);

                vector_add(&point_vec, &point);
        }
        /* Check if we reach end of the file. */
        if (secondary_errno != LNF_EOF) {
                PRINT_WARNING(E_LNF, secondary_errno, "EOF wasn't reached");
        }

        /**********************************************************************/
        /* Create and fill distance matrix. */
        distance_matrix = distance_matrix_init(point_vec.size);
        distance_matrix_fill(distance_matrix, point_vec.size,
                        (struct point *)point_vec.data);
        //distance_matrix_validate(distance_matrix, point_vec.size);

        /**********************************************************************/
        /* DBSCAN */
        const size_t cluster_cnt = dbscan(&point_vec, distance_matrix);
        dbscan_local_model_print(&point_vec, distance_matrix, cluster_cnt);
        dbscan_local_model_validate(&point_vec, distance_matrix, cluster_cnt);
        dbscan_local_model_send(&point_vec);

        /**********************************************************************/
        /* Print results. */
        //putchar('\n');
        //for (size_t i = 0; i < point_vec.size; ++i) {
        //        struct point *p = (struct point *)point_vec.data + i;

        //        printf("%lu: %lu\n", p->m.id, p->m.cluster_id);
        //}
        //putchar('\n');



        /**********************************************************************/
        /* Free the memory. */
        distance_matrix_free(distance_matrix);
        vector_free(&point_vec);

        MPI_Send(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_DATA, MPI_COMM_WORLD);
        PRINT_DEBUG("read %zu, processed %zu", file_rec_cntr,
                        file_proc_rec_cntr);

        return primary_errno;
}

static void task_free(struct slave_task_ctx *stc)
{
        if (stc->filter) {
                lnf_filter_free(stc->filter);
        }
        free(stc->path_str);
}


static error_code_t task_init_filter(lnf_filter_t **filter, char *filter_str)
{
        int secondary_errno;


        assert(filter != NULL && filter_str != NULL && strlen(filter_str) != 0);

        /* Initialize filter. */
        //secondary_errno = lnf_filter_init(filter, filter_str); //old filter
        secondary_errno = lnf_filter_init_v2(filter, filter_str); //new filter
        if (secondary_errno != LNF_OK) {
                PRINT_ERROR(E_LNF, secondary_errno,
                                "cannot initialise filter \"%s\"", filter_str);
                return E_LNF;
        }

        return E_OK;
}


static error_code_t task_receive_ctx(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;


        assert(stc != NULL);

        /* Receive task context, path string and optional filter string. */
        MPI_Bcast(&stc->shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        stc->path_str = calloc_wr(stc->shared.path_str_len + 1, sizeof (char),
                        true);
        MPI_Bcast(stc->path_str, stc->shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (stc->shared.filter_str_len > 0) {
                char filter_str[stc->shared.filter_str_len + 1];

                MPI_Bcast(filter_str, stc->shared.filter_str_len, MPI_CHAR,
                                ROOT_PROC, MPI_COMM_WORLD);

                filter_str[stc->shared.filter_str_len] = '\0'; //termination
                primary_errno = task_init_filter(&stc->filter, filter_str);
                //it is OK not to chech primary_errno
        }


        return primary_errno;
}


static error_code_t process(struct slave_task_ctx *stc, char **paths,
                size_t paths_cnt)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;


        /* Initialize LNF record only once. */
        secondary_errno = lnf_rec_init(&stc->rec);
        if (secondary_errno != LNF_OK) {
                primary_errno = E_LNF;
                PRINT_ERROR(primary_errno, secondary_errno, "lnf_rec_init()");
                goto err;
        }


        /* Loop through all the files. */
        for (size_t i = 0; i < paths_cnt; ++i) {
                if (primary_errno != E_OK) {
                        continue;
                }

                /* Open a flow file. */
                secondary_errno = lnf_open(&stc->file, paths[i], LNF_READ, NULL);
                if (secondary_errno != LNF_OK) {
                        PRINT_WARNING(E_LNF, secondary_errno,
                                        "unable to open file \"%s\"", paths[i]);
                        continue;
                }

                primary_errno = clusterize(stc);

                lnf_close(stc->file);
        }

        /* Free LNF record. */
        lnf_rec_free(stc->rec);
err:

        return primary_errno;
}

error_code_t clustering_slave(int world_size)
{
        error_code_t primary_errno = E_OK;
        struct slave_task_ctx stc;
        char **paths = NULL;
        size_t paths_cnt = 0;


        memset(&stc, 0, sizeof (stc));
        stc.slave_cnt = world_size - 1; //all nodes without master

        /* Wait for reception of task context from master. */
        primary_errno = task_receive_ctx(&stc);
        if (primary_errno != E_OK) {
                goto finalize_task;
        }

        /* Data source specific initialization. */
        paths = path_array_gen(stc.path_str, stc.shared.time_begin,
                        stc.shared.time_end, &paths_cnt);
        if (paths == NULL) {
                primary_errno = E_PATH;
                goto finalize_task;
        }


        primary_errno = process(&stc, paths, paths_cnt);


finalize_task:
        path_array_free(paths, paths_cnt);
        task_free(&stc);

        return primary_errno;
}
/**
 * @}
 */ //slave_fun


/**
 * \defgroup master_fun Master functions
 * @{
 */
static void construct_master_task_ctx(struct master_task_ctx *mtc,
                const struct cmdline_args *args, int world_size)
{
        /* Fill shared content. */
        mtc->shared.working_mode = args->working_mode;
        memcpy(mtc->shared.fields, args->fields,
                        MEMBER_SIZE(struct master_task_ctx, shared.fields));

        mtc->shared.path_str_len =
                (args->path_str == NULL) ? 0 : strlen(args->path_str);

        mtc->shared.filter_str_len =
                (args->filter_str == NULL) ? 0 : strlen(args->filter_str);

        mtc->shared.rec_limit = args->rec_limit;

        mtc->shared.time_begin = args->time_begin;
        mtc->shared.time_end = args->time_end;

        mtc->shared.use_fast_topn = args->use_fast_topn;


        /* Fill master specific content. */
        mtc->slave_cnt = world_size - 1; //all nodes without master
        mtc->output_params = args->output_params;
}
static void recv_loop(size_t slave_cnt)
{
        size_t active_slaves = slave_cnt; //every slave is active
        struct point_features point_features;
        int rec_len = 0;
        MPI_Status status;

        struct vector feature_vec;
        vector_init(&feature_vec, sizeof (struct point_features));

        /* Data receiving loop. */
        while (active_slaves) {
                /* Receive a message from any slave. */
                MPI_Recv(&point_features, sizeof (point_features), MPI_BYTE,
                                MPI_ANY_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                                &status);

                /* Determine actual size of the received message. */
                MPI_Get_count(&status, MPI_BYTE, &rec_len);
                if (rec_len == 0) {
                        active_slaves--;
                        continue; //empty message -> slave finished
                }

                vector_add(&feature_vec, &point_features);
        }

        /**********************************************************************/
        struct output_params op;
        struct field_info fi[LNF_FLD_TERM_];

        op.print_records = OUTPUT_ITEM_YES;
        op.format = OUTPUT_FORMAT_PRETTY;
        op.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        op.ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;

        memset(fi, 0, sizeof (fi));
        fi[LNF_FLD_SRCADDR].id = LNF_FLD_SRCADDR;
        fi[LNF_FLD_DSTADDR].id = LNF_FLD_DSTADDR;
        fi[LNF_FLD_SRCPORT].id = LNF_FLD_SRCPORT;
        fi[LNF_FLD_DSTPORT].id = LNF_FLD_DSTPORT;
        fi[LNF_FLD_TCP_FLAGS].id = LNF_FLD_TCP_FLAGS;

        output_setup(op, fi);
        /**********************************************************************/

        putchar('\n');
        for (size_t i = 0; i < feature_vec.size; ++i) {
                struct point_features *pf =
                        ((struct point_features *)feature_vec.data) + i;

                print_rec((const uint8_t *)pf);
        }
        printf("summary: spec_core_size = %zu\n", feature_vec.size);

        vector_free(&feature_vec);
}


error_code_t clustering_master(int world_size, const struct cmdline_args *args)
{
        error_code_t primary_errno = E_OK;
        struct master_task_ctx mtc; //master task context
        double duration = -MPI_Wtime(); //start time measurement


        memset(&mtc, 0, sizeof (mtc));

        /* Construct master_task_ctx struct from command-line arguments. */
        construct_master_task_ctx(&mtc, args, world_size);
        //output_setup(mtc.output_params, mtc.shared.fields);


        /* Broadcast task context, path string and optional filter string. */
        MPI_Bcast(&mtc.shared, 1, mpi_struct_shared_task_ctx, ROOT_PROC,
                        MPI_COMM_WORLD);

        MPI_Bcast(args->path_str, mtc.shared.path_str_len, MPI_CHAR, ROOT_PROC,
                        MPI_COMM_WORLD);

        if (mtc.shared.filter_str_len > 0) {
                MPI_Bcast(args->filter_str, mtc.shared.filter_str_len,
                                MPI_CHAR, ROOT_PROC, MPI_COMM_WORLD);
        }


        /* Send, receive, process. */
        recv_loop(mtc.slave_cnt);


        duration += MPI_Wtime(); //end time measurement

        return primary_errno;
}
/**
 * @}
 */ //master_fun
