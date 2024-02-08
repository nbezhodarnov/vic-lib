#ifndef DYNAMIC_ALLOCATION_H
#define DYNAMIC_ALLOCATION_H

#include "vic.h"

void _ef_lock(vic_ef_t* ef);
void _ef_unlock(vic_ef_t* ef);

typedef struct data_pointer {
    unsigned long long int key;
    unsigned long long int tid;
} data_pointer;

void _init_dynamic_memory();
void _destroy_dynamic_memory();

data_pointer _allocate(unsigned int type_size);
data_pointer _allocate_array(unsigned int size, unsigned int type_size);

void _deallocate(data_pointer ptr);

void* _read(data_pointer ptr);
void* _read_from_array(data_pointer ptr, unsigned int index);
void _read_values_from_array(data_pointer ptr, void* out_array, unsigned int size, unsigned int start_index, unsigned int end_index);

void _write(data_pointer ptr, void* value);
void _write_to_array(data_pointer ptr, unsigned int index, void* value);
void _write_values_to_array(data_pointer ptr, void* values, unsigned int size, unsigned int start_index, unsigned int end_index);

#define define_data_ptr(type) \
    struct _ptr_##type; \
    \
    typedef struct _ptr_functions_##type {  \
        struct _ptr_##type (*allocate)(vic_ef_t* ef); \
        struct _ptr_##type (*allocate_array)(unsigned int size, vic_ef_t* ef); \
        void (*deallocate)(struct _ptr_##type ptr); \
        type (*read)(const struct _ptr_##type ptr); \
        type (*read_from_array)(const struct _ptr_##type ptr, unsigned int index); \
        void (*read_values_from_array)(const struct _ptr_##type ptr, type values[], unsigned int size, unsigned int start_index, unsigned int end_index); \
        void (*write)(struct _ptr_##type ptr, type value); \
        void (*write_to_array)(struct _ptr_##type ptr, unsigned int index, type value); \
        void (*write_values_to_array)(struct _ptr_##type ptr, type values[], unsigned int size, unsigned int start_index, unsigned int end_index); \
    } _ptr_functions_##type; \
    \
    struct _ptr_##type _allocate_##type(vic_ef_t* ef); \
    struct _ptr_##type _allocate_array_##type(unsigned int size, vic_ef_t* ef); \
    void _deallocate_##type(struct _ptr_##type ptr); \
    type _read_##type(const struct _ptr_##type ptr); \
    type _read_from_array_##type(const struct _ptr_##type ptr, unsigned int index); \
    void _read_values_from_array_##type(const struct _ptr_##type ptr, type out_array[], unsigned int size, unsigned int start_index, unsigned int end_index); \
    void _write_##type(struct _ptr_##type ptr, type value); \
    void _write_to_array_##type(struct _ptr_##type ptr, unsigned int index, type value); \
    void _write_values_to_array_##type(struct _ptr_##type ptr, type values[], unsigned int size, unsigned int start_index, unsigned int end_index); \
    \
    _ptr_functions_##type _ptr_##type##_functions = { \
        _allocate_##type, \
        _allocate_array_##type, \
        _deallocate_##type, \
        _read_##type, \
        _read_from_array_##type, \
        _read_values_from_array_##type, \
        _write_##type, \
        _write_to_array_##type, \
        _write_values_to_array_##type \
    }; \
    \
    typedef struct _ptr_##type { \
        unsigned long long int key; \
        unsigned long long int tid; \
        _ptr_functions_##type* functions; \
        vic_ef_t* ef; \
    } _ptr_##type; \
    \
    _ptr_##type _allocate_##type(vic_ef_t* ef) { \
        data_pointer base_ptr = _allocate(sizeof(type)); \
        _ptr_##type _ptr; \
        _ptr.key = base_ptr.key; \
        _ptr.tid = base_ptr.tid; \
        _ptr.functions = &_ptr_##type##_functions; \
        _ptr.ef = ef; \
        return _ptr; \
    } \
    \
    _ptr_##type _allocate_array_##type(unsigned int size, vic_ef_t* ef) { \
        data_pointer base_ptr = _allocate_array(size, sizeof(type)); \
        _ptr_##type _ptr; \
        _ptr.key = base_ptr.key; \
        _ptr.tid = base_ptr.tid; \
        _ptr.functions = &_ptr_##type##_functions; \
        _ptr.ef = ef; \
        return _ptr; \
    } \
    \
    void _deallocate_##type(_ptr_##type ptr) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        _deallocate(base_ptr); \
        _ef_unlock(ptr.ef); \
    } \
    \
    type _read_##type(const _ptr_##type ptr) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        type value = *(type*)_read(base_ptr); \
        _ef_unlock(ptr.ef); \
        return value; \
    } \
    \
    type _read_from_array_##type(const _ptr_##type ptr, unsigned int index) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        type value = *(type*)_read_from_array(base_ptr, index); \
        _ef_unlock(ptr.ef); \
        return value; \
    } \
    \
    void _read_values_from_array_##type(const _ptr_##type ptr, type out_array[], unsigned int size, unsigned int start_index, unsigned int end_index) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        _read_values_from_array(base_ptr, out_array, size, start_index, end_index); \
        _ef_unlock(ptr.ef); \
    } \
    \
    void _write_##type(_ptr_##type ptr, type value) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        _write(base_ptr, &value); \
        _ef_unlock(ptr.ef); \
    } \
    \
    void _write_to_array_##type(_ptr_##type ptr, unsigned int index, type value) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        _write_to_array(base_ptr, index, &value); \
        _ef_unlock(ptr.ef); \
    } \
    \
    void _write_values_to_array_##type(_ptr_##type ptr, type values[], unsigned int size, unsigned int start_index, unsigned int end_index) { \
        data_pointer base_ptr; \
        base_ptr.key = ptr.key; \
        base_ptr.tid = ptr.tid; \
        _ef_lock(ptr.ef); \
        _write_values_to_array(base_ptr, values, size, start_index, end_index); \
        _ef_unlock(ptr.ef); \
    }

#define data_ptr(type) _ptr_##type

#define allocate(type, ef) _allocate_##type(ef)
#define allocate_array(type, size, ef) _allocate_array_##type(size, ef)

#define deallocate(ptr) ptr.functions->deallocate(ptr)

#define read_value(ptr) ptr.functions->read(ptr)
#define read_value_from_array(ptr, index) ptr.functions->read_from_array(ptr, index)
#define read_values_from_array_range(ptr, out_array, size, start_index, end_index) ptr.functions->read_values_from_array(ptr, out_array, size, start_index, end_index)
#define read_values_from_array_start(ptr, out_array, size, start_index) ptr.functions->read_values_from_array(ptr, out_array, size, start_index, size)
#define read_all_values_from_array(ptr, out_array, size) ptr.functions->read_values_from_array(ptr, out_array, size, 0, size)

#define write_value(ptr, value) ptr.functions->write(ptr, value)
#define write_value_to_array(ptr, index, value) ptr.functions->write_to_array(ptr, index, value)
#define write_values_to_array_range(ptr, values, size, start_index, end_index) ptr.functions->write_values_to_array(ptr, values, size, start_index, end_index)
#define write_values_to_array_start(ptr, values, size, start_index) ptr.functions->write_values_to_array(ptr, values, size, start_index, size)
#define write_all_values_to_array(ptr, values, size) ptr.functions->write_values_to_array(ptr, values, size, 0, size)

void export_dynamic_data(char* filename);
void import_dynamic_data(char* filename);

#endif
