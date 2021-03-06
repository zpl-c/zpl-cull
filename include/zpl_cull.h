/*

ZPL - Tree culler module

Usage:
    #define ZPLC_IMPLEMENTATION exactly in ONE source file right BEFORE including the library, like:

    #define ZPLC_IMPLEMENTATION
    #include"zpl_cull.h"

Dependencies:
    zpl.h

Credits:
    Dominik Madarasz (GitHub: zaklaus)

Version History:
    3.1.3 - Fixed tests and warnings
    3.1.2 - Fixed zplc_insert bug with invalid branched nodes
    3.1.1 - Fixed zplc_insert signature
    3.1.0 - Added min_bounds checks + fixes
    3.0.1 - A lot of bug fixes.
    3.0.0 - Added the ability to remove specific node, Few utilities were added.
    2.1.2 - Small fixes for tiny cpp warnings
    2.0.0 - Slight fixes and design changes
    1.0.2 - Small oversight fixed
    1.0.1 - Fixes.
    1.0.0 - Initial version
    
    
    Copyright 2017 Dominik Madarász
    Licensed under the Apache License, Version 2.0 (the "License");
    you may not use this file except in compliance with the License.
    You may obtain a copy of the License at

      http://www.apache.org/licenses/LICENSE-2.0

    Unless required by applicable law or agreed to in writing, software
    distributed under the License is distributed on an "AS IS" BASIS,
    WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
    See the License for the specific language governing permissions and
    limitations under the License. 
*/

///////////////////////////////////////////////////////////
//
// NOTE(ZaKlaus): If you use memory arena, it gets reset
// when clear() is called, that means,
// if your tree shares memory with other systems
// in the arena, the data might get corrupted!
//
// Due to the nature of how linear allocators work
// you can't free single resource of the arena and
// the only way to clear it is to completely,
// reset it. Please, consider using either
// a different allocator supporting _free_ operation
// or make sure the (sub)arena's memory is
// exclusive to the tree only.
//

#ifndef ZPL_INCLUDE_ZPL_CULL_H
#define ZPL_INCLUDE_ZPL_CULL_H

#if defined(__cplusplus)
extern "C" {
#endif

    //
    // Spatial bounding box
    //

    typedef zplm_aabb3_t zplc_bounds_t;

    //
    // Tree culler structure
    //

    typedef enum zplc_dim_e {
        zplc_dim_2d_ev = 2,
        zplc_dim_3d_ev = 3,
    } zplc_dim_e;

    typedef struct zplc_node_t {
        zplm_vec3_t position;

        // NOTE(ZaKlaus): Custom data to be set by user.
        void *ptr;
        u64   tag;
        b32   unused;
    } zplc_node_t;

    typedef struct zplc_t {
        zpl_allocator_t            allocator;

        u32                        max_nodes;
        isize                      dimensions;
        zplc_bounds_t              boundary;
        zplm_vec3_t                min_bounds;
        b32                        use_min_bounds;
        zpl_array_t(zplc_node_t)   nodes;
        zpl_array_t(usize)         free_nodes;
        zpl_array_t(struct zplc_t) trees;
    } zplc_t;

    ZPL_DEF void         zplc_init       (zplc_t *c, zpl_allocator_t a, isize dims, zplc_bounds_t bounds, zplm_vec3_t min_bounds, u32 max_nodes);
    ZPL_DEF void         zplc_query      (zplc_t *c, zplc_bounds_t bounds, zpl_array_t(zplc_node_t) *out_nodes);
    ZPL_DEF zplc_t      *zplc_insert     (zplc_t *c, zplc_node_t node);
    ZPL_DEF b32          zplc_remove     (zplc_t *c, u64 tag);
    ZPL_DEF zplc_t      *zplc_find_branch(zplc_t *c, u64 tag);
    ZPL_DEF void         zplc_split      (zplc_t *c);
    ZPL_DEF void         zplc_clear      (zplc_t *c);

    #define zplc_free zplc_clear


#if defined(__cplusplus)
}
#endif

#if defined(ZPLC_IMPLEMENTATION) && !defined(ZPLC_IMPLEMENTATION_DONE)
#define ZPLC_IMPLEMENTATION_DONE

#if defined(__cplusplus)
extern "C" {
#endif

    void zplc_init(zplc_t *c, zpl_allocator_t a, isize dims, zplc_bounds_t bounds, zplm_vec3_t min_bounds, u32 max_nodes) {
        zplc_t c_ = {0};
        *c            = c_;
        c->allocator  = a;
        c->boundary   = bounds;
        c->min_bounds = min_bounds;
        c->use_min_bounds = zplm_vec3_mag(min_bounds) > 0.0f;
        c->max_nodes  = max_nodes;
        c->dimensions = dims;

    }

    b32 zplc__contains(isize dims, zplc_bounds_t a, f32 *point) {
        for (i32 i = 0; i < dims; ++i) {
            if (!( a.centre.e[i] - a.half_size.e[i] <= point[i]
                   && a.centre.e[i] + a.half_size.e[i] >= point[i])) {
                return false;
            }
        }

        return true;
    }

    b32 zplc__intersects(isize dims, zplc_bounds_t a, zplc_bounds_t b) {
        for (i32 i = 0; i < dims; ++i) {
            if (zpl_abs(a.centre.e[i] - b.centre.e[i]) > (a.half_size.e[i] + b.half_size.e[i])) return false;
        }

        return true;
    }

    void zplc_query(zplc_t *c, zplc_bounds_t bounds, zplc_node_t **out_nodes) {
        if (c->nodes == NULL) return;
        if (!zplc__intersects(c->dimensions, c->boundary, bounds)) return;

        isize nodes_count = zpl_array_count(c->nodes);
        for (i32 i = 0; i < nodes_count; ++i) {
            if (c->nodes[i].unused) continue;

            b32 inside = zplc__contains(c->dimensions, bounds, c->nodes[i].position.e);

            if (inside) {
                zpl_array_append(*out_nodes, c->nodes[i]);
            }

        }
        if (c->trees == NULL) return;

        isize trees_count = zpl_array_count(c->trees);
        if (trees_count == 0) return;

        for (i32 i = 0; i < trees_count; ++i) {
            zplc_query((c->trees+i), bounds, out_nodes);
        }
    }

    b32 zplc_remove(zplc_t *c, u64 tag) {
        if (c->nodes == NULL) return false;
        for (i32 i = 0; i < zpl_array_count(c->nodes); ++i) {
            zplc_node_t *node = &c->nodes[i];
            if (node->tag == tag) {
                if (node->unused) return false;
                if (c->free_nodes == NULL) {
                    zpl_array_init_reserve(c->free_nodes, c->allocator, c->max_nodes);
                }
                zpl_array_append(c->free_nodes, i);
                node->unused = true;
                return true;
            }
        }

        return false;
    }

    zplc_t *zplc_find_branch(zplc_t *c, u64 tag) {
        for (i32 i = 0; i < zpl_array_count(c->nodes); ++i) {
            if (c->nodes[i].tag == tag) {
                return c;
            }
        }

        if (c->trees == NULL) return NULL;
        isize trees_count = zpl_array_count(c->trees);
        if (trees_count == 0) return NULL;

        for (i32 i = 0; i < trees_count; ++i) {
            zplc_t *branch = zplc_find_branch(&c->trees[i], tag);
            if (branch) return branch;
        }

        return NULL;
    }

    b32 zplc__bounds_small_enough(zplc_bounds_t a, zplm_vec3_t b) {
        //TODO(zaklaus): Is this the best way we can determine bounds for k-d ?
        return a.half_size.x <= b.x && a.half_size.y <= b.y && a.half_size.z <= b.z;
    }

    zplc_t *zplc_insert(zplc_t *c, zplc_node_t node) {
        if (!zplc__contains(c->dimensions, c->boundary, node.position.e)) return NULL;

        if (c->nodes == NULL) {
            zpl_array_init(c->nodes, c->allocator);
        }

        if (c->free_nodes && zpl_array_count(c->free_nodes) > 0) {
            node.unused = false;
            c->nodes[c->free_nodes[zpl_array_count(c->free_nodes)-1]] = node;
            zpl_array_pop(c->free_nodes);
            return c;
        }

        if ((usize)zpl_array_count(c->nodes) < c->max_nodes) {
            insert:
            zpl_array_append(c->nodes, node);
            return c;
        }

        if (c->use_min_bounds && zplc__bounds_small_enough(c->boundary, c->min_bounds)) {
            goto insert;
        }

        if (c->trees == NULL) {
            zpl_array_init(c->trees, c->allocator);
        }

        isize trees_count = zpl_array_count(c->trees);
        if (trees_count == 0) {
            zplc_split(c);
        }

        trees_count = zpl_array_count(c->trees);
        for (i32 i = 0; i < trees_count; ++i) {
            zplc_t *tree = zplc_insert((c->trees+i), node);
            if (tree) return tree;
        }

        return NULL;
    }

    zpl_global f32 zplc__tpl[][3] = {
        {-1.0f, -1.0f, -1.0f},
        {+1.0f, -1.0f, -1.0f},
        {-1.0f, +1.0f, -1.0f},
        {+1.0f, +1.0f, -1.0f},
        {-1.0f, -1.0f, +1.0f},
        {+1.0f, -1.0f, +1.0f},
        {-1.0f, +1.0f, +1.0f},
        {+1.0f, +1.0f, +1.0f},
    };

    void zplc_split(zplc_t *c) {
        zplc_bounds_t hd = c->boundary;
        for (i32 i = 0; i < c->dimensions; ++i) {
            hd.half_size.e[i] /= 2.0f;
        }

        i32 loops = 0;
        /**/ if (c->dimensions == zplc_dim_2d_ev) {
            loops = 4;
        }
        else if (c->dimensions == zplc_dim_3d_ev) {
            loops = 8;
        }

        f32 p[3] = {0};
        for (i32 i = 0; i < loops; ++i) {
            zplc_t tree = {0};
            zplc_bounds_t bounds = {0};
            p[0] = c->boundary.centre.e[0] + hd.half_size.e[0]*zplc__tpl[i][0];
            p[1] = c->boundary.centre.e[1] + hd.half_size.e[1]*zplc__tpl[i][1];
            p[2] = c->boundary.centre.e[2] + hd.half_size.e[2]*zplc__tpl[i][2];
            zpl_memcopy(bounds.centre.e, p, 3 * sizeof(f32));
            bounds.half_size = hd.half_size;

            tree.boundary   = bounds;
            tree.min_bounds = c->min_bounds;
            tree.use_min_bounds = c->use_min_bounds;
            tree.max_nodes  = c->max_nodes;
            tree.dimensions = c->dimensions;
            tree.allocator  = c->allocator;

            zpl_array_append(c->trees, tree);
        }
    }

    void zplc_clear(zplc_t *c) {
        // TODO(ZaKlaus): Support more allocators?
        if (c->allocator.proc == zpl_arena_allocator_proc) {
            zpl_free_all(c->allocator);
            c->nodes = NULL;
            c->trees = NULL;
            return;
        }

        if (c->nodes) {
            zpl_array_free(c->nodes);
            c->nodes = NULL;
        }

        if (!c->trees) return;
        isize trees_count = zpl_array_count(c->trees);
        for (i32 i = 0; i < trees_count; ++i) {
            zplc_clear((c->trees+i));
        }

        zpl_array_free(c->trees);
        if (c->free_nodes) zpl_array_free(c->free_nodes);
        c->trees = NULL;
        c->free_nodes = NULL;
    }

#if defined(__cplusplus)
}
#endif

#endif // ZPLC_IMPLEMENTATION

#endif // ZPL_INCLUDE_ZPL_CULL_H
