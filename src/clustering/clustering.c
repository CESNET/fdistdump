/** Master and slave IP flow cluster analysis functionality.
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
#include "metric_space.h"
#include "slave.h"
#include "path_array.h"
#include "vector.h"
#include "output.h"
#include "print.h"

#include <string.h>
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


/* Global variables. */
//static double global_eps = 0.6;
//static size_t global_min_pts = 10;
#define global_eps (2.0 / 6.0) + 10 * DBL_EPSILON
#define global_min_pts 10

extern MPI_Datatype mpi_struct_shared_task_ctx;


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
};


/******************************************************************************/
/**
 * \defgroup shared_fun_group Master-slave shared functionality
 * @{
 */

/* Mark the points in eps_vec as covered by an id. */
static void mark_covered(const size_t id, struct vector *eps_vec)
{
        struct point **eps_begin = (struct point **)eps_vec->data;
        struct point **eps_end = eps_begin + eps_vec->size;

        for (struct point **point = eps_begin; point < eps_end; ++point) {
                (*point)->clustering.covered_by = id;
        }
}

static int dbscan(struct point *point_storage, size_t point_storage_size,
                struct distance *distance)
{
        int cluster_id = CLUSTER_FIRST;
        size_t cov_pts;

        struct vector seeds; //vector of pointers to the record storage
        struct vector neighbors; //vector of pointers to the record storage

        vector_init(&seeds, sizeof (point_storage));
        vector_init(&neighbors, sizeof (point_storage));
        vector_reserve(&seeds, point_storage_size);


        /* Loop through all base points (static vector). */
        struct point *point_end = point_storage + point_storage_size;
        for (struct point *point = point_storage; point < point_end; ++point) {
                if (point->clustering.cluster_id != CLUSTER_UNVISITED) {
                        /* The point is a noise or already in some cluster. */
                        PRINT_DEBUG("base point %zu: already part of cluster %d",
                                        point->id, point->clustering.cluster_id);
                        continue;
                }

                cov_pts = point_eps_neigh(point, point_storage,
                                point_storage_size, &seeds, distance,
                                global_eps);

                if (cov_pts < global_min_pts) {
                        /* The point is a noise, this may be chaged later. */
                        PRINT_DEBUG("base point %zu: noise covering %zu points",
                                        point->id, cov_pts);
                        point->clustering.cluster_id = CLUSTER_NOISE;
                        vector_clear(&seeds);
                        continue;
                }

                /* The point is a core point of a new cluster, expand it. */
                mark_covered(point->id, &seeds);
                point->clustering.cluster_id = cluster_id;
                point->clustering.core = true;
                PRINT_DEBUG("base point %zu: new cluster %d, core point covering %zu points",
                                point->id, point->clustering.cluster_id, cov_pts);

                /* Loop through the seed points (*growing* vector). */
                for (size_t j = 0; j < seeds.size; ++j) {
                        struct point *seed = ((struct point **)seeds.data)[j];

                        if (seed->clustering.cluster_id == CLUSTER_UNVISITED) {
                                seed->clustering.cluster_id = cluster_id;
                                cov_pts = point_eps_neigh(seed, point_storage,
                                                point_storage_size, &neighbors,
                                                distance, global_eps);

                                if (cov_pts >= global_min_pts) { //core point
                                        PRINT_DEBUG("seed %zu: core point covering %zu points (%zu new)",
                                                        seed->id, cov_pts, neighbors.size);
                                        seed->clustering.core = true;
                                        mark_covered(seed->id, &neighbors);
                                        vector_concat(&seeds, &neighbors);
                                } else { //border point
                                        PRINT_DEBUG("seed %zu: border point covering %zu points (%zu new)",
                                                        seed->id, cov_pts, neighbors.size);
                                }
                        } else if (seed->clustering.cluster_id == CLUSTER_NOISE) {
                                /* The point was a noise, but isn't anymore. */
                                seed->clustering.cluster_id = cluster_id;
                                PRINT_DEBUG("seed %zu: border point, former noise",
                                                seed->id);
                        } else { /* The point was already part of some cluster. */
                                PRINT_DEBUG("seed %zu: already part of cluster %d",
                                                seed->id, seed->clustering.cluster_id);
                        }
                }

                cluster_id++; //cluster finished, move to the next one
        }

        vector_free(&neighbors);
        vector_free(&seeds);

        return cluster_id;
}

static void dbscan_print(const struct point *point_storage, size_t size)
{
        for (const struct point *point = point_storage;
                        point < point_storage + size;
                        ++point) {
                printf("%d,%s\n", point->clustering.cluster_id,
                                sprint_rec((const uint8_t *)&point->f));
        }
}

static void model_print(const struct point *point_storage,
                size_t point_storage_size, size_t cluster_cnt)
{
        /* Loop through all the clusters. */
        for (size_t i = CLUSTER_NOISE; i < cluster_cnt; ++i) {
                size_t cluster_size = 0;
                size_t core_cnt = 0;

                /* Loop through all the points. */
                for (const struct point *point = point_storage;
                                point < point_storage + point_storage_size;
                                ++point)
                {
                        assert(point->clustering.cluster_id >= CLUSTER_NOISE);
                        if ((size_t)point->clustering.cluster_id == i) {
                                point_print(point);
                                cluster_size++;
                                core_cnt += point->clustering.core;
                        }
                }
                printf("model: cluster %zu: size = %zu, core_cnt = %zu\n", i,
                                cluster_size, core_cnt);
        }
}
/**
 * @}
 */ //shared_fun_group


/******************************************************************************/
/**
 * \defgroup slave_fun_group Slave-only functionality
 * @{
 */
/*
 * The comparison function must return an integer less than, equal to, or
 * greater than zero if the first argument is considered to be respectively less
 * than, equal to, or greater than the second.
 */
static int compar_dir_cluster_id(const void *p1, const void *p2)
{
        const int p1_cluster_id = ((const struct point *)p1)->clustering.cluster_id;
        const int p2_cluster_id = ((const struct point *)p2)->clustering.cluster_id;

        if (p1_cluster_id < p2_cluster_id) {
                return -1;
        } else if (p1_cluster_id > p2_cluster_id) {
                return 1;
        } else {
                return 0;
        }
}


static void local_model_cluster(struct point *cluster[], size_t cluster_size,
                struct vector *repr_vec, struct distance *distance,
                const bool noise)
{
        size_t repr_cnt = 0; //number of representative points
        size_t covered_cnt = 0; //number of points covered by the reprs.

        /* Calculate the inner-cluster representative quality. */
        for (size_t i = 0; i < cluster_size; ++i) {
                struct point *repr = cluster[i];
                const double *repr_distance = distance_get_vector(distance, repr);

                repr->clustering.quality = 0.0;
                for (size_t j = 0; j < cluster_size; ++j) {
                        const struct point *point = cluster[j];
                        const double dist = repr_distance[point->id];

                        if (dist < global_eps) {
                                repr->clustering.quality += global_eps - dist;
                        }
                }
        }

        while (covered_cnt != cluster_size) {
                assert(repr_cnt <= covered_cnt);

                /*
                 * Find a new representative with the highest quality which is
                 * directly density reachable to one of the representatives.
                 */
                struct point **best_cand = NULL;
                /* Loop through the representative candidates. */
                for (size_t i = repr_cnt; i < cluster_size; ++i) {
                        const struct point *cand = cluster[i]; //repr. candidate

                        /*
                         * As a best candidate select only core points (except
                         * for noise) and only those candidates with quality
                         * better then the previous best candidate.
                         */
                        if ((noise || cand->clustering.core) &&
                                        (best_cand == NULL || cand->clustering.quality >
                                         (*best_cand)->clustering.quality))
                        {
                                const double *cand_distance;

                                if (noise || repr_cnt == 0) { //no reprs. yet
                                        best_cand = cluster + i;
                                        continue;
                                }

                                /* Loop through the representaives. */
                                cand_distance = distance_get_vector(distance,
                                                cand);
                                for (size_t j = 0; j < repr_cnt; ++j) {
                                        const double dist =
                                                cand_distance[cluster[j]->id];

                                        /* Is repr. directly density reachable? */
                                        if (dist < global_eps) {
                                                best_cand = cluster + i;
                                                break;
                                        }
                                }
                        }
                }
                assert(best_cand != NULL);

                /* Swap the new representative point with the top element. */
                struct point *repr = *best_cand; //representative point
                *best_cand = cluster[repr_cnt];
                cluster[repr_cnt] = repr;

                const double *repr_distance =
                        distance_get_vector(distance, repr);

                /* Mark the points covered by the new representative. */
                repr->cov_cnt = 0;
                /* Loop through the representative's neighbors. */
                for (size_t i = repr_cnt; i < cluster_size; ++i) {
                        struct point *repr_nbr = cluster[i];
                        double dist = repr_distance[repr_nbr->id];

                        if (dist >= global_eps || repr_nbr->clustering.covered_by !=
                                        SIZE_MAX) {
                                /* Either too far away or already covered. */
                                continue;
                        }

                        repr_nbr->clustering.covered_by = repr->id;
                        repr->cov_cnt++;
                        const double *repr_nbr_distance =
                                distance_get_vector(distance, repr_nbr);
                        /* Loop through the representative's neighbors'
                         * neighbors. */
                        for (size_t j = 0; j < cluster_size; ++j) {
                                struct point *repr_nbr_nbr = cluster[j];
                                dist = repr_nbr_distance[repr_nbr_nbr->id];

                                if (dist < global_eps) {
                                        repr_nbr_nbr->clustering.quality -=
                                                (global_eps - dist);
                                }
                        }
                }

                covered_cnt += repr->cov_cnt;
                vector_add_ts(repr_vec, &repr); //store pointer to the repr.
                repr_cnt++;
        }
}

/* Point storage has to be sorted according to the cluster ID! */
/* TODO: try performance without another level of indirection. */
static void local_model(struct vector *repr_vec, struct point *point_storage,
                size_t point_storage_size, struct distance *distance,
                size_t cluster_cnt)
{
        //TODO: make point_ptr_array part of the metric_space
        struct point **point_ptr_array; //array of pointers to the point storage
        size_t cluster_size[cluster_cnt];
        size_t cluster_offset[cluster_cnt];

        point_ptr_array = malloc_wr(point_storage_size, sizeof (point_storage),
                        true);

        /* Calculate sizes of individual clusters. */
        memset(cluster_size, 0, sizeof (cluster_size));
        for (size_t i = 0; i < point_storage_size; ++i) {
                point_storage[i].clustering.covered_by = SIZE_MAX; //mark as uncovered
                point_ptr_array[i] = point_storage + i;
                assert(point_storage[i].clustering.cluster_id >= CLUSTER_NOISE);
                cluster_size[point_storage[i].clustering.cluster_id]++;
        }

        /* Calculate cluster offsets in the point_ptr_array. */
        cluster_offset[CLUSTER_NOISE] = 0;
        for (size_t i = CLUSTER_FIRST; i < cluster_cnt; ++i) {
                cluster_offset[i] = cluster_offset[i - 1] + cluster_size[i - 1];
        }

        /* Select the representatives for each cluster. */
        vector_clear(repr_vec);
        #pragma omp parallel for
        for (size_t i = CLUSTER_NOISE; i < cluster_cnt; ++i) {
                struct point **cluster_ptr_array = point_ptr_array +
                        cluster_offset[i];

                /* Check cluster ID of the first and the last point. */
                assert((size_t)cluster_ptr_array[0]->clustering.cluster_id == i);
                assert((size_t)cluster_ptr_array[cluster_size[i] - 1]->clustering.cluster_id == i);

                double wtime = -omp_get_wtime();
                local_model_cluster(cluster_ptr_array, cluster_size[i], repr_vec,
                                distance, i == CLUSTER_NOISE);
                PRINT_DEBUG("local model computation for cluster %zu finished in %f",
                                i, wtime + omp_get_wtime());
        }

        free(point_ptr_array);
}

static void local_model_send(const struct vector *repr_vec)
{
        uint64_t repr_vec_size = repr_vec->size;
        MPI_Reduce(&repr_vec_size, NULL, 1, MPI_UINT64_T, MPI_SUM, ROOT_PROC,
                        MPI_COMM_WORLD);

        const struct point **repr_begin = (const struct point **)repr_vec->data;
        const struct point **repr_end = repr_begin + repr_vec->size;
        for (const struct point **repr = repr_begin; repr < repr_end; ++repr) {
                MPI_Send(*repr, sizeof (**repr), MPI_BYTE, ROOT_PROC, TAG_DATA,
                                MPI_COMM_WORLD);
        }
}

static size_t global_model_recv(const struct vector *repr_vec,
                int cluster_id_map[])
{
        size_t gm_size; //global model size
        struct point gm_point;
        size_t cluster_cnt = 0;

        MPI_Bcast(&gm_size, 1, MPI_UINT64_T, ROOT_PROC, MPI_COMM_WORLD);

        for (size_t i = 0; i < gm_size; ++i) {
                MPI_Bcast(&gm_point, sizeof (gm_point), MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);

                for (size_t j = 0; j < repr_vec->size; ++j) {
                        struct point *repr = ((struct point **)repr_vec->data)[j];

                        if (memcmp(&repr->f, &gm_point.f,
                                                sizeof (struct point_features)) == 0) {
                                int global_cluster_id = gm_point.clustering.cluster_id;

                                assert(global_cluster_id >= CLUSTER_NOISE);
                                MAX_ASSIGN(cluster_cnt, (size_t)global_cluster_id);
                                cluster_id_map[repr->id] = global_cluster_id;
                                break;
                        }

                }
        }

        return cluster_cnt + 1;
}

static void global_model_relabel(struct point *point_storage, size_t size,
                const int cluster_id_map[])
{
        for (struct point *point = point_storage;
                        point < point_storage + size;
                        ++point)
        {
                const int new_id = cluster_id_map[point->clustering.covered_by];

                if (new_id >= CLUSTER_FIRST) {
                        /* Mapping exists, relabel. */
                        point->clustering.cluster_id = new_id;
                }
        }
}


static error_code_t clusterize(const struct metric_space *ms, size_t slave_cnt)
{
        error_code_t error_code = E_OK;

        /**********************************************************************/
        /* Setup output (temporary). */
        struct output_params op;
        struct field_info fi[LNF_FLD_TERM_];

        op.print_records = OUTPUT_ITEM_YES;
        op.format = OUTPUT_FORMAT_PRETTY;
        op.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        op.ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;
        op.ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;

        memset(fi, 0, sizeof (fi));
        fi[LNF_FLD_SRCPORT].id = LNF_FLD_SRCPORT;
        fi[LNF_FLD_DSTPORT].id = LNF_FLD_DSTPORT;
        fi[LNF_FLD_TCP_FLAGS].id = LNF_FLD_TCP_FLAGS;
        fi[LNF_FLD_SRCADDR].id = LNF_FLD_SRCADDR;
        fi[LNF_FLD_DSTADDR].id = LNF_FLD_DSTADDR;
        fi[LNF_FLD_PROT].id = LNF_FLD_PROT;

        output_setup(op, fi);

        /**********************************************************************/
        /* Create and fill distance matrix. */
        struct distance *distance = distance_init(ms->size, ms->point_storage);
        distance_fill(distance);
        //distance_print(distance);
        //distance_validate(distance);
        //distance_pmf(distance);

        /**********************************************************************/
        /* DBSCAN */
        const size_t local_cluster_cnt = dbscan(ms->point_storage, ms->size,
                        distance);
        if (slave_cnt == 1) {
                //dbscan_print(ms->point_storage, ms->size);
                goto finish;
        }

        /*
         * Sort the point storage according to the cluster ID for better
         * cache-friendliness during determination of the local model.
         */
        qsort(ms->point_storage, ms->size, sizeof (*ms->point_storage),
                        compar_dir_cluster_id);

        /**********************************************************************/
        /* Create and send local model. */
        {
        struct vector repr_vec;
        vector_init(&repr_vec, sizeof (ms->point_storage));

        local_model(&repr_vec, ms->point_storage, ms->size, distance,
                        local_cluster_cnt);
        //model_print(ms->point_storage, ms->size, local_cluster_cnt);
        local_model_send(&repr_vec);


        /**********************************************************************/
        /* Global model. */
        int cluster_id_map[ms->size];
        memset(cluster_id_map, 0, sizeof(cluster_id_map)); //TODO

        global_model_recv(&repr_vec, cluster_id_map);
        global_model_relabel(ms->point_storage, ms->size, cluster_id_map);
        //model_print(ms->point_storage, ms->size, global_cluster_cnt);
        dbscan_print(ms->point_storage, ms->size);

        vector_free(&repr_vec);
        }


        /**********************************************************************/
finish:
        /* Free the memory. */
        distance_free(distance);

        return error_code;
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
        int ret;

        assert(filter != NULL && filter_str != NULL && strlen(filter_str) != 0);

        /* Initialize filter. */
        //ret = lnf_filter_init(filter, filter_str); //old filter
        ret = lnf_filter_init_v2(filter, filter_str); //new filter
        if (ret != LNF_OK) {
                PRINT_ERROR(E_LNF, ret, "cannot initialise filter \"%s\"",
                                filter_str);
                return E_LNF;
        }

        return E_OK;
}


static error_code_t task_receive_ctx(struct slave_task_ctx *stc)
{
        error_code_t error_code = E_OK;


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
                error_code = task_init_filter(&stc->filter, filter_str);
                //it is OK not to check error_code
        }


        return error_code;
}

error_code_t clustering_slave(int world_size)
{
        error_code_t error_code = E_OK;

        struct slave_task_ctx stc;
        memset(&stc, 0, sizeof (stc));
        stc.slave_cnt = world_size - 1; //all nodes without master

        /* Wait for reception of task context from master. */
        error_code = task_receive_ctx(&stc);
        if (error_code != E_OK) {
                goto free_task_ctx;
        }

        /* Data source specific initialization. */
        size_t paths_cnt = 0;
        const char **paths = path_array_gen(stc.path_str, stc.shared.time_begin,
                        stc.shared.time_end, &paths_cnt);
        if (paths == NULL) {
                error_code = E_PATH;
                goto free_task_ctx;
        }

        /**********************************************************************/
        struct metric_space *ms = metric_space_init();
        metric_space_load(ms, paths, paths_cnt, stc.filter);

        error_code = clusterize(ms, stc.slave_cnt);

        /**********************************************************************/
        /* Cleanup and finalizations. */
free_metric_space:
        metric_space_free(ms);
free_path_array:
        path_array_free(paths, paths_cnt);
free_task_ctx:
        task_free(&stc);
finalize:
        MPI_Barrier(MPI_COMM_WORLD); //only for time measurement

        return error_code;
}
/**
 * @}
 */ //slave_fun_group


/******************************************************************************/
/**
 * \defgroup master_fun_group Master-only functionality
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


static void global_model_send(const struct point *point_storage, size_t size)
{
        MPI_Bcast(&size, 1, MPI_UINT64_T, ROOT_PROC, MPI_COMM_WORLD);

        for (struct point *point = point_storage;
                        point < point_storage + size;
                        ++point)
        {
                MPI_Bcast(point, sizeof (*point), MPI_BYTE, ROOT_PROC,
                                MPI_COMM_WORLD);
        }
}

static void local_model_recv(struct metric_space *ms)
{
        uint64_t point_cnt = 0;

        MPI_Reduce(MPI_IN_PLACE, &point_cnt, 1, MPI_UINT64_T, MPI_SUM, ROOT_PROC,
                        MPI_COMM_WORLD);
        struct point *point_storage = malloc_wr(point_cnt,
                        sizeof (*point_storage), true);

        /* Data receiving loop. */
        for (size_t i = 0; i < point_cnt; ++i) {
                /* Receive a message from any slave. */
                MPI_Recv(&point_storage[i], sizeof (*point_storage), MPI_BYTE,
                                MPI_ANY_SOURCE, TAG_DATA, MPI_COMM_WORLD,
                                MPI_STATUS_IGNORE);

                const size_t cov_cnt = point_storage[i].cov_cnt;
                //point_metadata_clear(&point.m); //TODO
                point_storage[i].id = i;
                point_storage[i].cov_cnt = cov_cnt;
        }

        ms->point_storage = point_storage;
        ms->size = point_cnt;
}


error_code_t clustering_master(int world_size, const struct cmdline_args *args)
{
        error_code_t error_code = E_OK;
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

        if (mtc.slave_cnt == 1) {
                goto finalize;
        }

        /**********************************************************************/
        /* Setup output. */
        struct output_params op;
        struct field_info fi[LNF_FLD_TERM_];

        op.print_records = OUTPUT_ITEM_YES;
        op.format = OUTPUT_FORMAT_PRETTY;
        op.tcp_flags_conv = OUTPUT_TCP_FLAGS_CONV_STR;
        op.ip_addr_conv = OUTPUT_IP_ADDR_CONV_STR;
        op.ip_proto_conv = OUTPUT_IP_PROTO_CONV_STR;

        memset(fi, 0, sizeof (fi));
        fi[LNF_FLD_SRCPORT].id = LNF_FLD_SRCPORT;
        fi[LNF_FLD_DSTPORT].id = LNF_FLD_DSTPORT;
        fi[LNF_FLD_TCP_FLAGS].id = LNF_FLD_TCP_FLAGS;
        fi[LNF_FLD_SRCADDR].id = LNF_FLD_SRCADDR;
        fi[LNF_FLD_DSTADDR].id = LNF_FLD_DSTADDR;
        fi[LNF_FLD_PROT].id = LNF_FLD_PROT;

        output_setup(op, fi);


        /**********************************************************************/
        /* Receive local models from all slaves. */
        struct metric_space *ms = metric_space_init();
        local_model_recv(ms);


        /**********************************************************************/
        /* Create and fill distance matrix. */
        struct distance *distance = distance_init(ms->size, ms->point_storage);
        distance_fill(distance);
        //distance_validate(distance);
        //distance_print(distance);


        /**********************************************************************/
        /* DBSCAN */
        const size_t cluster_cnt = dbscan(ms->point_storage, ms->size,
                        distance);
        //dbscan_print(ms->point_storage, ms->size);
        //model_print(ms->point_storage, ms->size, cluster_cnt);
        global_model_send(ms->point_storage, ms->size);


        /**********************************************************************/
        /* Cleanup and finalizations. */
        distance_free(distance);
        metric_space_free(ms);

finalize:
        MPI_Barrier(MPI_COMM_WORLD); //only for time measurement
        duration += MPI_Wtime(); //end time measurement
        PRINT_INFO("duration: %f", duration);

        return error_code;
}
/**
 * @}
 */ //master_fun_group
