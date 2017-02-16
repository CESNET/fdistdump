/** Implementation of the Vantage-point tree, a metric space indexing data
 * structure.
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

#include "vp_tree.h"
#include "print.h"

#include <stdlib.h>
#include <assert.h>
#include <omp.h>


struct vp_node {
        const struct point *vantage_point;

        /* Descendants. */
        struct vp_node *left;
        struct vp_node *right;

        double left_lower_bound;
        double left_upper_bound;
        double right_lower_bound;
        double right_upper_bound;

        /* Following are only for debugging. */
        double median_dist;
        size_t left_size;
        size_t right_size;
};


/**
 * \defgroup selection_group kth smallest element selection
 * @{
 */
/** Quickselect's partition procedure.
 *
 * In linear time, partition a list into three parts: less than, greater than
 * and equals to the pivot, for example input 3 2 7 4 5 1 4 1 will be
 * partitioned into 3 2 1 1 | 5 7 | 4 4 4 where 4 is the pivot.
 * Modified version of the median-of-three strategy is implemented, it ends with
 * a median at the end of an array (this saves us one or two swaps).
 */
static void qselect_partition(struct point **points_ptr, size_t points_size,
                size_t *less_size, size_t *equal_size)
{
        /* Modified median-of-three and pivot selection. */
        struct point **first_ptr = points_ptr;
        struct point **middle_ptr = points_ptr + (points_size / 2);
        struct point **last_ptr = points_ptr + (points_size - 1);
        if ((*first_ptr)->m.quality > (*last_ptr)->m.quality) {
                SWAP(*first_ptr, *last_ptr, struct point *);
        }
        if ((*first_ptr)->m.quality > (*middle_ptr)->m.quality) {
                SWAP(*first_ptr, *middle_ptr, struct point *);
        }
        if ((*last_ptr)->m.quality > (*middle_ptr)->m.quality) { //reversed
                SWAP(*last_ptr, *middle_ptr, struct point *);
        }
        const double pivot_value = (*last_ptr)->m.quality;

        /* Element swapping. */
        size_t greater_idx = 0;
        size_t equal_idx = points_size - 1;
        size_t i = 0;
        while (i < equal_idx) {
                const double elem_value = points_ptr[i]->m.quality;

                if (elem_value < pivot_value) {
                        SWAP(points_ptr[greater_idx], points_ptr[i],
                                        struct point *);
                        greater_idx++;
                        i++;
                } else if (elem_value == pivot_value) {
                        equal_idx--;
                        SWAP(points_ptr[i], points_ptr[equal_idx],
                                        struct point *);
                } else { //elem_value > pivot_value
                        i++;
                }
        }

        *less_size = greater_idx;
        *equal_size = points_size - equal_idx;
}

/** A selection algorithm to find the kth smallest element in an unordered list.
 */
static struct point * qselect(struct point **points_ptr, size_t points_size,
                size_t k)
{
        size_t less_size;
        size_t equal_size;

        qselect_partition(points_ptr, points_size, &less_size, &equal_size);

        if (k < less_size) { //k lies in the less-than-pivot partition
                points_size = less_size;
        } else if (k < less_size + equal_size) { //k lies in the equals-to-pivot 
                return points_ptr[points_size - 1];
        } else { //k lies in the greater-than-pivot partition
                points_ptr += less_size;
                points_size -= less_size + equal_size;
                k -= less_size + equal_size;
        }

        return qselect(points_ptr, points_size, k);
}
/**
 * @}
 */ //selection_group

/*
 * The comparison function must return an integer less than, equal to, or
 * greater than zero if the first argument is considered to be respectively less
 * than, equal to, or greater than the second.
 */
static int compar_indir_quality(const void *p1, const void *p2)
{
        const double p1_quality = (*(const struct point *const *)p1)->m.quality;
        const double p2_quality = (*(const struct point *const *)p2)->m.quality;

        if (p1_quality < p2_quality) {
                return -1;
        } else if (p1_quality > p2_quality) {
                return 1;
        } else {
                return 0;
        }
}


static void points_ptr_print(struct point **points_ptr, size_t points_size)
{
        for (struct point **point = points_ptr;
                        point < points_ptr + points_size;
                        ++point)
        {
                point_print(*point);
        }
}

static void points_ptr_shuffle(struct point **points_ptr, size_t points_size)
{
        for (size_t i = points_size - 1; i > 0; --i) {
                size_t j = rand() % (i + 1);
                SWAP(points_ptr[j], points_ptr[i], struct point *);
        }
}


static const struct point * select_vantage_point(struct point **points_ptr,
                size_t points_size)
{
        struct point **const vp_dest = points_ptr;

        //struct point **vp = points_ptr;
        //struct point **vp = points_ptr + rand() % points_size;
        //SWAP(*vp_dest, *vp, struct point *);
        //return *vp_dest;

        const size_t samples_cnt = 5; //TODO: elaborate
        double max_variance = 0.0;
        const struct point *best_sample = NULL;
        for (size_t i = 0; i < samples_cnt; ++i) {
                const struct point *sample = points_ptr[rand() % points_size];

                /* Calculate the distances and determine their median value. */
                distance_ones_perspective(sample, points_ptr, points_size);
                const double median_dist = qselect(points_ptr, points_size,
                                points_size / 2)->m.quality;

                /* Calculate a variance. */
                double variance = 0.0;
                for (struct point **point = points_ptr;
                                point < points_ptr + points_size;
                                ++point)
                {
                        const double diff = (*point)->m.quality - median_dist;
                        variance += diff * diff;
                }
                variance /= points_size;

                /* Remember the sample with a highest variance. */
                if (variance >= max_variance) { //= in case all the pts are same
                        max_variance = variance;
                        best_sample = sample;
                }
        }
        assert(best_sample != NULL);

        /* Find the best sample and move it to the beginning of the array. */
        for (struct point **point = points_ptr;
                        point < points_ptr + points_size;
                        ++point)
        {
                if (*point == best_sample) {
                        SWAP(*vp_dest, *point, struct point *);
                        return *vp_dest;
                }
        }
        assert(!"best sample not found among points");
}

/** \brief Make a vp-tree.
 *
 * 1. Check conditions ensuring proper tree termination and create a new node.
 * 2. Select a vantage point, swap it with a first point in the points_ptr
 *    array to remove the vantage point from further processing. Calculate
 *    distances from the vantage point to all the other points.
 * 3. Sort the pointers according to the vantage point's distance prespective
 *    (stored in variable "quality") and select a their distance median.
 * 4. Find a border between left and right balls such that all distances in the
 *    left ball are less than the median and all distances in the right ball are
 *    greater than or equal the median. Also store
 *    {left,right} {lower,upper} bounds, which is a minimum/maximum distance in
 *    each ball.
 * 5. Recursively construct rest of the tree for both balls.
 */
static struct vp_node *make_tree(struct point **points_ptr, size_t points_size)
{
        assert(points_ptr != NULL);

        printf("-----------------------------------------------------------\n");
        printf("original: %zu points\n", points_size);
        points_ptr_print(points_ptr, points_size);
        putchar('\n');

        /* 1. */
        if (points_size == 0) { //no points at all
                return NULL;
        }
        struct vp_node *node = calloc_wr(1, sizeof (*node), true);
        if (points_size == 1) { //only vantage point
                node->vantage_point = points_ptr[0];
                node->left = NULL;
                node->right = NULL;
                return node;
        }

        /* 2. */
        node->vantage_point = select_vantage_point(points_ptr, points_size);
        points_ptr++; //skip the first point which is the vantage point
        points_size--;
        distance_ones_perspective(node->vantage_point, points_ptr, points_size);

        printf("vantage point: ");
        point_print(node->vantage_point);
        putchar('\n');

        /* 3. */
        //TODO: quickselect + partition may be faster
        qsort(points_ptr, points_size, sizeof (*points_ptr),
                        compar_indir_quality);
        const size_t median_idx = points_size / 2;
        const double median_dist = points_ptr[median_idx]->m.quality;

        /* 4. */
        /* Unballanced tree: **************************************************/
        node->left_lower_bound = points_ptr[0]->m.quality;
        node->right_upper_bound = points_ptr[points_size - 1]->m.quality;
        size_t left_size = 0;
        for (size_t i = median_idx - 1; i != SIZE_MAX; --i) {
                if (points_ptr[i]->m.quality < median_dist) {
                        left_size = i + 1;
                        node->left_upper_bound = points_ptr[i]->m.quality;
                        node->right_lower_bound = points_ptr[i + 1]->m.quality;
                        break;
                }
        }
        /* Ballanced tree: ****************************************************/
        //size_t left_size = median_idx;
        //node->left_lower_bound = points_ptr[0]->m.quality;
        //node->left_upper_bound = points_ptr[left_size - 1]->m.quality;
        //node->right_lower_bound = points_ptr[left_size]->m.quality;
        //node->right_upper_bound = points_ptr[points_size - 1]->m.quality;


        node->median_dist = median_dist;
        node->left_size = left_size;
        node->right_size = points_size - left_size;

        printf("left ball: %zu points\n", left_size);
        points_ptr_print(points_ptr, left_size);
        putchar('\n');

        printf("right ball: %zu points\n", points_size - left_size);
        points_ptr_print(points_ptr + left_size, points_size - left_size);
        printf("---------------------------------------------------------\n\n");

        /* 5. */
        node->left = make_tree(points_ptr, left_size);
        node->right = make_tree(points_ptr + left_size, points_size - left_size);
        return node;
}


struct vp_node * vp_tree_init(struct point *points_data, size_t points_size)
{
        assert(points_data != NULL && points_size != 0);

        /* Create an array of pointers to the points storage. */
        struct point **points_ptr = malloc_wr(points_size, sizeof (*points_ptr),
                        true);
        for (size_t i = 0; i < points_size; ++i) {
                points_ptr[i] = points_data + i;
        }

        //srand(time(NULL));
        struct vp_node *root = make_tree(points_ptr, points_size);
        assert(root != NULL);

        free(points_ptr);
        return root;
}

/* Port-order traversal. */
void vp_tree_free(struct vp_node *root)
{
        if (root == NULL) {
                return;
        }

        vp_tree_free(root->left);
        vp_tree_free(root->right);
        free(root);
}

void vp_tree_range_query(const struct vp_node *root, const struct point *p,
                double range)
{
        if (root == NULL) {
                return;
        }

        const double dist = distance_function(p, root->vantage_point);
        //printf("dist = %f, range = %f\n", dist, range);
        if (dist < range) { //VP is in the range
                printf("%zu ", root->vantage_point->id);
        }

        //printf("left_upper_bound = %f\n", root->left_upper_bound);
        if (dist < root->left_upper_bound + range) {
                vp_tree_range_query(root->left, p, range);
        }
        //printf("right_lower_bound = %f\n", root->right_lower_bound);
        if (dist > root->right_lower_bound - range) {
                vp_tree_range_query(root->right, p, range);
        }
}

/* Pre-order traversal. */
void vp_tree_print(const struct vp_node *root)
{
        if (root == NULL) {
                return;
        }

        printf("point id = %zu, llb = %f, lub = %f, rlb = %f, rub = %f, "
                        "median = %f, left size = %zu, right size = %zu\n",
                        root->vantage_point->id, root->left_lower_bound,
                        root->left_upper_bound, root->right_lower_bound,
                        root->right_upper_bound, root->median_dist,
                        root->left_size, root->right_size);

        vp_tree_print(root->left);
        vp_tree_print(root->right);
}

size_t vp_tree_height(const struct vp_node *root)
{
        if (root == NULL) {
                return 0;
        }

        size_t left_height = vp_tree_height(root->left);
        size_t right_height = vp_tree_height(root->right);

        return (MAX(left_height, right_height) + 1);
}
