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

#ifdef _OPENMP
#include <omp.h>
#endif //_OPENMP
#include <mpi.h>
#include <libnf.h>


#define EPS 0.6
#define MIN_PTS 10


/* Global variables. */
extern MPI_Datatype mpi_struct_shared_task_ctx;


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


#define CLUSTER_UNVISITED 0 //point wasn't visited by the algorithm yet
#define CLUSTER_NOISE     1 //point doesn't belong to any cluster yet
#define CLUSTER_FIRST     2 //first no-noise cluster
struct rec {
        size_t id; //point ID
        size_t cluster_id; //ID of cluster the point belongs to
        //bool spec_core; //specific core-point flag

        /* Feature vector. */
        uint16_t srcport;
        uint16_t dstport;
        uint8_t proto;
        lnf_ip_t srcaddr;
        lnf_ip_t dstaddr;
} __attribute__((packed));


static struct {
        double wtime_sum_cur;
        double wtime_sum_prev;

        int num_threads;
        size_t measurement_cnt;
        bool finished;
} thread_num_ctx;

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


double distance_function(const struct rec *point1, const struct rec *point2)
{
        double distance = 5.0;

        /* After the logical negation, 0 means different, 1 means same. */
        distance -= !memcmp(&point1->srcaddr, &point2->srcaddr,
                        sizeof (point1->srcaddr));
        distance -= !memcmp(&point1->dstaddr, &point2->dstaddr,
                        sizeof (point1->dstaddr));
        distance -= (point1->srcport == point2->srcport);
        distance -= (point1->dstport == point2->dstport);
        distance -= (point1->proto == point2->proto);

        //printf("\n");
        //print_rec((const uint8_t *)&point1->srcport);
        //print_rec((const uint8_t *)&point2->srcport);
        //printf("%f\n", distance / 5.0);

        return distance / 5.0;
}

double distance_function_rand(const struct rec *rec1, const struct rec *rec2)
{
        (void)rec1;
        (void)rec2;
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
                const struct rec *rec_data)
{
        if (dm[0] == NULL) { //no distance matrix, only distance vector
                return;
        }

        #pragma omp parallel for firstprivate(dm, size, rec_data)
        for (size_t row = 0; row < size; ++row) {
                dm[row][row] = 0.0; //property 1: zero on the main diagonal

                for (size_t col = 0; col < row; ++col) {
                        const double distance = distance_function(
                                        rec_data + row, rec_data + col);

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
static bool distance_matrix_check(double **dm, size_t size)
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
 * point). Epsilon-neighborhood is an open set (i.e. <, not <=).
 */
static void eps_get_neigh(const struct rec *point, double **distance_matrix,
                const struct vector *rec_vec, struct vector *eps_vec)
{
        const struct rec *rec_data = (const struct rec *)rec_vec->data;
        double *distance_vec;
        static bool thread_num_initialized = false;

        /* Clear the result vector. */
        vector_clear(eps_vec);

        if (distance_matrix[0] == NULL) {
                if (!thread_num_initialized) {
                        thread_num_init();
                        thread_num_initialized = true;
                }

                distance_vec = (double *)distance_matrix + 1;

                thread_num_start();
                #pragma omp parallel for firstprivate(distance_vec, rec_data)
                for (size_t i = 0; i < rec_vec->size; ++i) {
                        distance_vec[i] = distance_function(
                                        rec_data + point->id, rec_data + i);
                }
                thread_num_stop();
        } else {
                distance_vec = distance_matrix[point->id];
        }

        /* Find and store eps-neighbors of a certain point. */
        for (size_t i = 0; i < rec_vec->size; ++i) {
                if (distance_vec[i] < EPS) {
                        struct rec *neigbor = vector_get_ptr(rec_vec, i);

                        vector_add(eps_vec, &neigbor); //store only the pointer
                }
        }
}


/*
 * Comparing addresses is fine because struct vector uses continuous memory.
 * Destination vector consist 1 to N non-descending sequences.
 */
static bool eps_merge(struct vector *dest_vec, const struct vector *new_vec)
{
        /* Iterators. */
        const struct rec **dest_it = (const struct rec **)dest_vec->data;
        const struct rec **dest_it_end = dest_it + dest_vec->size;

        //printf("\n\nNEW EPS MERGE\n");
        /* Loop through destination vector. */
        while (dest_it < dest_it_end) {
                const struct rec **new_it = (const struct rec **)new_vec->data;
                const struct rec **new_it_end = new_it + new_vec->size;
                const struct rec *dest_last = *dest_it;

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
                struct rec *point = ((struct rec **)new_vec->data)[i];
                if (point != NULL) {
                        vector_add(dest_vec, &point);
                }
        }

        return true;
}


void dbscan(const struct vector *rec_vec, double **distance_matrix)
{
        size_t cluster_id = CLUSTER_FIRST;

        struct vector eps_vec; //vectors of pointers to the record storage
        struct vector new_eps_vec;

        vector_init(&eps_vec, sizeof (struct rec *));
        vector_init(&new_eps_vec, sizeof (struct rec *));


        /* Loop through all the points. */
        for (size_t i = 0; i < rec_vec->size; ++i) {
                struct rec *point = (struct rec *)rec_vec->data + i;

                if (point->cluster_id != CLUSTER_UNVISITED) {
                        /* The point is a noise or already in some cluster. */
                        continue;
                }

                eps_get_neigh(point, distance_matrix, rec_vec, &eps_vec);

                if (eps_vec.size < MIN_PTS) {
                        /* The point is a noise, this may be chaged later. */
                        point->cluster_id = CLUSTER_NOISE;
                        vector_clear(&eps_vec);
                        continue;
                }

                /* The point is a core point of a new cluster, expand it. */
                point->cluster_id = cluster_id;

                /* Loop through the whole (growing) eps-neighborhood. */
                for (size_t j = 0; j < eps_vec.size; ++j) {
                        struct rec *new_point = ((struct rec **)eps_vec.data)[j];

                        if (new_point->cluster_id == CLUSTER_UNVISITED) {
                                /*
                                 * The point is either a core point (then add
                                 * its eps-neighborhood to the current
                                 * eps-neighborhood) or a border point.
                                 */
                                new_point->cluster_id = cluster_id;
                                eps_get_neigh(new_point, distance_matrix,
                                                rec_vec, &new_eps_vec);

                                if (new_eps_vec.size >= MIN_PTS) { //core point
                                        vector_concat(&eps_vec, &new_eps_vec);
                                        //eps_merge(&eps_vec, &new_eps_vec);
                                } //else the point is a borer point

                        } else if (new_point->cluster_id == CLUSTER_NOISE) {
                                /* The point was a noise, but isn't anymore. */
                                new_point->cluster_id = cluster_id;
                        } //else the point was already part of some cluster
                }

                cluster_id++; //cluster finished, move to the next one
        }

        vector_free(&new_eps_vec);
        vector_free(&eps_vec);
}


static error_code_t clusterize(struct slave_task_ctx *stc)
{
        error_code_t primary_errno = E_OK;
        int secondary_errno;

        size_t file_rec_cntr = 0;
        size_t file_proc_rec_cntr = 0;
        size_t flows_cnt_meta;

        struct rec rec; //data point
        struct vector rec_vec; //set of data points

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


        vector_init(&rec_vec, sizeof (struct rec));
        assert(lnf_info(stc->file, LNF_INFO_FLOWS, &flows_cnt_meta,
                                sizeof (flows_cnt_meta)) == LNF_OK);
        vector_reserve(&rec_vec, flows_cnt_meta);


        /**********************************************************************/
        /* Read all records from the file. */
        while ((secondary_errno = lnf_read(stc->file, stc->rec)) == LNF_OK) {
                file_rec_cntr++;

                /* Apply filter (if there is any). */
                if (stc->filter && !lnf_filter_match(stc->filter, stc->rec)) {
                        continue;
                }
                file_proc_rec_cntr++;

                rec.id = file_rec_cntr - 1;
                rec.cluster_id = CLUSTER_UNVISITED;
                //rec.spec_core = false;

                lnf_rec_fget(stc->rec, LNF_FLD_SRCADDR, &rec.srcaddr);
                lnf_rec_fget(stc->rec, LNF_FLD_DSTADDR, &rec.dstaddr);
                lnf_rec_fget(stc->rec, LNF_FLD_SRCPORT, &rec.srcport);
                lnf_rec_fget(stc->rec, LNF_FLD_DSTPORT, &rec.dstport);
                lnf_rec_fget(stc->rec, LNF_FLD_PROT, &rec.proto);

                vector_add(&rec_vec, &rec);
        }
        /* Check if we reach end of the file. */
        if (secondary_errno != LNF_EOF) {
                PRINT_WARNING(E_LNF, secondary_errno, "EOF wasn't reached");
        }

        /**********************************************************************/
        /* Create and fill distance matrix. */
        distance_matrix = distance_matrix_init(rec_vec.size);
        distance_matrix_fill(distance_matrix, rec_vec.size,
                        (struct rec *)rec_vec.data);
        //distance_matrix_check(distance_matrix, rec_vec.size);

        /**********************************************************************/
        /* DBSCAN */
        dbscan(&rec_vec, distance_matrix);

        /**********************************************************************/
        /* Print results. */
        putchar('\n');
        for (size_t i = 0; i < rec_vec.size; ++i) {
                struct rec *point = (struct rec *)rec_vec.data + i;

                printf("%lu: %lu\n", point->id, point->cluster_id);
        }
        putchar('\n');



        /**********************************************************************/
        /* Free the memory. */
        distance_matrix_free(distance_matrix);
        vector_free(&rec_vec);

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


static void progress_report_init(size_t files_cnt)
{
        MPI_Gather(&files_cnt, 1, MPI_UNSIGNED_LONG, NULL, 0, MPI_UNSIGNED_LONG,
                        ROOT_PROC, MPI_COMM_WORLD);
}

static void progress_report_next(void)
{
        MPI_Request request = MPI_REQUEST_NULL;


        MPI_Isend(NULL, 0, MPI_BYTE, ROOT_PROC, TAG_PROGRESS,
                        MPI_COMM_WORLD, &request);
        MPI_Request_free(&request);
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

                /* Report that another file has been processed. */
                progress_report_next();
        }

        /* Free LNF record. */
        lnf_rec_free(stc->rec);
err:

        return primary_errno;
}

error_code_t clustering(int world_size)
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

        /* Report number of files to be processed. */
        progress_report_init(paths_cnt);


        primary_errno = process(&stc, paths, paths_cnt);


        /* Reduce statistics to the master. */
        MPI_Reduce(&stc.processed_summ, NULL, 3, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);
        MPI_Reduce(&stc.metadata_summ, NULL, 15, MPI_UINT64_T, MPI_SUM,
                        ROOT_PROC, MPI_COMM_WORLD);


finalize_task:
        path_array_free(paths, paths_cnt);
        task_free(&stc);

        return primary_errno;
}
