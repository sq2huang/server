/* Copyright (c) 2016 MariaDB corporation

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; version 2 of the License.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA */

#ifndef UNIQUE_INCLUDED
#define UNIQUE_INCLUDED

#include "filesort.h"

/*
   Unique -- class for unique (removing of duplicates).
   Puts all values to the TREE. If the tree becomes too big,
   it's dumped to the file. User can request sorted values, or
   just iterate through them. In the last case tree merging is performed in
   memory simultaneously with iteration, so it should be ~2-3x faster.
 */

class Unique :public Sql_alloc
{
  DYNAMIC_ARRAY file_ptrs;
  ulong max_elements;   /* Total number of elements that will be stored in-memory */
  size_t max_in_memory_size;
  IO_CACHE file;
  TREE tree;
 /* Number of elements filtered out due to min_dupl_count when storing results
    to table. See Unique::get */
  ulong filtered_out_elems;
  uint size;

  uint full_size;   /* Size of element + space needed to store the number of
                       duplicates found for the element. */
  uint min_dupl_count;   /* Minimum number of occurences of element required for
                            it to be written to record_pointers.
                            always 0 for unions, > 0 for intersections */
  bool with_counters;
  /*
    TRUE  :the value stored is of variable size
    FALSE :the value stored is of fixed size
  */
  bool packed;
  size_t memory_used;
  /*
    Packed record ptr for a record of the table, this will be stored in
    the unique object
  */
  uchar* packed_rec_ptr;
  SORT_FIELD *sortorder;
  Sort_keys *sort_keys;

  bool merge(TABLE *table, uchar *buff, size_t size, bool without_last_merge);
  bool flush();
  size_t space_left()
  {
    DBUG_ASSERT(max_in_memory_size >= memory_used);
    return max_in_memory_size - memory_used;
  }
  bool is_full(size_t record_size)
  {
    return record_size > space_left();
  }

public:
  ulong elements;
  SORT_INFO sort;
  Unique(qsort_cmp2 comp_func, void *comp_func_fixed_arg,
         uint size_arg, size_t max_in_memory_size_arg,
         uint min_dupl_count_arg= 0, bool packed_arg= false);
  ~Unique();
  ulong elements_in_tree() { return tree.elements_in_tree; }
  inline bool unique_add(void *ptr)
  {
    DBUG_ENTER("unique_add");
    DBUG_PRINT("info", ("tree %u - %lu", tree.elements_in_tree, max_elements));
    if (!(tree.flag & TREE_ONLY_DUPS) &&
        tree.elements_in_tree >= max_elements && flush())
      DBUG_RETURN(1);
    DBUG_RETURN(!tree_insert(&tree, ptr, 0, tree.custom_arg));
  }
  inline bool unique_add(void *ptr, uint32 packed_size)
  {
    DBUG_ENTER("unique_add");
    DBUG_PRINT("info", ("tree %u - %lu", tree.elements_in_tree, max_elements));
    TREE_ELEMENT *res;
    size_t rec_size= packed_size + sizeof(TREE_ELEMENT) + tree.size_of_element;
    if (!(tree.flag & TREE_ONLY_DUPS) && is_full(rec_size) && flush())
      DBUG_RETURN(1);
    res= tree_insert(&tree, ptr, packed_size, tree.custom_arg);
    if (res)
      memory_used+= rec_size;
    DBUG_RETURN(!res);
  }

  bool is_in_memory() { return (my_b_tell(&file) == 0); }
  void close_for_expansion() { tree.flag= TREE_ONLY_DUPS; }

  bool get(TABLE *table);
  bool is_packed() { return packed; }
  static void store_packed_length(uchar *p, uint sz)
  {
    int4store(p, sz - size_of_length_field);
  }
  static uint read_packed_length(uchar *p)
  {
    return size_of_length_field + uint4korr(p);
  }
  
  static const uint size_of_length_field= 4;
  /* Cost of searching for an element in the tree */
  inline static double get_search_cost(ulonglong tree_elems, uint compare_factor)
  {
    return log((double) tree_elems) / (compare_factor * M_LN2);
  }  

  static double get_use_cost(uint *buffer, size_t nkeys, uint key_size,
                             size_t max_in_memory_size, uint compare_factor,
                             bool intersect_fl, bool *in_memory);
  inline static int get_cost_calc_buff_size(size_t nkeys, uint key_size,
                                            size_t max_in_memory_size)
  {
    size_t max_elems_in_tree=
      max_in_memory_size / ALIGN_SIZE(sizeof(TREE_ELEMENT)+key_size);
    return (int) (sizeof(uint)*(1 + nkeys/max_elems_in_tree));
  }

  void reset();
  bool walk(TABLE *table, tree_walk_action action, void *walk_action_arg);

  uint get_size() const { return size; }
  size_t get_max_in_memory_size() const { return max_in_memory_size; }
  uchar *get_packed_rec_ptr() { return packed_rec_ptr; }
  bool setup(THD *thd, Item_sum *item, uint non_const_args, uint arg_count);
  Sort_keys *get_keys() { return sort_keys; }
  SORT_FIELD *get_sortorder() { return sortorder; }

  friend int unique_write_to_file(uchar* key, element_count count, Unique *unique);
  friend int unique_write_to_ptrs(uchar* key, element_count count, Unique *unique);

  friend int unique_write_to_file_with_count(uchar* key, element_count count,
                                             Unique *unique);
  friend int unique_intersect_write_to_ptrs(uchar* key, element_count count, 
				            Unique *unique);
};

#endif /* UNIQUE_INCLUDED */
