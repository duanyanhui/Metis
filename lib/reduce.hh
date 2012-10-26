#ifndef REDUCE_H
#define REDUCE_H

#include "mr-types.hh"
#include "value_helper.hh"
#include "reduce_bucket_manager.hh"
#include "bench.hh"
#include "application.hh"
#include <assert.h>
#include <string.h>
#ifdef JOS_USER
#include <inc/compiler.h>
#endif

struct reduce_emit_functor {
    void operator()(keyvals_t &p) const {
        if (the_app_->application_type() == atype_mapreduce)
            static_cast<map_reduce *>(the_app_)->internal_reduce_emit(p);
	else
            static_cast<map_group *>(the_app_)->internal_reduce_emit(p);
    }
    static reduce_emit_functor &instance() {
        static reduce_emit_functor in;
        return in;
    }
};

struct append_functor {
    append_functor(xarray<keyvals_t> *x) : x_(x) {}
    void operator()(keyvals_t &kvs) {
	// kvs.vals is owned by callee
        x_->push_back(kvs);
        kvs.init();
    }
  private:
    xarray<keyvals_t> *x_;
};

template <typename C, typename F>
inline void group_one_sorted(C &a, F &f) {
    // group and apply functor
    size_t n = a.size();
    keyvals_t kvs;
    for (size_t i = 0; i < n;) {
	kvs.key = a[i].key;
        map_values_mv(&kvs, &a[i]);
        ++i;
        for (; i < n && !comparator::key_compare(kvs.key, a[i].key); ++i)
	    map_values_mv(&kvs, &a[i]);
        f(kvs);
    }
}

template <typename C, typename F, typename PC>
inline void group_unsorted(C **a, int na, F &f, PC pc) {
    if (na == 1) {
        a[0]->sort(pc);
        group_one_sorted(*a[0], f);
    }
    if (na <= 1)
        return;
    size_t np = 0;
    for (int i = 0; i < na; i++)
        np += a[i]->size();
    C *one = new C;
    one->set_capacity(np);
    for (int i = 0; i < na; i++)
        one->append(*a[i]);
    one->sort(pc);
    group_one_sorted(*one, f);
    delete one;
}

template <typename C, typename F>
inline void group_sorted(C **nodes, int n, F &f) {
    if (!n)
        return;
    typename C::iterator it[JOS_NCPU];
    for (int i = 0; i < n; i++)
	 it[i] = nodes[i]->begin();
    int marks[JOS_NCPU];
    keyvals_t dst;
    while (1) {
	int min_idx = -1;
	bzero(marks, sizeof(marks));
	int m = 0;
	// Find minimum key
	for (int i = 0; i < n; ++i) {
	    if (it[i] == nodes[i]->end())
		continue;
	    int cmp = 0;
	    if (min_idx >= 0)
		cmp = comparator::key_compare(it[min_idx]->key, it[i]->key);
	    if (min_idx < 0 || cmp > 0) {
		++ m;
		marks[i] = m;
		min_idx = i;
	    } else if (!cmp)
		marks[i] = m;
	}
	if (min_idx < 0)
	    break;
        // Merge all the values with the same mimimum key.
	dst.key = it[min_idx]->key;
	for (int i = 0; i < n; i++) {
	    if (marks[i] != m)
		continue;
	    do {
		map_values_mv(&dst, &(*it[i]));
                ++it[i];
	    } while (it[i] != nodes[i]->end() &&
                     comparator::key_compare(dst.key, it[i]->key) == 0);
	}
        f(dst);
    }
}

#endif
