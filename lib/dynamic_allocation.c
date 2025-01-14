#include "dynamic_allocation.h"

#include <tpl.h>

#include <threads.h>
#include <stdatomic.h>
#include <signal.h>
#include <sys/syscall.h>

#define _GNU_SOURCE
#include <unistd.h>

#include <assert.h>

#include <stdio.h>

int _compare(data_pointer val_1, data_pointer val_2) {
    if (val_1.tid < val_2.tid) {
        return -1;
    }
    else if (val_1.tid > val_2.tid) {
        return 1;
    }

    if (val_1.key < val_2.key) {
        return -1;
    }
    else if (val_1.key > val_2.key) {
        return 1;
    }

    return 0;
}

#define CC_NO_SHORT_NAMES
#include "third_party/cc/cc.h"
#define CC_CMPR data_pointer, { return _compare(val_1, val_2); }
#define CC_HASH data_pointer, { return (val.tid + val.key) * (val.tid + val.key + 1) / 2 + val.tid; }
#include "third_party/cc/cc.h"

typedef struct base_data_allocation_struct {
    void *data;
    unsigned long long size;
    unsigned long long capacity;
} base_data_allocation_struct;

cc_map(data_pointer, base_data_allocation_struct) dynamic_memory_storage;
atomic_int key_counter = 0;

bool initialized = false;

void _init_dynamic_memory()
{
    if (!initialized) {
        initialized = true;
        cc_init(&dynamic_memory_storage);
    }
}

void _destroy_dynamic_memory()
{
    if (!initialized) {
        return;
    }

    if (cc_size(&dynamic_memory_storage) > 0) {
        cc_for_each(&dynamic_memory_storage, key_ptr, value_ptr) {
            free(value_ptr->data);
        }
    }

    cc_cleanup(&dynamic_memory_storage);
    initialized = false;
}

data_pointer _allocate(unsigned int type_size)
{
    if (!initialized) {
        initialized = true;
        cc_init(&dynamic_memory_storage);
    }

    data_pointer ptr;
    ptr.tid = syscall(__NR_gettid);
    ptr.key = atomic_fetch_add(&key_counter, 1);

    base_data_allocation_struct base_data;
    base_data.size = type_size;
    base_data.capacity = 1;
    base_data.data = malloc(type_size);

    cc_insert(&dynamic_memory_storage, ptr, base_data);

    return ptr;
}

data_pointer _allocate_array(unsigned int size, unsigned int type_size)
{
    if (!initialized) {
        initialized = true;
        cc_init(&dynamic_memory_storage);
    }

    data_pointer ptr;
    ptr.tid = syscall(__NR_gettid);
    ptr.key = atomic_fetch_add(&key_counter, 1);

    base_data_allocation_struct base_data;
    base_data.size = type_size;
    base_data.capacity = size;
    base_data.data = malloc(type_size * size);

    cc_insert(&dynamic_memory_storage, ptr, base_data);

    return ptr;
}

void _deallocate(data_pointer ptr)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        free(base_data->data);
        cc_erase(&dynamic_memory_storage, ptr);
    }

    if (cc_size(&dynamic_memory_storage) == 0) {
        initialized = false;
        cc_cleanup(&dynamic_memory_storage);
    }
}

void* _read(data_pointer ptr)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        return base_data->data;
    }

    return NULL;
}

void* _read_from_array(data_pointer ptr, unsigned int index)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        return (void*)((char*)base_data->data + index * base_data->size);
    }

    return NULL;
}

void _read_values_from_array(data_pointer ptr, void* out_array, unsigned int size, unsigned int start_index, unsigned int end_index)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        memcpy(out_array, (char*)base_data->data + start_index * base_data->size, (end_index - start_index) * base_data->size);
    }
}

void _write(data_pointer ptr, void* value)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        memcpy(base_data->data, value, base_data->size);
    }
}

void _write_to_array(data_pointer ptr, unsigned int index, void* value)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        memcpy((char*)base_data->data + index * base_data->size, value, base_data->size);
    }
}

void _write_values_to_array(data_pointer ptr, void* values, unsigned int size, unsigned int start_index, unsigned int end_index)
{
    base_data_allocation_struct *base_data = cc_get(&dynamic_memory_storage, ptr);
    if (base_data != NULL) {
        memcpy((char*)base_data->data + start_index * base_data->size, values, (end_index - start_index) * base_data->size);
    }
}

void export_dynamic_data(char* filename)
{
    data_pointer key = {0, 0};
    base_data_allocation_struct value = {NULL, 0, 0};

    tpl_bin tb;
    tpl_node *tn = tpl_map("A(UUUUB)", &key.key, &key.tid, &value.size, &value.capacity, &tb);

    cc_for_each(&dynamic_memory_storage, key_ptr, value_ptr) {
        key.key = key_ptr->key;
        key.tid = key_ptr->tid;

        tb.addr = value_ptr->data;
        tb.sz = value_ptr->size * value_ptr->capacity;
        value.size = value_ptr->size;
        value.capacity = value_ptr->capacity;

        tpl_pack(tn, 1);
    }

    tpl_dump(tn, TPL_FILE, filename);
    tpl_free(tn);
}

void import_dynamic_data(char* filename)
{
    data_pointer key = {0, 0};
    base_data_allocation_struct value = {NULL, 0, 0};

    tpl_bin tb;
    tpl_node *tn = tpl_map("A(UUUUB)", &key.key, &key.tid, &value.size, &value.capacity, &tb);
    tpl_load(tn, TPL_FILE, filename);

    while (tpl_unpack(tn, 1) > 0) {
        value.data = tb.addr;

        cc_insert(&dynamic_memory_storage, key, value);
    }

    tpl_free(tn);
}
